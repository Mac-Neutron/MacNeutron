"""eac-vm/d3d/gen.py's model, manifest and rules (Direct3D forwarding M2, Tasks 2 and 3), against native DXMT's own headers
(build/dxmt-native/install/include-native, eac-vm/dxmt-native/build.sh) and rules.txt.
    python3 -m unittest eac-vm/d3d/tests/test_gen.py"""
import contextlib
import io
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "eac-vm", "d3d"))
import gen  # noqa: E402

INCLUDE = os.path.join(ROOT, "build", "dxmt-native", "install", "include-native")
RULES = os.path.join(ROOT, "eac-vm", "d3d", "rules.txt")


def model():
    return gen.load(INCLUDE, RULES)


class ModelTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.m = model()

    def test_interface_counts(self):
        d3d12 = [i for i in self.m.interfaces.values() if i.header == "d3d12.h"]
        dxgi = [i for i in self.m.interfaces.values() if i.header.startswith("dxgi")]
        self.assertEqual(len(d3d12), 62)
        self.assertEqual(len(dxgi), 44)

    def test_fence_methods_in_vtable_order(self):
        names = [meth.name for meth in self.m.interfaces["ID3D12Fence"].methods]
        self.assertEqual(names, ["QueryInterface", "AddRef", "Release", "GetPrivateData", "SetPrivateData",
                                 "SetPrivateDataInterface", "SetName", "GetDevice", "GetCompletedValue",
                                 "SetEventOnCompletion", "Signal"])

    def test_base(self):
        self.assertEqual(self.m.interfaces["ID3D12Device5"].base, "ID3D12Device4")
        self.assertEqual(self.m.interfaces["ID3D12Object"].base, "IUnknown")

    def test_union(self):
        fields = self.m.structs["D3D12_RESOURCE_BARRIER"].fields
        unions = [f for f in fields if f.inline is not None and f.inline.union]
        self.assertEqual(len(unions), 1)
        self.assertEqual([f.name for f in unions[0].inline.fields], ["Transition", "Aliasing", "UAV"])

    def test_explicit_aggregate_return(self):
        get_desc = [meth for meth in self.m.interfaces["ID3D12Resource"].methods if meth.name == "GetDesc"][0]
        self.assertEqual(get_desc.ret, "D3D12_RESOURCE_DESC *")
        self.assertEqual([(p.type, p.name) for p in get_desc.params], [("D3D12_RESOURCE_DESC *", "__ret")])

    def test_iid(self):
        self.assertEqual(self.m.interfaces["ID3D12Device"].iid, "189819f1-1db6-4b57-be54-1821339b85f7")

    def test_private_interfaces(self):
        ext = self.m.interfaces["IMTLD3D12DeviceExt"]
        self.assertEqual(ext.iid, "a8c64ba7-6d54-4833-8cc6-2605c5b454be")
        self.assertEqual([meth.name for meth in ext.methods][3:], ["GetTemporalScalerScaleRange",
                                                                    "CreateTemporalScaler"])
        self.assertIn("MTL_TEMPORAL_UPSCALE_D3D12_DESC", self.m.structs)

    def test_exports(self):
        names = [e.name for e in self.m.exports]
        for name in ("D3D12CreateDevice", "D3D12SerializeRootSignature", "CreateDXGIFactory2"):
            self.assertIn(name, names)


class ManifestTest(unittest.TestCase):
    def test_stable_and_from_16(self):
        a, b = gen.manifest(model()), gen.manifest(model())
        self.assertEqual(a, b)
        self.assertEqual(a[0][0], 16)
        ids = [row[0] for row in a]
        self.assertEqual(ids, list(range(16, 16 + len(ids))))

    def test_declared_methods_only(self):
        rows = gen.manifest(model())
        names = [row[1] for row in rows]
        self.assertIn("ID3D12Fence::SetEventOnCompletion", names)
        self.assertNotIn("ID3D12Fence1::SetEventOnCompletion", names)  # inherited: its declaring interface's id
        self.assertNotIn("ID3D12Fence::QueryInterface", names)  # IUnknown's are the guest's own
        slot = [row[2] for row in rows if row[1] == "ID3D12Fence::SetEventOnCompletion"][0]
        self.assertEqual(slot, 9)


def spec(calls, model, name):
    """The generated call spec for <Interface>::<Method>."""
    ids = {row[1]: row[0] for row in gen.manifest(model)}
    return calls[ids[name]]


class ClassifyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.m = model()
        cls.calls = gen.classify(cls.m, gen.parse_rules(gen.read(RULES), cls.m))

    def test_defaults(self):
        c = spec(self.calls, self.m, "ID3D12Device::CreateCommittedResource")
        by = {f.param: f for f in c.fields}
        self.assertEqual((by["heap_properties"].kind, by["heap_properties"].size), ("WMF_IN", 20))
        self.assertEqual(by["resource"].kind, "WMF_HANDLE_OUT")
        self.assertEqual(c.mode, "sync")

    def test_fixed_array_and_count(self):
        c = spec(self.calls, self.m, "ID3D12GraphicsCommandList::ClearRenderTargetView")
        by = {f.param: f for f in c.fields}
        self.assertEqual((by["color"].kind, by["color"].size, by["color"].array), ("WMF_IN", 16, False))
        self.assertEqual((by["rects"].kind, by["rects"].array, by["rects"].count), ("WMF_IN", True, "rect_count"))
        self.assertEqual(c.mode, "async")

    def test_unruled_arrays_stop(self):
        rules = gen.parse_rules("\n".join(l for l in gen.read(RULES).splitlines()
                                          if not l.startswith("ID3D12Device::CopyDescriptors ")), self.m)
        with self.assertRaises(gen.RuleError) as e:
            gen.classify(self.m, rules)
        self.assertIn("ID3D12Device::CopyDescriptors", str(e.exception))

    def test_cli_names_unruled_methods(self):
        with tempfile.TemporaryDirectory() as d:
            rules = os.path.join(d, "rules.txt")
            with open(rules, "w") as f:
                f.write("\n".join(l for l in gen.read(RULES).splitlines()
                                   if not l.startswith("ID3D12Device::CopyDescriptors ")))
            err = io.StringIO()
            with contextlib.redirect_stderr(err):
                rc = gen.main(["--headers", INCLUDE, "--rules", rules, "--out", d, "calls"])
            self.assertEqual(rc, 1)
            self.assertIn("ID3D12Device::CopyDescriptors", err.getvalue())
            self.assertFalse(os.path.exists(os.path.join(d, "calls.c")))

    def test_wrap(self):
        # wrap (M3): guest code runs first and then the generated proxy, which forwards as the rules say
        rules = gen.parse_rules(gen.read(RULES) + "\nID3D12Device::GetNodeCount wrap\n", self.m)
        calls = gen.classify(self.m, rules)
        out, _ = gen.emit_proxies(self.m, rules, calls)
        wid = next(i for i, c in calls.items() if c.name == "ID3D12Device::GetNodeCount")
        self.assertEqual(calls[wid].mode, "sync")
        self.assertIn(f'mn_d3d_special("ID3D12Device::GetNodeCount", (const void *)px_{wid})', out)
        start = out.index(f"px_{wid}(")
        self.assertIn("mn_d3d_call(", out[start:out.index("\n}", start)])
        self.assertNotIn('mn_d3d_special("ID3D12Device::GetAdapterLuid"', out)

    def test_wrap_an_export(self):
        # an export's wrap (M3): the generated proxy is mn_px_<name>, which guest/proxy.cpp's export calls
        rules = gen.parse_rules(gen.read(RULES) + "\n::DXGIGetDebugInterface1 wrap\n", self.m)
        calls = gen.classify(self.m, rules)
        out, _ = gen.emit_proxies(self.m, rules, calls)
        self.assertIn('extern "C" HRESULT WINAPI mn_px_DXGIGetDebugInterface1(', out)
        self.assertNotIn('extern "C" HRESULT WINAPI DXGIGetDebugInterface1(', out)
        self.assertIn('extern "C" HRESULT WINAPI CreateDXGIFactory1(', out)

    def test_a_list_records(self):
        # a command list's method (M3 Task 10, SMITE 2's) goes through mn_d3d_record, which keeps a list handed between
        # threads in order; another interface's through mn_d3d_call
        out, _ = gen.emit_proxies(self.m, gen.parse_rules(gen.read(RULES), self.m), self.calls)
        def body(name):
            mid = next(i for i, c in self.calls.items() if c.name == name)
            start = out.index(f"px_{mid}(")
            return out[start:out.index("\n}", start)]
        self.assertIn("mn_d3d_record(This, ", body("ID3D12GraphicsCommandList::EndQuery"))
        self.assertIn("mn_d3d_record(This, ", body("ID3D12GraphicsCommandList::Close"))
        self.assertIn("mn_d3d_record(This, ", body("ID3D12GraphicsCommandList1::OMSetDepthBounds"))
        self.assertNotIn("mn_d3d_record", body("ID3D12Device::GetNodeCount"))
        # M4: a D3D11 context's and a D3D10 device's (a deferred context recorded on one thread, the immediate one
        # called from several), not a device child's
        self.assertIn("mn_d3d_record(This, ", body("ID3D11DeviceContext::Draw"))
        self.assertIn("mn_d3d_record(This, ", body("ID3D11DeviceContext1::ClearView"))
        self.assertIn("mn_d3d_record(This, ", body("ID3D10Device::Draw"))
        self.assertNotIn("mn_d3d_record", body("ID3D10DeviceChild::GetDevice"))
        self.assertNotIn("mn_d3d_record", body("ID3D11Device::GetCreationFlags"))

    VIEWS = ("interface ITestViews : IUnknown 01234567-89ab-cdef-0123-456789abcdef\n"
             "HRESULT Get(UINT NumViews, ITestViews **views);\n"
             "HRESULT Name(LPCSTR name, LPCWSTR wide);\n"
             "end\n")

    def views(self, rule=True):
        m = model()
        gen.parse_rules_declarations(self.VIEWS, m)
        text = gen.read(RULES) + ("\nITestViews::Get views out count=NumViews\n" if rule else "")
        rules = gen.parse_rules(text, m)
        return m, rules

    def test_masks(self):
        # M4: D3D11 and D3D10 take the model past 128 interfaces: the answers' masks hold 256, the reply's block is
        # 9 words (answers 0-3, moved 4-7, the first handle 8)
        m = gen.Model()
        for i in range(256):
            m.interfaces[f"I{i}"] = gen.Interface(f"I{i}", f"{i:08x}-0000-0000-0000-000000000000", "IUnknown", [], "t")
        self.assertEqual(len(gen.iface_index(m)), 256)
        m.interfaces["I256"] = gen.Interface("I256", "00000100-0000-0000-0000-000000000001", "IUnknown", [], "t")
        with self.assertRaises(gen.RuleError):
            gen.iface_index(m)
        self.assertIn("_ifaces[9]", gen.emit_params(self.calls))

    def test_objects_out(self):
        # a getter's array of objects out (M4): 8 bytes an element on the wire, the objects' first handles; the
        # proxy makes each the guest's proxy again, the stub turns each native pointer into its handle
        m, rules = self.views()
        calls = gen.classify(m, rules)
        mid = next(i for i, c in calls.items() if c.name == "ITestViews::Get")
        f = next(f for f in calls[mid].fields if f.param == "views")
        self.assertEqual((f.kind, f.size, f.array, f.count, f.objects), ("WMF_OUT", 8, True, "NumViews", "ITestViews"))
        out, _ = gen.emit_proxies(m, rules, calls)
        start = out.index(f"px_{mid}(")
        self.assertIn("mn_proxy_known(", out[start:out.index("\n}", start)])
        stubs = gen.emit_stubs(m, rules, calls)
        start = stubs.index(f"mn_d3d_stub_{mid}(")
        self.assertIn("mn_d3d_objects_out(", stubs[start:stubs.index("\n}", start)])

    def test_objects_out_needs_a_rule(self):
        m, rules = self.views(rule=False)
        with self.assertRaises(gen.RuleError) as e:
            gen.classify(m, rules)
        self.assertIn("ITestViews::Get views", str(e.exception))

    def test_string_typedefs(self):
        m, rules = self.views()
        calls = gen.classify(m, rules)
        c = next(c for c in calls.values() if c.name == "ITestViews::Name")
        self.assertEqual([f.kind for f in c.fields], ["WMF_STR_IN", "WMF_WSTR_IN"])

    def test_d3d11_counts(self):
        # M4: D3D11 and D3D10 in the model (ID3D10Multithread has ID3D11Multithread's IID and vtable: one of them)
        self.assertEqual(len(gen.iface_index(self.m)), 204)
        self.assertNotIn("ID3D10Multithread", self.m.interfaces)
        names = {c.name for c in self.calls.values()}
        for n in ("::D3D11CreateDevice", "::D3D11CoreCreateDevice", "::D3D11CreateDeviceAndSwapChain",
                  "::D3D10CoreCreateDevice"):
            self.assertIn(n, names)
        self.assertEqual(sum(1 for n in names if n.startswith(("ID3D11", "ID3D10", "::D3D1"))), 522)

    def test_d3d11_getters_are_arrays(self):
        c = spec(self.calls, self.m, "ID3D11DeviceContext::PSGetShaderResources")
        f = next(f for f in c.fields if f.param == "ppShaderResourceViews")
        self.assertEqual((f.objects, f.count), ("ID3D11ShaderResourceView", "NumViews"))
        c = spec(self.calls, self.m, "ID3D10Device::OMGetRenderTargets")
        self.assertEqual(next(f for f in c.fields if f.param == "ppRenderTargetViews").objects, "ID3D10RenderTargetView")

    def test_d3d11_own(self):
        # initial data and updates: one region the own code sizes, aux[0] bytes
        for name, param in (("ID3D11Device::CreateTexture2D", "pInitialData"),
                            ("ID3D11DeviceContext::UpdateSubresource", "pSrcData"),
                            ("ID3D10Device::CreateBuffer", "pInitialData")):
            c = spec(self.calls, self.m, name)
            f = next(f for f in c.fields if f.param == param)
            self.assertEqual((f.kind, f.count, f.count_width), ("WMF_IN", "aux[0]", 8), name)
        rules = gen.parse_rules(gen.read(RULES), self.m)
        for name in ("ID3D11Device::CreateBuffer", "ID3D11DeviceContext::Map", "ID3D10Texture2D::Map"):
            self.assertIn(name, rules.own)

    def test_every_method_has_a_mode(self):
        for row in gen.manifest(self.m):
            self.assertIn(self.calls[row[0]].mode, ("async", "sync", "submit", "special", "refuse"), row[1])

    def test_layout(self):
        size = lambda t: gen.size_align(self.m, t)[0]
        self.assertEqual(size("D3D12_RESOURCE_BARRIER"), 32)
        self.assertEqual(size("D3D12_HEAP_PROPERTIES"), 20)
        self.assertEqual(size("D3D12_RESOURCE_DESC"), 56)
        self.assertEqual(size("DXGI_SAMPLE_DESC"), 8)


if __name__ == "__main__":
    unittest.main()

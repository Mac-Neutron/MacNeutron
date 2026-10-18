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

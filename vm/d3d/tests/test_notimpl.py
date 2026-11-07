"""Every generated host stub and the six hand-written D3D11/D3D10 stubs in host/native.cpp check
winemetal's thread-local MTLD3DNotImplemented flag after their call, alongside catch (...) (DXMTRS step 5b Task 16).
    python3 -m unittest vm/d3d/tests/test_notimpl.py"""
import os
import re
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "vm", "d3d"))
import gen  # noqa: E402

BUILD = os.environ.get("BUILD_DIR") or os.path.join(ROOT, "build")
INCLUDE = os.environ.get("I") or os.path.join(BUILD, "dxmt-native", "install", "include-native")
RULES = os.path.join(ROOT, "vm", "d3d", "rules.txt")
NATIVE_CPP = os.path.join(ROOT, "vm", "d3d", "host", "native.cpp")
DISPATCH_C = os.path.join(ROOT, "vm", "d3d", "host", "dispatch.c")
STUBS_H = os.path.join(ROOT, "vm", "d3d", "host", "stubs.h")


def missing_notimpl_stubs(stubs_text, calls):
    """Return the list of (mid, name) for generated stubs that do not check mn_not_impl before catch (...)."""
    missing = []
    for mid in sorted(calls):
        c = calls[mid]
        if c.mode in ("special", "refuse"):
            continue
        marker = f'extern "C" NTSTATUS_T mn_d3d_stub_{mid}(void *params)'
        start = stubs_text.find(marker)
        if start < 0:
            missing.append((mid, c.name))
            continue
        end = stubs_text.index("\n}", start)
        body = stubs_text[start:end]
        ret = "&a->ret" if c.ret == "HRESULT" else "nullptr"
        want = f"if (mn_not_impl && mn_not_impl()) return mn_d3d_not_in_dxmt({mid}, {ret});"
        catch_pos = body.find("} catch (...) {")
        want_pos = body.find(want)
        if want_pos < 0 or catch_pos < 0 or want_pos > catch_pos:
            missing.append((mid, c.name))
    return missing


class NotImplTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.m = gen.load(INCLUDE, RULES)
        cls.rules = gen.parse_rules(gen.read(RULES), cls.m)
        cls.calls = gen.classify(cls.m, cls.rules)
        cls.stubs = gen.emit_stubs(cls.m, cls.rules, cls.calls)

    def test_every_generated_stub_checks_flag(self):
        missing = missing_notimpl_stubs(self.stubs, self.calls)
        self.assertEqual(missing, [], f"{len(missing)} stubs missing mn_not_impl check, first: {missing[:5]}")

    def test_stub_without_flag_fails_check(self):
        first_mid = next(mid for mid in sorted(self.calls) if self.calls[mid].mode not in ("special", "refuse"))
        ret = "&a->ret" if self.calls[first_mid].ret == "HRESULT" else "nullptr"
        target = f"if (mn_not_impl && mn_not_impl()) return mn_d3d_not_in_dxmt({first_mid}, {ret});"
        self.assertIn(target, self.stubs)
        broken = self.stubs.replace(target, "", 1)
        self.assertEqual(missing_notimpl_stubs(broken, self.calls), [(first_mid, self.calls[first_mid].name)])

    def test_native_cpp_and_dispatch_plumbing(self):
        native = gen.read(NATIVE_CPP)
        tries = list(re.finditer(r"\btry\s*\{(.*?)\}\s*catch\s*\(\.\.\.\)", native, re.S))
        self.assertEqual(len(tries), 6)
        for m in tries:
            self.assertIn("mn_not_impl && mn_not_impl()", m.group(1))
        dispatch = gen.read(DISPATCH_C)
        self.assertIn('"MTLD3DNotImplemented"', dispatch)
        self.assertIn("mn_not_impl", gen.read(STUBS_H))


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""The Direct3D forwarding generator (spec §4.2; M2 plan Tasks 2 and 3).

Reads the widl-generated headers native DXMT compiles against (build/dxmt-native/install/include-native: d3d12.h,
d3dcommon.h, dxgi*.h), whose C vtables list every interface's methods in vtable order, and rules.txt: DXMT's private
interfaces (declared there in the headers' C prototype form) and the annotations each method needs. Writes, under
--out:
    manifest.txt   one line a forwarded method or export: "<id> <Interface>::<Method> <slot>" ("<id> ::<Export> -1"):
                   ids from 16 (0-15 are the family's own ops), declared methods only, header order, then the exports

    gen.py --headers <include-native> --rules <rules.txt> --out <dir> manifest
"""
import argparse
import os
import re
import sys
from dataclasses import dataclass, field
from typing import Dict, List, Optional

FIRST_ID = 16
IUNKNOWN_METHODS = 3
# The headers the model reads, in order (paths under include-native): their interfaces are numbered in this order.
HEADERS = ["d3d12/d3d12.h", "directx/d3dcommon.h", "directx/dxgi.h", "directx/dxgi1_2.h", "directx/dxgi1_3.h",
           "directx/dxgi1_4.h", "directx/dxgi1_5.h", "directx/dxgi1_6.h", "directx/dxgidebug.h"]
# The exports games call: d3d12.def's and dxgi.def's (DXMT's own lists).
EXPORTS = {"d3d12.dll": ["D3D12CreateDevice", "D3D12GetDebugInterface", "D3D12CreateRootSignatureDeserializer",
                         "D3D12CreateVersionedRootSignatureDeserializer", "D3D12SerializeRootSignature",
                         "D3D12SerializeVersionedRootSignature", "D3D12GetInterface"],
           "dxgi.dll": ["CreateDXGIFactory", "CreateDXGIFactory1", "CreateDXGIFactory2",
                        "DXGIDeclareAdapterRemovalSupport", "DXGIGetDebugInterface1"]}


@dataclass
class Param:
    type: str
    name: str


@dataclass
class Method:
    name: str
    ret: str
    params: List[Param]


@dataclass
class Interface:
    name: str
    iid: Optional[str]
    base: Optional[str]
    methods: List[Method]
    header: str


@dataclass
class Struct:
    name: Optional[str]
    fields: List["Field"]
    union: bool = False


@dataclass
class Field:
    type: str
    name: str
    array: Optional[str] = None  # the length's text, for an array field ("3*4" for [3][4])
    inline: Optional[Struct] = None  # a struct or union declared in place
    bits: Optional[int] = None  # a bitfield's width


@dataclass
class Export:
    name: str
    dll: str
    ret: str
    params: List[Param]


@dataclass
class Model:
    interfaces: Dict[str, Interface] = field(default_factory=dict)
    structs: Dict[str, Struct] = field(default_factory=dict)
    enums: Dict[str, Dict[str, str]] = field(default_factory=dict)
    typedefs: Dict[str, str] = field(default_factory=dict)
    exports: List[Export] = field(default_factory=list)


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def norm_type(t):
    """'const  D3D12_RANGE*' -> 'const D3D12_RANGE *', 'void **' -> 'void **'."""
    stars = t.count("*")
    base = re.sub(r"\s+", " ", t.replace("*", " ")).strip()
    return base + (" " + "*" * stars if stars else "")


def split_top(text, sep=","):
    """Splits at sep outside brackets."""
    parts, depth, cur = [], 0, []
    for ch in text:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == sep and depth == 0:
            parts.append("".join(cur))
            cur = []
        else:
            cur.append(ch)
    if "".join(cur).strip():
        parts.append("".join(cur))
    return parts


def parse_param(text):
    text = text.strip()
    if text in ("", "void"):
        return None
    m = re.match(r"^(.*?)(\w+)\s*(\[[^\]]*\])?$", text, re.S)
    if not m:
        raise ValueError(f"can't read the parameter {text!r}")
    typ, name, arr = m.group(1), m.group(2), m.group(3)
    if not typ.strip():  # an unnamed parameter: the whole text is the type
        return Param(norm_type(text), "")
    return Param(norm_type(typ) + (" *" if arr else ""), name)


def parse_params(text):
    return [p for p in (parse_param(t) for t in split_top(text)) if p]


VTBL = re.compile(r"typedef\s+struct\s+(\w+)Vtbl\s*\{(.*?)\}\s*\1Vtbl\s*;", re.S)
VMETHOD = re.compile(r"([\w\s\*]+?)\s*\(\s*STDMETHODCALLTYPE\s*\*\s*(\w+)\s*\)\s*\(([^;]*)\)\s*;", re.S)
GUID = re.compile(r"DEFINE_GUID\(\s*IID_(\w+)\s*,\s*0x([0-9a-fA-F]+)\s*,\s*0x([0-9a-fA-F]+)\s*,\s*0x([0-9a-fA-F]+)\s*,"
                  r"\s*0x([0-9a-fA-F]+)\s*,\s*0x([0-9a-fA-F]+)\s*,\s*0x([0-9a-fA-F]+)\s*,\s*0x([0-9a-fA-F]+)\s*,"
                  r"\s*0x([0-9a-fA-F]+)\s*,\s*0x([0-9a-fA-F]+)\s*,\s*0x([0-9a-fA-F]+)\s*,\s*0x([0-9a-fA-F]+)\s*\)")
MIDL = re.compile(r'MIDL_INTERFACE\("([0-9a-fA-F-]+)"\)\s*(\w+)\s*:\s*public\s+(\w+)')
EXPORT = re.compile(r"(\w[\w\s\*]*?)\s+(?:__stdcall|WINAPI)\s+(\w+)\s*\(([^;]*)\)\s*;", re.S)


def guid_text(g):
    a, b, c = int(g[0], 16), int(g[1], 16), int(g[2], 16)
    d = [int(x, 16) for x in g[3:]]
    return f"{a:08x}-{b:04x}-{c:04x}-{d[0]:02x}{d[1]:02x}-" + "".join(f"{x:02x}" for x in d[2:])


def parse_vmethods(body, iface):
    methods = []
    for m in VMETHOD.finditer(body):
        params = parse_params(m.group(3))
        if not params or not params[0].type.startswith(iface):
            raise ValueError(f"{iface}::{m.group(2)}: no This parameter")
        methods.append(Method(m.group(2), norm_type(m.group(1)), params[1:]))
    return methods


def parse_struct_body(body, union=False, name=None):
    """A struct's or union's fields; members declared in place (widl's __C89_NAMELESS unions) are inline Structs."""
    fields, i = [], 0
    body = body.replace("__C89_NAMELESS", " ")
    while i < len(body):
        m = re.compile(r"\s*(struct|union)\s*(\w+)?\s*\{").match(body, i)
        if m:
            depth, j = 1, m.end()
            while depth:
                depth += {"{": 1, "}": -1}.get(body[j], 0)
                j += 1
            inner = parse_struct_body(body[m.end():j - 1], m.group(1) == "union")
            end = body.index(";", j)
            label = body[j:end].strip()
            fields.append(Field(m.group(1), label, None, inner))
            i = end + 1
            continue
        end = body.find(";", i)
        if end < 0:
            break
        decl = body[i:end].strip()
        i = end + 1
        if not decl:
            continue
        fm = re.match(r"^(.*?)(\w+)\s*((?:\[[^\]]*\]\s*)*)(?::\s*(\d+))?$", decl, re.S)
        if not fm:
            raise ValueError(f"{name}: can't read the field {decl!r}")
        dims = re.findall(r"\[([^\]]*)\]", fm.group(3))
        fields.append(Field(norm_type(fm.group(1)), fm.group(2), "*".join(d.strip() for d in dims) or None, None,
                            int(fm.group(4)) if fm.group(4) else None))
    return Struct(name, fields, union)


def find_braced(text, start):
    depth, j = 0, start
    while True:
        ch = text[j]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return j
        j += 1


def parse_types(text, model):
    for m in re.finditer(r"typedef\s+(struct|union)\s+(\w+)\s*\{", text):
        if m.group(2).endswith("Vtbl"):  # the interfaces' (parse_header)
            continue
        close = find_braced(text, m.end() - 1)
        model.structs.setdefault(m.group(2), parse_struct_body(text[m.end():close], m.group(1) == "union",
                                                                m.group(2)))
    for m in re.finditer(r"typedef\s+enum\s+(\w+)\s*\{(.*?)\}\s*\1\s*;", text, re.S):
        values = {}
        for item in split_top(m.group(2)):
            k, _, v = item.partition("=")
            if k.strip():
                values[k.strip()] = v.strip()
        model.enums.setdefault(m.group(1), values)
    for m in re.finditer(r"typedef\s+([\w\s\*]+?)\s+(\w+)\s*;", text):
        t = norm_type(m.group(1))
        if not t.startswith(("struct ", "union ", "enum ")) and m.group(2) not in model.typedefs:
            model.typedefs[m.group(2)] = t


def read(path):
    with open(path) as f:
        return f.read()


def parse_header(path, header, model):
    text = strip_comments(read(path))
    iids = {m.group(1): guid_text(m.groups()[1:]) for m in GUID.finditer(text)}
    bases = {m.group(2): (m.group(3), m.group(1).lower()) for m in MIDL.finditer(text)}
    for m in VTBL.finditer(text):
        name = m.group(1)
        base, iid = bases.get(name, (None, None))
        model.interfaces[name] = Interface(name, iids.get(name, iid), base, parse_vmethods(m.group(2), name), header)
    parse_types(text, model)
    wanted = {n: dll for dll, names in EXPORTS.items() for n in names}
    for m in EXPORT.finditer(text):
        if m.group(2) in wanted and m.group(2) not in [e.name for e in model.exports]:
            model.exports.append(Export(m.group(2), wanted[m.group(2)], norm_type(m.group(1)),
                                        parse_params(m.group(3))))


def parse_rules_declarations(text, model):
    """rules.txt's declarations: `struct <S> { fields };`, `interface <I> : <Base> <iid> ... end` with C prototypes,
    and `export <dll> <prototype>;` for an export the headers don't declare."""
    text = strip_comments(re.sub(r"#[^\n]*", " ", text))
    for m in re.finditer(r"\bstruct\s+(\w+)\s*\{", text):
        close = find_braced(text, m.end() - 1)
        model.structs[m.group(1)] = parse_struct_body(text[m.end():close], False, m.group(1))
    for m in re.finditer(r"\binterface\s+(\w+)\s*:\s*(\w+)\s+([0-9a-fA-F-]{36})(.*?)\bend\b", text, re.S):
        name, base = m.group(1), m.group(2)
        own = []
        for pm in re.finditer(r"([\w\s\*]+?)\s+(\w+)\s*\(([^;]*)\)\s*;", m.group(4), re.S):
            own.append(Method(pm.group(2), norm_type(pm.group(1)), parse_params(pm.group(3))))
        model.interfaces[name] = Interface(name, m.group(3).lower(), base, inherited(model, base) + own, "rules.txt")
    for m in re.finditer(r"\bexport\s+(\S+)\s+([\w\s\*]+?)\s+(\w+)\s*\(([^;]*)\)\s*;", text, re.S):
        if m.group(3) not in [e.name for e in model.exports]:
            model.exports.append(Export(m.group(3), m.group(1), norm_type(m.group(2)), parse_params(m.group(4))))


def inherited(model, base):
    """A private interface's vtable starts with its base's methods (IUnknown's 3)."""
    if base == "IUnknown":
        return [Method("QueryInterface", "HRESULT", [Param("REFIID", "riid"), Param("void **", "object")]),
                Method("AddRef", "ULONG", []), Method("Release", "ULONG", [])]
    return list(model.interfaces[base].methods)


def load(include_dir, rules_path):
    model = Model()
    for h in HEADERS:
        parse_header(os.path.join(include_dir, h), os.path.basename(h), model)
    parse_rules_declarations(read(rules_path), model)
    missing = [n for names in EXPORTS.values() for n in names if n not in [e.name for e in model.exports]]
    if missing:
        raise ValueError(f"exports the headers don't declare and rules.txt doesn't either: {', '.join(missing)}")
    return model


def base_count(model, iface):
    base = model.interfaces[iface].base
    if base in (None, "IUnknown"):
        return IUNKNOWN_METHODS
    return len(model.interfaces[base].methods)


def manifest(model):
    """[(id, "<Interface>::<Method>", slot)] for every declared method, then [(id, "::<Export>", -1)]."""
    rows, next_id = [], FIRST_ID
    for name, iface in model.interfaces.items():
        for slot in range(base_count(model, name), len(iface.methods)):
            rows.append((next_id, f"{name}::{iface.methods[slot].name}", slot))
            next_id += 1
    for e in model.exports:
        rows.append((next_id, f"::{e.name}", -1))
        next_id += 1
    return rows


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--headers", required=True)
    ap.add_argument("--rules", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("what", choices=["manifest"])
    a = ap.parse_args(argv)
    try:
        model = load(a.headers, a.rules)
    except ValueError as e:
        print(f"gen.py: {e}", file=sys.stderr)
        return 1
    os.makedirs(a.out, exist_ok=True)
    with open(os.path.join(a.out, "manifest.txt"), "w") as f:
        for row in manifest(model):
            f.write(f"{row[0]} {row[1]} {row[2]}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

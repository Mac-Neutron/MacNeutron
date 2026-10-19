#!/usr/bin/env python3
"""The Direct3D forwarding generator (spec §4.2; M2 plan Tasks 2 and 3).

Reads the widl-generated headers native DXMT compiles against (build/dxmt-native/install/include-native: d3d12.h,
d3dcommon.h, dxgi*.h), whose C vtables list every interface's methods in vtable order, and rules.txt: DXMT's private
interfaces (declared there in the headers' C prototype form) and the annotations each method needs. Writes, under
--out:
    manifest.txt   one line a forwarded method or export: "<id> <Interface>::<Method> <slot>" ("<id> ::<Export> -1"):
                   ids from 16 (0-15 are the family's own ops), declared methods only, header order, then the exports
    calls:         mnd3d_params.h (each method's params struct), calls.c (mn_d3d_calls[], by id, for the marshaller),
                   ifaces.c (the interfaces' IIDs and families), stubs.cpp (the host's calls into native DXMT),
                   layout.cpp (the guest's struct layouts checked against the native headers'), roundtrip-cases.c

    gen.py --headers <include-native> --rules <rules.txt> --out <dir> manifest|calls
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
HEADERS = ["directx/dxgiformat.h", "directx/dxgicommon.h", "directx/dxgitype.h", "d3d12/d3d12.h",
           "directx/d3dcommon.h", "directx/dxgi.h", "directx/dxgi1_2.h", "directx/dxgi1_3.h", "directx/dxgi1_4.h",
           "directx/dxgi1_5.h", "directx/dxgi1_6.h", "directx/dxgidebug.h",
           # M4: D3D11 and D3D10, after the others so that D3D12's and DXGI's interfaces keep their numbers
           "directx/d3d10.h", "directx/d3d10_1.h", "directx/d3d11.h", "directx/d3d11_1.h", "directx/d3d11_2.h",
           "directx/d3d11_3.h", "directx/d3d11_4.h"]
# The exports games call: d3d12.def's and dxgi.def's (DXMT's own lists).
EXPORTS = {"d3d12.dll": ["D3D12CreateDevice", "D3D12GetDebugInterface", "D3D12CreateRootSignatureDeserializer",
                         "D3D12CreateVersionedRootSignatureDeserializer", "D3D12SerializeRootSignature",
                         "D3D12SerializeVersionedRootSignature", "D3D12GetInterface"],
           "dxgi.dll": ["CreateDXGIFactory", "CreateDXGIFactory1", "CreateDXGIFactory2",
                        "DXGIDeclareAdapterRemovalSupport", "DXGIGetDebugInterface1"],
           # M4: d3d11.def's and d3d10core.def's (DXMT's, with Wine's D3D11CoreRegisterLayers)
           "d3d11.dll": ["D3D11CoreCreateDevice", "D3D11CoreRegisterLayers", "D3D11CreateDevice",
                         "D3D11CreateDeviceAndSwapChain", "D3D11On12CreateDevice"],
           "d3d10core.dll": ["D3D10CoreCreateDevice", "D3D10CoreRegisterLayers"]}


@dataclass
class Param:
    type: str
    name: str
    array: Optional[str] = None  # a fixed array's length ("const FLOAT color[4]": type 'const FLOAT *', "4")


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
    declared: bool = False  # rules.txt's (no header declares it, or not with its parameters' names)


@dataclass
class Model:
    interfaces: Dict[str, Interface] = field(default_factory=dict)
    structs: Dict[str, Struct] = field(default_factory=dict)
    enums: Dict[str, Dict[str, str]] = field(default_factory=dict)
    typedefs: Dict[str, str] = field(default_factory=dict)
    exports: List[Export] = field(default_factory=list)
    constants: Dict[str, int] = field(default_factory=dict)  # #define <NAME> <number>


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


# String typedefs (D3D11's ClassLinkage, counters, annotations): their pointers, so the defaults read them as strings.
STRING_TYPEDEFS = {"LPCSTR": "const char *", "LPCWSTR": "const WCHAR *", "LPSTR": "char *", "LPWSTR": "WCHAR *"}


def norm_type(t):
    """'const  D3D12_RANGE*' -> 'const D3D12_RANGE *', 'void **' -> 'void **', 'LPCSTR' -> 'const char *'."""
    stars = t.count("*")
    base = re.sub(r"\s+", " ", t.replace("*", " ")).strip()
    if base in STRING_TYPEDEFS:
        base, stars = STRING_TYPEDEFS[base][:-2], stars + 1
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
    if arr:
        return Param(norm_type(norm_type(typ) + " *"), name, arr.strip("[] ") or None)
    return Param(norm_type(typ), name)


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
        st = parse_struct_body(text[m.end():close], m.group(1) == "union", m.group(2))
        model.structs.setdefault(m.group(2), st)
        alias = re.match(r"\s*(\w+)\s*;", text[close + 1:])  # "typedef struct TAG { } NAME;": NAME as well
        if alias:
            model.structs.setdefault(alias.group(1), st)
    for m in re.finditer(r"typedef\s+enum\s+(\w+)\s*\{(.*?)\}\s*(\w+)\s*;", text, re.S):
        values = {}
        for item in split_top(m.group(2)):
            k, _, v = item.partition("=")
            if k.strip():
                values[k.strip()] = v.strip()
        model.enums.setdefault(m.group(1), values)
        model.enums.setdefault(m.group(3), values)
    for m in re.finditer(r"typedef\s+([\w\s\*]+?)\s+(\w+)\s*;", text):
        t = norm_type(m.group(1))
        if not t.startswith(("struct ", "union ", "enum ", "interface ")) and m.group(2) not in model.typedefs:
            model.typedefs[m.group(2)] = t


def read(path):
    with open(path) as f:
        return f.read()


def parse_header(path, header, model):
    text = strip_comments(read(path))
    for m in re.finditer(r"^#define\s+(\w+)\s+\(?\s*(\d+)\s*\)?\s*$", text, re.M):
        model.constants.setdefault(m.group(1), int(m.group(2)))
    iids = {m.group(1): guid_text(m.groups()[1:]) for m in GUID.finditer(text)}
    bases = {m.group(2): (m.group(3), m.group(1).lower()) for m in MIDL.finditer(text)}
    for m in VTBL.finditer(text):
        name = m.group(1)
        base, iid = bases.get(name, (None, None))
        model.interfaces[name] = Interface(name, iids.get(name, iid), base, parse_vmethods(m.group(2), name), header)
    parse_types(text, model)
    wanted = {n: dll for dll, names in EXPORTS.items() for n in names}
    for m in EXPORT.finditer(re.sub(r"^\s*#.*$", " ", text, flags=re.M)):  # not a preceding #endif
        if m.group(2) in wanted and m.group(2) not in [e.name for e in model.exports]:
            try:
                params = parse_params(m.group(3))
            except ValueError:  # d3d11.h's prototypes name no parameters: rules.txt declares those exports
                continue
            model.exports.append(Export(m.group(2), wanted[m.group(2)], norm_type(m.group(1)), params))


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
            model.exports.append(Export(m.group(3), m.group(1), norm_type(m.group(2)), parse_params(m.group(4)), True))


def inherited(model, base):
    """A private interface's vtable starts with its base's methods (IUnknown's 3)."""
    if base == "IUnknown":
        return [Method("QueryInterface", "HRESULT", [Param("REFIID", "riid"), Param("void **", "object")]),
                Method("AddRef", "ULONG", []), Method("Release", "ULONG", [])]
    return list(model.interfaces[base].methods)


# Interfaces another one's IID and vtable stand for (QueryInterface finds that one): D3D10's multithread interface is
# D3D11's (d3d10.h and d3d11_4.h, the same IID and methods).
SAME_AS = {"ID3D10Multithread": "ID3D11Multithread"}


def load(include_dir, rules_path):
    model = Model()
    for h in HEADERS:
        parse_header(os.path.join(include_dir, h), os.path.basename(h), model)
    for name, same in SAME_AS.items():
        if name in model.interfaces and model.interfaces[name].iid == model.interfaces[same].iid:
            del model.interfaces[name]
    iids = {}
    for n, i in model.interfaces.items():
        if i.iid and iids.setdefault(i.iid, n) != n:
            raise ValueError(f"{n} has {iids[i.iid]}'s IID: SAME_AS it?")
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


# ---- rules and classification (Task 3) ----

class RuleError(ValueError):
    pass


# The guest's ABI, x86-64 Windows: the host's (arm64 macOS) for every type here but WCHAR, 2 bytes in the guest.
SCALARS = {}
for _n in ("BYTE", "UINT8", "INT8", "CHAR", "char", "BOOLEAN", "UCHAR", "unsigned char"):
    SCALARS[_n] = (1, 1, "uint8_t")
for _n in ("WORD", "UINT16", "INT16", "SHORT", "USHORT", "WCHAR"):
    SCALARS[_n] = (2, 2, "uint16_t")
for _n in ("UINT", "UINT32", "ULONG", "DWORD", "unsigned int"):
    SCALARS[_n] = (4, 4, "uint32_t")
for _n in ("INT", "INT32", "LONG", "BOOL", "HRESULT", "int"):
    SCALARS[_n] = (4, 4, "int32_t")
for _n in ("FLOAT", "float"):
    SCALARS[_n] = (4, 4, "float")
for _n in ("UINT64", "ULONGLONG", "SIZE_T", "UINT_PTR", "ULONG_PTR", "HANDLE", "HWND", "HMONITOR", "HDC", "HMODULE",
           "HINSTANCE"):
    SCALARS[_n] = (8, 8, "uint64_t")
for _n in ("INT64", "LONGLONG", "INT_PTR", "LONG_PTR"):
    SCALARS[_n] = (8, 8, "int64_t")
SCALARS["double"] = (8, 8, "double")
# windows_base.h's structs (not a header the model reads).
WELL_KNOWN = {"GUID": (16, 4), "IID": (16, 4), "LUID": (8, 4), "RECT": (16, 4), "POINT": (8, 4), "SIZE": (8, 4),
              "LARGE_INTEGER": (8, 8), "PALETTEENTRY": (4, 1)}
REFS = ("REFIID", "REFGUID", "REFCLSID")
LOCAL_INTERFACES = ("IUnknown",)


def resolve(model, t):
    """A type's base name, const and pointers dropped, typedefs followed."""
    base = t.replace("const ", "").replace("*", "").strip()
    seen = set()
    while base in model.typedefs and base not in seen:
        seen.add(base)
        nxt = model.typedefs[base].replace("const ", "").replace("*", "").strip()
        if nxt == base or "*" in model.typedefs[base]:
            break
        base = nxt
    return base


def stars(t):
    return t.count("*")


def is_interface(model, base):
    return base in model.interfaces or base in LOCAL_INTERFACES


def eval_dim(model, text):
    total = 1
    for d in text.split("*"):
        d = d.strip()
        if re.match(r"^\d+$", d):
            total *= int(d)
        elif d in model.constants:
            total *= model.constants[d]
        else:
            raise RuleError(f"an array length that isn't a number: {d}")
    return total


def struct_layout(model, st):
    """(size, align, {field name: (offset, field)}) of a struct or union; inline members' fields are their own."""
    off, align, fields = 0, 1, {}
    unit = None  # a bitfield run: (offset, size, bits used)
    for f in st.fields:
        if f.bits is not None:
            sz, al = size_align(model, f.type)
            if unit and unit[1] == sz and unit[2] + f.bits <= sz * 8 and not st.union:
                unit = (unit[0], sz, unit[2] + f.bits)
                fields[f.name] = (unit[0], f)
                continue
            o = (off + al - 1) // al * al if not st.union else 0
            unit = (o, sz, f.bits)
            fields[f.name] = (o, f)
            off = max(off, o + sz) if not st.union else max(off, sz)
            align = max(align, al)
            continue
        unit = None
        if f.inline is not None:
            sz, al, _ = struct_layout(model, f.inline)
        else:
            sz, al = size_align(model, f.type)
        if f.array:
            sz *= eval_dim(model, f.array)
        if st.union:
            fields[f.name] = (0, f)
            off = max(off, sz)
        else:
            o = (off + al - 1) // al * al
            fields[f.name] = (o, f)
            off = o + sz
        align = max(align, al)
    size = (off + align - 1) // align * align
    return size, align, fields


def size_align(model, t):
    if stars(t):
        return 8, 8
    base = resolve(model, t)
    if base in SCALARS:
        return SCALARS[base][:2]
    if base in model.enums:
        return 4, 4
    if base in WELL_KNOWN:
        return WELL_KNOWN[base]
    if base in model.structs:
        size, align, _ = struct_layout(model, model.structs[base])
        return size, align
    raise RuleError(f"a type the model doesn't know: {base}")


def has_wchar(model, t, seen=None):
    """True if a type is or holds a WCHAR (2 bytes in the guest, 4 natively): such structs are specials."""
    seen = seen or set()
    if stars(t):
        return False
    base = resolve(model, t)
    if base == "WCHAR":
        return True
    st = model.structs.get(base)
    if not st or base in seen:
        return False
    seen.add(base)
    return any(has_wchar(model, f.type, seen) if f.inline is None else
               any(has_wchar(model, g.type, seen) for g in f.inline.fields) for f in st.fields)


@dataclass
class FieldSpec:
    kind: str  # WMF_*
    param: str  # the params struct's member (a method's argument), or a struct's field path
    offset: Optional[int] = None  # within a struct (nested lists); None: the params struct's member
    size: int = 0
    array: bool = False
    count: Optional[str] = None  # the count's member (params) or field path (struct)
    count_offset: Optional[int] = None
    count_width: int = 0
    deref: bool = False  # the count is read through a pointer (count=*<param>): an inout argument before this one
    one_if: Optional[str] = None  # the array is one element when this argument is nonzero (its own length member)
    nested: Optional["NestedSpec"] = None
    union: Optional["UnionSpec"] = None
    iface: Optional[str] = None  # a handle's interface ("riid": the call's REFIID names it)
    objects: Optional[str] = None  # an array of objects out (a getter's): their interface, 8 bytes each on the wire


@dataclass
class NestedSpec:
    name: str  # the element's type
    fields: List[FieldSpec]


@dataclass
class UnionSpec:
    name: str
    variants: List[tuple]  # [(value, NestedSpec or None)]


@dataclass
class ParamLayout:
    name: str
    ctype: str  # the params struct member's C type ("uint8_t name[56]" members: ctype "bytes", size, align)
    size: int
    align: int


@dataclass
class CallSpec:
    id: int
    name: str  # "<Interface>::<Method>" or "::<Export>"
    mode: str  # async, sync, submit, special, refuse
    fields: List[FieldSpec]
    params: List[ParamLayout]
    ret: str
    structs: set = field(default_factory=set)  # the structs its fields name (layout.cpp checks them)
    method: Optional[Method] = None
    arena: bool = False  # rules.txt's arena: its allocations from the guest's arenas, as a creation's (M4 Task 7)


@dataclass
class Rules:
    modes: Dict[str, str] = field(default_factory=dict)
    params: Dict[tuple, List[str]] = field(default_factory=dict)  # (method, param): words
    fields: Dict[tuple, List[str]] = field(default_factory=dict)  # (struct, field): words
    companions: List[tuple] = field(default_factory=list)  # (interface, companion): QueryInterface answers it too
    own: set = field(default_factory=set)  # generated tables, with guest and host code of our own (MN_D3D_ID_*)
    wrap: set = field(default_factory=set)  # guest code of our own (proxy.cpp) first, then the generated proxy (M3)
    arena: set = field(default_factory=set)  # calls whose allocations the guest's arenas serve (M4 Task 7)


MODES = ("async", "sync", "submit", "special", "refuse")


def parse_rules(text, model):
    """rules.txt's annotation lines (the declarations are parse_rules_declarations'):
        <Interface>::<Method> async|sync|submit|special|refuse
        <Interface>::* refuse                   every method the interface declares (one DXMT doesn't implement)
        <Interface>::<Method> <param> in|out|inout [count=<param>|size=<param>|bytes=<n>|one|string|wstring]
                                                [unit=<n>: a void array's element bytes, with count]
                                                [one_if=<param>: one element when that argument is nonzero]
        struct <S>.<field> in|out [count=<field>|size=<field>|bytes=<n>|one|string]
        struct <S>.<union field> union=<discriminant field> <Member>=<value> ...  (the members with pointers)
        companion <Interface> <Interface>       an object of the first answers the second (another C++ subobject)
        <Interface>::<Method> own                the generated params and tables, but guest and host code of our own
                                                (proxy.cpp's mn_d3d_special, the dispatcher's own stubs)
        <Interface>::<Method> arena              DXMT's buffers made in it come from the guest's arenas, as in a
                                                creation (a map's renames: mn_d3d_arena_call)
    count=*<param> and size=*<param> read the length through an inout pointer argument. ::<Export> names an export
    the same way. refuse: the guest answers E_NOTIMPL (nothing, for a void method), logged."""
    rules, depth = Rules(), 0
    for raw in text.splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        if line.startswith(("interface ",)) or (line.startswith("struct ") and "{" in line):
            depth += 1
            continue
        if depth:
            if line in ("end", "};") or line.startswith("}"):
                depth -= 1
            continue
        if line.startswith("export "):
            continue
        words = line.split()
        if words[0] == "companion" and len(words) == 3:
            rules.companions.append((words[1], words[2]))
            continue
        if words[0] == "struct":
            st, _, fname = words[1].partition(".")
            if not fname:
                raise RuleError(f"rules.txt: {line!r}: struct rules name a field")
            rules.fields[(st, fname)] = words[2:]
        elif "::" in words[0]:
            if len(words) == 2 and words[1] == "own":
                rules.own.add(words[0])
            elif len(words) == 2 and words[1] == "wrap":
                rules.wrap.add(words[0])
            elif len(words) == 2 and words[1] == "arena":
                rules.arena.add(words[0])
            elif len(words) == 2 and words[1] in MODES:
                rules.modes[words[0]] = words[1]
            elif len(words) >= 3:
                rules.params[(words[0], words[1])] = words[2:]
            else:
                raise RuleError(f"rules.txt: can't read {line!r}")
        else:
            raise RuleError(f"rules.txt: can't read {line!r}")
    return rules


def length_spec(words):
    """(direction, form, value) from a rule's words: form one of one, count, size, bytes, string, wstring."""
    if not words or words[0] not in ("in", "out", "inout"):
        raise RuleError(f"a rule wants in, out or inout first: {' '.join(words)}")
    form, value, unit, one_if = "one", None, 1, None
    for w in words[1:]:
        k, _, v = w.partition("=")
        if k in ("count", "size", "bytes"):
            form, value = k, v
        elif k == "unit":
            unit = int(v, 0)
        elif k == "one_if":
            one_if = v
        elif k in ("one", "string", "wstring", "wstruct"):
            form = k
        else:
            raise RuleError(f"a rule word the generator doesn't know: {w}")
    return words[0], form, value, unit, one_if


KIND = {"in": "WMF_IN", "out": "WMF_OUT", "inout": "WMF_INOUT"}


def nested_for(model, rules, base, where, seen=()):
    """The fields inside a struct a pointer leads to, as a nested list (offsets within it), or None when the bytes
    alone are enough. Interface pointers are handles; any other pointer needs a struct rule; a WCHAR is a special."""
    st = model.structs.get(base)
    if st is None:
        return None
    if base in seen:
        raise RuleError(f"{where}: {base} contains itself")
    if has_wchar(model, base):
        raise RuleError(f"{where}: {base} holds WCHAR (its guest and native sizes differ): a special")
    out = []
    _, _, layout = struct_layout(model, st)
    for fname, (off, f) in layout.items():
        rule = rules.fields.get((base, fname))
        if f.inline is not None and f.inline.union:
            spec = union_for(model, rules, base, fname, f, off, where, seen + (base,))
            if spec:
                out.append(FieldSpec("WMF_UNION", fname, off, union=spec[0], count_offset=spec[1], count_width=spec[2]))
            continue
        if f.inline is not None:  # a struct in place: its fields at their offsets
            out += shifted(nested_for_struct(model, rules, f.inline, base, where, seen + (base,)), off)
            continue
        if f.array and not stars(f.type):
            elem = resolve(model, f.type)
            if elem in model.structs and nested_for(model, rules, elem, where, seen + (base,)):
                raise RuleError(f"{where}: {base}.{fname} is an array of structs with pointers")
            continue
        if not stars(f.type):
            elem = resolve(model, f.type)
            inner = nested_for(model, rules, elem, where, seen + (base,)) if elem in model.structs else None
            out += shifted(inner.fields if inner else [], off)
            continue
        g = pointer_field(model, rules, f.type, fname, off, rule, where + f" {base}.{fname}", seen + (base,))
        if g.count:  # a count is a field of the same struct
            if g.count not in layout or g.deref:
                raise RuleError(f"{where}: {base}.{g.param}'s count {g.count} isn't a field of {base}")
            g.count_offset = layout[g.count][0]
            g.count_width = size_align(model, layout[g.count][1].type)[0]
        out.append(g)
    return NestedSpec(base, out) if out else None


def shifted(fields, off):
    """A struct-in-place's fields, at its offset in the outer struct (their counts and discriminants too)."""
    for g in fields:
        g.offset += off
        if g.count_offset is not None:
            g.count_offset += off
    return fields


def nested_for_struct(model, rules, st, owner, where, seen):
    tmp_name = f"{owner}.inline"
    model.structs[tmp_name] = st
    try:
        spec = nested_for(model, rules, tmp_name, where, seen)
    finally:
        del model.structs[tmp_name]
    return spec.fields if spec else []


def union_for(model, rules, owner, fname, f, off, where, seen):
    """A union with pointers in some variant: its rule names the discriminant and each member's value."""
    variants = []
    for g in f.inline.fields:
        inner = nested_for(model, rules, resolve(model, g.type), where, seen) if g.inline is None else None
        variants.append((g.name, inner))
    if not any(v for _, v in variants):
        return None
    words = rules.fields.get((owner, fname))
    if not words or not words[0].startswith("union="):
        raise RuleError(f"{where}: {owner}.{fname} is a union with pointers: it needs a union= rule")
    disc = words[0].split("=", 1)[1]
    values = dict(w.split("=", 1) for w in words[1:])
    _, _, layout = struct_layout(model, model.structs[owner])
    if disc not in layout:
        raise RuleError(f"{where}: {owner} has no field {disc}")
    dsize, _ = size_align(model, layout[disc][1].type)
    spec = UnionSpec(f"{owner}.{fname}", [])
    for name, inner in variants:
        if inner and name not in values:
            raise RuleError(f"{where}: {owner}.{fname}'s rule gives no value for {name}, which has pointers")
        for v in values.get(name, "").split(","):
            if v and inner:
                spec.variants.append((int(v, 0), inner))
    return spec, layout[disc][0], dsize


def pointer_field(model, rules, t, name, offset, rule, where, seen=()):
    """One pointer: a struct's field (offset set) or a call's argument (offset None)."""
    elem_t = t[: t.rindex("*")].strip()
    base = resolve(model, elem_t)
    if rule:
        direction, form, value, unit, one_if = length_spec(rule)
        if form == "string":
            return FieldSpec("WMF_STR_IN", name, offset)
        if form == "wstring":
            return FieldSpec("WMF_WSTR_IN", name, offset)
        if form == "wstruct":  # a struct whose WCHAR arrays the host converts (own code): the guest's size
            st = model.structs.get(base)
            wchars = sum(eval_dim(model, f.array) for f in (st.fields if st else []) if f.array and resolve(model, f.type) == "WCHAR")
            if not st or direction != "out" or not wchars:
                raise RuleError(f"{where}: wstruct is for a struct out with WCHAR arrays")
            size = f"({size_align(model, elem_t)[0]} + (MN_WM_GUEST_WCHAR - 2) * {wchars})"
            return FieldSpec("WMF_OUT", name, offset, size)
        if stars(elem_t) == 1 and is_interface(model, base) and direction == "out" and offset is None:
            # objects out (M4: D3D11's getters): each the first handle the host gave it, which the guest knows
            if form == "one":
                return FieldSpec("WMF_HANDLE_OUT", name, offset, iface=base)
            if form != "count" or value.startswith("*"):
                raise RuleError(f"{where}: objects out want count=<argument>")
            return FieldSpec("WMF_OUT", name, offset, 8, array=True, count=value, objects=base)
        if stars(elem_t) == 1 and is_interface(model, base):  # an array of interface pointers: a handle each
            esize, nested = 8, NestedSpec("handle", [FieldSpec("WMF_HANDLE_IN", "", 0, iface=base)])
        elif base == "void" or form in ("size", "bytes"):
            esize, nested = unit if form == "count" else 1, None
        else:
            esize, nested = size_align(model, elem_t)[0], nested_for(model, rules, base, where, seen)
        fs = FieldSpec(KIND[direction], name, offset, esize, nested=nested)
        if form in ("count", "size"):
            fs.array, fs.count, fs.deref = True, value.lstrip("*"), value.startswith("*")
            if one_if and offset is not None:
                raise RuleError(f"{where}: one_if is for a method's argument")
            fs.one_if = one_if
        elif form == "bytes":
            fs.size = int(value, 0)
        return fs
    if is_interface(model, base) and stars(t) == 1:
        return FieldSpec("WMF_HANDLE_IN", name, offset, iface=base)
    if stars(t) == 1 and t.startswith("const ") and base in ("char", "CHAR"):
        return FieldSpec("WMF_STR_IN", name, offset)
    if stars(t) == 1 and t.startswith("const ") and base == "WCHAR":
        return FieldSpec("WMF_WSTR_IN", name, offset)
    raise RuleError(f"{where}: the pointer {t} {name} needs a rule")


def count_like(method):
    return any(re.search(r"(count|^num|Num|_num|^n[A-Z])", p.name) and not stars(p.type) for p in method.params)


def classify_param(model, rules, mname, method, i, p):
    t, where = p.type, f"{mname} {p.name}"
    rule = rules.params.get((mname, p.name))
    base = resolve(model, t)
    if t in REFS:
        return FieldSpec("WMF_IN", p.name, size=16)
    if not stars(t):
        if base in model.structs and nested_for(model, rules, base, where):
            raise RuleError(f"{where}: {t} by value holds pointers")
        if has_wchar(model, t):
            raise RuleError(f"{where}: {t} by value holds WCHAR")
        return None
    if rule:
        return pointer_field(model, rules, t, p.name, None, rule, where)
    elem_t = t[: t.rindex("*")].strip()
    if is_interface(model, base):
        if stars(t) == 1:
            return FieldSpec("WMF_HANDLE_IN", p.name, iface=base)
        if stars(t) == 2 and " const" in t:  # T *const *: an array of them in
            raise RuleError(f"{where}: {t} is an array: it needs a count rule")
        if stars(t) == 2 and count_like(method):  # a getter's array of objects out, maybe (D3D11's)
            raise RuleError(f"{where}: {t} may be an array of objects out: it needs a rule (out count=<n>, or out one)")
        if stars(t) == 2:
            return FieldSpec("WMF_HANDLE_OUT", p.name, iface=base)
    if t == "void **" and i and method.params[i - 1].type in REFS:
        return FieldSpec("WMF_HANDLE_OUT", p.name, iface="riid")
    if stars(t) > 1:
        raise RuleError(f"{where}: {t} needs a rule")
    const = t.startswith("const ")
    if p.array:
        esize = size_align(model, elem_t)[0]
        return FieldSpec("WMF_IN" if const else "WMF_OUT", p.name, size=esize * eval_dim(model, p.array),
                         nested=nested_for(model, rules, base, where))
    if p.name == "__ret":
        return FieldSpec("WMF_OUT", p.name, size=size_align(model, elem_t)[0], nested=nested_for(model, rules, base,
                                                                                                 where))
    if base == "WCHAR" and const:
        return FieldSpec("WMF_WSTR_IN", p.name)
    if base in ("char", "CHAR") and const:
        return FieldSpec("WMF_STR_IN", p.name)
    if base == "void" or count_like(method) or (const and base not in model.structs):
        raise RuleError(f"{where}: the pointer {t} needs a rule (it may be an array, or its size another argument)")
    if has_wchar(model, base):
        raise RuleError(f"{where}: {base} holds WCHAR: a special")
    return FieldSpec("WMF_IN" if const else "WMF_OUT", p.name, size=size_align(model, elem_t)[0],
                     nested=nested_for(model, rules, base, where))


def param_layout(model, p):
    t = p.type
    if stars(t) or t in REFS or t.startswith("PFN_"):  # function pointers: the guest's, opaque
        return ParamLayout(p.name, "uint64_t", 8, 8)
    base = resolve(model, t)
    if base in SCALARS:
        size, align, ctype = SCALARS[base]
        return ParamLayout(p.name, ctype, size, align)
    if base in model.enums:
        return ParamLayout(p.name, "uint32_t", 4, 4)
    size, align = size_align(model, t)
    return ParamLayout(p.name, "bytes", size, align)


def default_mode(ret, fields):
    outs = any(f.kind in ("WMF_OUT", "WMF_INOUT", "WMF_HANDLE_OUT") for f in fields)
    if outs or ret not in ("void", "HRESULT"):
        return "sync"
    return "async" if ret == "void" else "sync"


def collect_structs(fields, out):
    for f in fields:
        if f.nested:
            out.add(f.nested.name)
            collect_structs(f.nested.fields, out)
        if f.union:
            for _, v in f.union.variants:
                if v:
                    out.add(v.name)
                    collect_structs(v.fields, out)


def classify(model, rules):
    """{id: CallSpec} for every method and export the manifest numbers; a RuleError lists every one that needs a
    rule."""
    calls, errors = {}, []
    methods = {}
    for name, iface in model.interfaces.items():
        for slot in range(base_count(model, name), len(iface.methods)):
            methods[f"{name}::{iface.methods[slot].name}"] = iface.methods[slot]
    for e in model.exports:
        methods[f"::{e.name}"] = Method(e.name, e.ret, e.params)
    for mid, mname, _ in manifest(model):
        method = methods[mname]
        mode = rules.modes.get(mname) or rules.modes.get(mname.split("::")[0] + "::*")
        fields = []
        # widl's aggregate returns hand back their __ret argument: those are values, not pointers.
        if mode not in ("special", "refuse") and stars(method.ret) and not any(p.name == "__ret" for p in method.params):
            errors.append(f"{mname}: it returns a pointer ({method.ret}): special or refuse")
            continue
        if mode not in ("special", "refuse"):
            try:
                for i, p in enumerate(method.params):
                    f = classify_param(model, rules, mname, method, i, p)
                    if f:
                        fields.append(f)
                byname = {p.name: p for p in method.params}
                for f in fields:
                    if f.kind == "WMF_OUT" and f.nested:  # the host would write its own pointers into them
                        raise RuleError(f"{mname} {f.param}: data out holds pointers or handles: a special")
                    if f.count in ("aux0", "aux1") and mname in rules.own and not f.deref:
                        f.count, f.count_width = f"aux[{f.count[3]}]", 8  # the own code's size (M4: packed data)
                        continue
                    if f.count:
                        if f.count not in byname:
                            raise RuleError(f"{mname} {f.param}: no argument {f.count}")
                        target = byname[f.count]
                        if f.one_if and f.one_if not in byname:
                            raise RuleError(f"{mname} {f.param}: no argument {f.one_if}")
                        if f.deref:
                            if not stars(target.type) or not any(g.param == f.count and g.kind == "WMF_INOUT"
                                                                 for g in fields):
                                raise RuleError(f"{mname} {f.param}: count=*{f.count} wants an inout pointer")
                            f.count_width = size_align(model, target.type[: target.type.rindex("*")])[0]
                        else:
                            f.count_width = param_layout(model, target).size
                # A count read through a pointer is the host's copy by the time its array is read: its field first.
                firsts = {f.count for f in fields if f.deref}
                fields.sort(key=lambda f: f.param not in firsts)
            except RuleError as e:
                errors.append(str(e))
                continue
        try:
            params = [param_layout(model, p) for p in method.params]
        except RuleError as e:
            errors.append(f"{mname}: {e}")
            continue
        spec = CallSpec(mid, mname, mode or default_mode(method.ret, fields), fields, params, method.ret,
                        method=method, arena=mname in rules.arena)
        collect_structs(fields, spec.structs)
        if mode not in ("special", "refuse"):  # every struct an argument passes or points to
            spec.structs |= {resolve(model, p.type) for p in method.params if resolve(model, p.type) in model.structs}
        calls[mid] = spec
    if errors:
        raise RuleError(f"{len(errors)} methods need rules:\n  " + "\n  ".join(errors))
    ifaces = {n.split("::")[0] for n in methods}
    for name in list(rules.modes) + [k[0] for k in rules.params] + sorted(rules.own | rules.wrap | rules.arena):
        if name.endswith("::*") and name[:-3] in ifaces:
            continue
        if name not in methods:
            raise RuleError(f"rules.txt names a method the headers don't have: {name}")
    return calls


# ---- the call tables (Task 3): calls.c, mnd3d_params.h, layout.cpp, roundtrip-cases.c ----

C_KEYWORDS = {"self", "ret", "auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else",
              "enum", "extern", "float", "for", "goto", "if", "inline", "int", "long", "register", "restrict",
              "return", "short", "signed", "sizeof", "static", "struct", "switch", "typedef", "union", "unsigned",
              "void", "volatile", "while", "class", "new", "delete", "this", "template", "private", "public",
              "operator", "friend", "virtual", "namespace", "bool", "true", "false"}
MODE = {"async": "MN_WM_QUEUED", "sync": "MN_WM_SYNC", "submit": "MN_WM_SUBMIT", "special": "MN_WM_SYNC",
        "refuse": "MN_WM_LOCAL"}
E_NOTIMPL = 0x80004001


def member(i, name):
    """A params struct member's name: the argument's, unless C or the struct's own (self, ret) take it."""
    if not name:
        return f"a{i}"
    return name + "_" if name in C_KEYWORDS else name


class Tables:
    """The nested lists and unions every call shares (1-based indexes, as calls.h's), deduplicated."""

    def __init__(self):
        self.nested, self.unions, self.keys = [], [], {}

    def nest(self, spec):
        rows = [self.row(f, None) for f in spec.fields]
        key = ("n", tuple(rows))
        if key not in self.keys:
            self.nested.append((spec.name, rows))
            self.keys[key] = len(self.nested)
        return self.keys[key]

    def union(self, spec):
        variants = [(v, self.nest(n)) for v, n in spec.variants]
        key = ("u", tuple(variants))
        if key not in self.keys:
            self.unions.append((spec.name, variants))
            self.keys[key] = len(self.unions)
        return self.keys[key]

    def row(self, f, params):
        """One struct mn_wm_field initializer. params: the call's params struct (a top-level field), or None."""
        off = f"offsetof(struct {params}, {f.param})" if params else str(f.offset)
        hkind = "MN_HK_D3D" if f.kind in ("WMF_HANDLE_IN", "WMF_HANDLE_OUT") else "0"
        flags = " | ".join(x for x, on in (("WMF_ARRAY", f.array), ("WMF_DEREF", f.deref)) if on) or "0"
        if f.count:
            coff = f"offsetof(struct {params}, {f.count})" if params else str(f.count_offset)
        else:
            coff = str(f.count_offset or 0)
        nested = 0
        if f.union:
            nested = self.union(f.union)
        elif f.nested:
            nested = self.nest(f.nested)
        if nested > 255:
            raise RuleError("more than 255 nested lists or unions: calls.h's nested index is 8 bits")
        return (f"{{{off}, {f.kind}, {hkind}, 0, {flags}, {f.size}, {coff}, {f.count_width}, {nested}}}")


def emit_params(calls, own=()):
    out = ["/* Generated by eac-vm/d3d/gen.py from the native headers and rules.txt: don't edit. Each forwarded",
           " * method's params, in wire types (pointers and handles 64-bit, structs by value as their guest bytes):",
           " * the same layout for the guest's x86-64 code and the host's arm64. */",
           "#ifndef MN_D3D_PARAMS_H", "#define MN_D3D_PARAMS_H", "#include <stdint.h>",
           "#ifdef __cplusplus", "#define MN_D3D_ALIGNAS(n) alignas(n)", "#else",
           "#define MN_D3D_ALIGNAS(n) _Alignas(n)", "#endif", "",
           f"#define MN_D3D_FIRST {FIRST_ID}", f"#define MN_D3D_CALLS {max(calls) + 1}", ""]
    for mid in sorted(calls):
        c = calls[mid]
        out.append(f"struct mn_d3d_p_{mid} {{ /* {c.name} */")
        out.append("    uint64_t self;")
        out.append("    uint64_t ret;")
        for i, p in enumerate(c.params):
            name = member(i, p.name)
            if p.ctype == "bytes":
                out.append(f"    MN_D3D_ALIGNAS({p.align}) uint8_t {name}[{p.size}];")
            else:
                out.append(f"    {p.ctype} {name};")
        rename = {p.name: member(i, p.name) for i, p in enumerate(c.params)}
        for f in c.fields:
            if f.one_if:  # the array's own length: the method's count, or one (the proxy's)
                out.append(f"    uint32_t {rename[f.param]}_n;")
            if f.kind == "WMF_HANDLE_OUT":  # the reply's: the interfaces the object answers, and which of them it
                out.append(f"    uint64_t {rename[f.param]}_ifaces[9];  /* the interfaces it answers (words 0-3, bits by"
                           " index), those at another pointer (4-7), and the handle the host gave it before (8; 0: none) */")
        if c.name in own:
            out.append("    uint64_t aux[2]; /* own code's (rules.txt's own: a fence's mirror slot, ...) */")
        out.append("};")
    out += ["", "#endif", ""]
    return "\n".join(out)


def emit_calls(calls):
    t = Tables()
    per_call = {}
    for mid in sorted(calls):
        c = calls[mid]
        rename = {p.name: member(i, p.name) for i, p in enumerate(c.params)}
        rows = []
        if not c.name.startswith("::"):  # the object called: a handle like any other
            rows.append(t.row(FieldSpec("WMF_HANDLE_IN", "self"), f"mn_d3d_p_{mid}"))
        for f in c.fields:
            g = FieldSpec(**{**f.__dict__, "param": rename[f.param], "count": rename.get(f.count, f.count)})
            if f.one_if:
                g.count, g.count_width = rename[f.param] + "_n", 4
            rows.append(t.row(g, f"mn_d3d_p_{mid}"))
        per_call[mid] = rows
    out = ["/* Generated by eac-vm/d3d/gen.py from the native headers and rules.txt: don't edit. The forwarded",
           " * methods and exports, by id (manifest.txt), for eac-vm/metal/wm's marshaller. */",
           "#include <stddef.h>", '#include "calls.h"', '#include "mnd3d_params.h"', ""]
    for i, (name, rows) in enumerate(t.nested, 1):
        out.append(f"static const struct mn_wm_field n{i}[] = {{ /* {name} */")
        out += [f"    {r}," for r in rows]
        out.append("};")
    out.append("static const struct mn_wm_nested nested[] = {")
    out += [f"    {{{len(rows)}, n{i}}}," for i, (_, rows) in enumerate(t.nested, 1)]
    out.append("};")
    for i, (name, variants) in enumerate(t.unions, 1):
        out.append(f"static const uint32_t u{i}_values[] = {{{', '.join(str(v) for v, _ in variants)}}}; /* {name} */")
        out.append(f"static const uint8_t u{i}_nested[] = {{{', '.join(str(n) for _, n in variants)}}};")
    out.append("static const struct mn_wm_union unions[] = {")
    out += [f"    {{{len(v)}, u{i}_values, u{i}_nested}}," for i, (_, v) in enumerate(t.unions, 1)]
    if not t.unions:
        out.append("    {0, NULL, NULL},")
    out.append("};")
    out.append("static const struct mn_wm_tables tables = {nested, unions};")
    for mid in sorted(calls):
        if per_call[mid]:
            out.append(f"static const struct mn_wm_field f{mid}[] = {{")
            out += [f"    {r}," for r in per_call[mid]]
            out.append("};")
    out.append("const struct mn_wm_call mn_d3d_calls[MN_D3D_CALLS] = {")
    for mid in sorted(calls):
        c = calls[mid]
        status = f"0x{E_NOTIMPL:08x}u" if c.mode == "refuse" and c.ret == "HRESULT" else "0"
        sp = "MN_SP_D3D" if c.mode == "special" else "MN_SP_NONE"
        fields = f"f{mid}" if per_call[mid] else "NULL"
        out.append(f'    [{mid}] = {{"{c.name}", sizeof(struct mn_d3d_p_{mid}), {MODE[c.mode]}, {sp}, '
                   f"{len(per_call[mid])}, {status}, {fields}, &tables}},")
    out += ["};", "const uint8_t mn_d3d_arena_call[MN_D3D_CALLS] = {"]
    out += [f"    [{mid}] = 1," for mid in sorted(calls) if calls[mid].arena] or ["    0,"]
    out += ["};", ""]
    return "\n".join(out)


def struct_access(fname, f):
    """The C++ member name offsetof takes for a field: an anonymous union's or struct's first member."""
    if f.inline is not None and f.name in ("UNIONNAME", "DUMMYUNIONNAME", "DUMMYSTRUCTNAME", "STRUCTNAME"):
        return f.inline.fields[0].name
    return fname


def emit_layout(model, calls):
    """layout.cpp: compiled with the native headers, it checks the guest layout the generator computed (x86-64
    Windows) against the host's own, for every struct a call's data holds."""
    names = set()
    for c in calls.values():
        names |= c.structs
    out = ["// Generated by eac-vm/d3d/gen.py: don't edit. The guest's struct layouts (x86-64 Windows, as gen.py computes",
           "// them) against the host's (the native headers): data crosses the boundary as bytes.",
           "#include <cstddef>", "#include <d3d12.h>", "#include <dxgi1_6.h>", "#include <d3d10_1.h>",
           "#include <d3d11_4.h>", ""]
    for n in sorted(names):
        if n not in model.structs or "." in n or n.startswith("MTL_"):
            continue
        size, _, layout = struct_layout(model, model.structs[n])
        if has_wchar(model, n):  # a guest WCHAR is 2 bytes, the host's 4: the host converts (rules.txt's wstruct)
            continue
        out.append(f'static_assert(sizeof({n}) == {size}, "{n}");')
        for fname, (off, f) in layout.items():
            if f.bits is not None:
                continue
            out.append(f'static_assert(offsetof({n}, {struct_access(fname, f)}) == {off}, "{n}.{fname}");')
    out += ["int main() { return 0; }", ""]
    return "\n".join(out)


def emit_roundtrip(calls):
    ids = [mid for mid in sorted(calls) if calls[mid].mode in ("async", "sync", "submit")]
    out = ["/* Generated by eac-vm/d3d/gen.py: don't edit. The calls eac-vm/d3d/tests/roundtrip.c packs and unpacks. */",
           '#include "mnd3d_params.h"', f"const unsigned mn_d3d_roundtrip_count = {len(ids)};",
           "const unsigned mn_d3d_roundtrip_ids[] = {"]
    for i in range(0, len(ids), 16):
        out.append("    " + ", ".join(str(x) for x in ids[i:i + 16]) + ",")
    out += ["};", ""]
    return "\n".join(out)


def iid_bytes(text):
    """A GUID's 16 bytes in memory (Data1-3 little-endian), from its text form."""
    h = text.replace("-", "")
    d1, d2, d3 = int(h[0:8], 16), int(h[8:12], 16), int(h[12:16], 16)
    return list(d1.to_bytes(4, "little")) + list(d2.to_bytes(2, "little")) + list(d3.to_bytes(2, "little")) + \
        [int(h[i:i + 2], 16) for i in range(16, 32, 2)]


def iface_index(model):
    names = [n for n, i in model.interfaces.items() if i.iid]
    if len(names) > 256:
        raise RuleError(f"{len(names)} interfaces: the answers' masks have 256 bits (MN_D3D_MASK_WORDS)")
    return {n: k for k, n in enumerate(names)}


def families(model, rules):
    """Each interface's family: what an object created as it may answer (its bases, the interfaces derived from it,
    and their companions), which the host asks the object at its creation. An interface DXMT doesn't implement
    (`<I>::* refuse`) is never asked: DXMT logs every QueryInterface it can't answer."""
    index = iface_index(model)
    absent = {k[:-3] for k, m in rules.modes.items() if k.endswith("::*") and m == "refuse"}

    def chain(n):
        out = []
        while n in model.interfaces:
            out.append(n)
            n = model.interfaces[n].base
        return out
    for a, b in rules.companions:
        if a not in index or b not in index:
            raise RuleError(f"companion {a} {b}: {a if a not in index else b} isn't an interface of the model")
    fam = {}
    for n in index:
        members = set(chain(n)) | {m for m in index if n in chain(m)}
        for a, b in rules.companions:
            if a in members:
                members |= set(chain(b))
        fam[n] = sorted(index[m] for m in members if m in index and m not in absent)
    return index, fam


def emit_ifaces(model, rules):
    index, fam = families(model, rules)
    out = ["/* Generated by eac-vm/d3d/gen.py: don't edit. The interfaces by index (the answers' bits), their IIDs and",
           " * families. */", "#include <string.h>", '#include "mnd3d_params.h"', ""]
    for n, k in index.items():
        out.append(f"static const uint8_t fam{k}[] = {{{', '.join(str(x) for x in fam[n])}}};")
    out.append("const struct mn_d3d_iface mn_d3d_ifaces[MN_D3D_IFACES] = {")
    for n, k in index.items():
        iid = ", ".join(f"0x{b:02x}" for b in iid_bytes(model.interfaces[n].iid))
        out.append(f'    {{"{n}", {{{iid}}}, {len(fam[n])}, fam{k}, {index.get(model.interfaces[n].base, -1)}}},')
    out += ["};", "", "int mn_d3d_iface_of(const void *iid) {", "    for (int i = 0; i < MN_D3D_IFACES; i++)",
            "        if (!memcmp(mn_d3d_ifaces[i].iid, iid, 16)) return i;", "    return -1;", "}", ""]
    return "\n".join(out)


def iface_decls(model):
    return ["", f"#define MN_D3D_IFACES {len(iface_index(model))}", f"#define MN_D3D_EXPORTS {len(model.exports)}",
            "/* base: the interface it derives from, or -1 (IUnknown, or a base that isn't forwarded) */",
            "struct mn_d3d_iface { const char *name; uint8_t iid[16]; uint16_t nfamily; const uint8_t *family; int16_t base; };",
            "extern const struct mn_d3d_iface mn_d3d_ifaces[MN_D3D_IFACES];",
            "/* 1: the call's buffers come from the guest's arenas (rules.txt's arena) */",
            "extern const uint8_t mn_d3d_arena_call[MN_D3D_CALLS];",
            "/* The interface an IID names, or -1. */",
            "int mn_d3d_iface_of(const void *iid);", ""]


RECORDERS = re.compile(r"ID3D12GraphicsCommandList|ID3D11DeviceContext|ID3D10Device1?$|IMTLD3D12CommandListExt")

HANDLE_TYPES = ("HANDLE", "HWND", "HMONITOR", "HDC", "HMODULE", "HINSTANCE")


def stub_arg(model, i, p, c):
    name = f"a->{member(i, p.name)}"
    t = p.type
    if any(f.param == p.name and f.objects for f in c.fields):  # DXMT writes a getter's objects into host memory
        return f"any_ptr{{(uintptr_t)mn_o_{member(i, p.name)}}}"
    if t in REFS:
        return f"*(const GUID *)(uintptr_t){name}"
    out = next((f for f in c.fields if f.param == p.name and f.kind == "WMF_HANDLE_OUT"), None)
    if out:
        return f"any_ptr{{(uintptr_t)&{name}}}"
    if not stars(t) and resolve(model, t) == "HANDLE":  # a guest's handle (an event, mostly) means nothing natively:
        return "nullptr"                                # none (own code relays an event: host/native.cpp)
    if stars(t) or t.startswith("PFN_") or resolve(model, t) in HANDLE_TYPES:
        return f"any_ptr{{{name}}}"
    base = resolve(model, t)
    if base in model.enums:
        return f"({t}){name}"
    if param_layout(model, p).ctype == "bytes":
        return f"val<{t}>({name})"
    return name


def emit_stubs(model, rules, calls):
    index = iface_index(model)
    exports = [e for e in model.exports]
    out = ["// Generated by eac-vm/d3d/gen.py: don't edit. Each forwarded method and export on native DXMT: the params",
           "// the marshaller translated (handles to native pointers, data to host copies) passed to the method of the",
           "// declaring interface. Compiled with DXMT's own -DWIDL_EXPLICIT_AGGREGATE_RETURNS: a struct return is its",
           "// __ret argument, as the guest's C vtables have it.",
           '#include "stubs.h"', ""]
    for e in exports:
        if e.declared:  # rules.txt's prototype: a header declares it without its parameters' names, or not at all
            params = ", ".join(f"{p.type} {p.name}" for p in e.params) or "void"
            out.append(f'extern "C" {e.ret} WINAPI {e.name}({params});')
    out.append("extern \"C\" const struct mn_d3d_export mn_d3d_export_names[MN_D3D_EXPORTS] = {")
    for e in exports:
        out.append(f'    {{"{e.dll.replace(".dll", "")}", "{e.name}"}},')
    out += ["};", "extern \"C\" {", "void *mn_d3d_exports[MN_D3D_EXPORTS]; // mn_d3d_load's", "}", ""]
    ids = {}
    for mid in sorted(calls):
        c = calls[mid]
        if c.mode in ("special", "refuse"):
            continue
        iface, _, mname = c.name.partition("::")
        args = ", ".join(stub_arg(model, i, p, c) for i, p in enumerate(c.method.params))
        if iface:
            call = f"((::{iface} *)(uintptr_t)a->self)->{mname}({args})"
        else:
            k = [e.name for e in exports].index(mname)
            call = f"((decltype(&::{mname}))mn_d3d_exports[{k}])({args})"
        body = [f'extern "C" NTSTATUS_T mn_d3d_stub_{mid}(void *params) {{ // {c.name}',
                f"    auto *a = (struct mn_d3d_p_{mid} *)params;"]
        rename = {p.name: member(i, p.name) for i, p in enumerate(c.method.params)}
        for f in c.fields:  # a getter's objects, DXMT's to write here: never the guest's memory (M4's final review)
            if f.objects:
                m, n = rename[f.param], rename[f.count]
                body.append(f"    if (a->{n} > MN_D3D_MAX_OBJECTS) return 0; // past any slot range: nothing")
                body.append(f"    void *mn_o_{m}[MN_D3D_MAX_OBJECTS] = {{}};")
        body.append("    try {")  # natively, DXMT's IMPLEMENT_ME throws (fork 0084): the call fails, the VM host goes on
        if c.ret == "void":
            body.append(f"    {call};")
        else:
            body.append(f"    auto r = {call};")
            body.append("    static_assert(sizeof r <= 8);")
            body.append("    std::memcpy(&a->ret, &r, sizeof r);")
        made = [f for f in c.fields if f.kind == "WMF_HANDLE_OUT"]
        for f in made:
            m = rename[f.param]
            if f.iface == "riid":
                riid = next(c.method.params[j - 1].name for j, p in enumerate(c.method.params) if p.name == f.param)
                which = f"mn_d3d_iface_of((const void *)(uintptr_t)a->{rename[riid]})"
            else:
                which = str(index.get(f.iface, -1))
            ok = "SUCCEEDED(r) && " if c.ret == "HRESULT" else ""
            body.append(f"    if ({ok}a->{m}) mn_d3d_answers((void *)(uintptr_t)a->{m}, {which}, a->{m}_ifaces);")
        for f in c.fields:
            if f.objects:  # a getter's objects: the handles the guest knows them by, DXMT's references let go
                body.append(f"    mn_d3d_objects_out(mn_o_{rename[f.param]}, a->{rename[f.count]}, "
                            f"(uint64_t *)(uintptr_t)a->{rename[f.param]});")
        if c.ret == "HRESULT":
            body.append("    mn_d3d_result(r);")
        ret = "&a->ret" if c.ret == "HRESULT" else "nullptr"
        body += [f"    }} catch (...) {{ return mn_d3d_not_in_dxmt({mid}, {ret}); }}", "    return 0;", "}"]
        out += body
        ids[mid] = True
    out.append("extern \"C\" NTSTATUS_T (*mn_d3d_stubs[MN_D3D_CALLS])(void *) = {")
    for mid in range(max(calls) + 1):  # by id; NULL: none (an op of the family's own, a special, a refusal)
        out.append(f"    mn_d3d_stub_{mid}," if mid in ids else "    nullptr,")
    out += ["};", ""]
    return "\n".join(out)


def declaring(model, iface):
    """The interface that declares each slot of iface's vtable (IUnknown's three: None)."""
    chain = []
    n = iface
    while n in model.interfaces:
        chain.insert(0, n)
        n = model.interfaces[n].base
    out = [None] * IUNKNOWN_METHODS
    for y in chain:
        out += [y] * (len(model.interfaces[y].methods) - len(out))
    return out


def proxy_decl(model, c):
    """A proxy function's parameters (the C vtable's, This aside), named as the params struct's members."""
    out = []
    for i, p in enumerate(c.method.params):
        t = p.type
        name = member(i, p.name)
        out.append(f"{t} {name}" if not t.endswith("*") else f"{t}{name}")
    return out


def proxy_fill(model, c):
    lines = []
    for i, p in enumerate(c.method.params):
        m = member(i, p.name)
        t = p.type
        base = resolve(model, t)
        if t in REFS:
            lines.append(f"    mn_p.{m} = (uintptr_t)&{m};")
        elif stars(t) or t.startswith("PFN_") or base in HANDLE_TYPES:
            lines.append(f"    mn_p.{m} = (uintptr_t){m};")
        elif param_layout(model, p).ctype == "bytes":
            lines.append(f"    std::memcpy(mn_p.{m}, &{m}, sizeof mn_p.{m});")
        elif base in model.enums:
            lines.append(f"    mn_p.{m} = (uint32_t){m};")
        else:
            lines.append(f"    mn_p.{m} = {m};")
    return lines


def emit_proxies(model, rules, calls):
    """The guest's proxies (proxies.cpp): a function per forwarded method and export, filling its params and calling
    mn_d3d_call; the vtables, by interface index, built at load (a special's slot is guest code, mn_d3d_special)."""
    index = iface_index(model)
    by_name = {c.name: mid for mid, c in calls.items()}
    out = ["// Generated by eac-vm/d3d/gen.py: don't edit. The guest's proxies: each forwarded method fills its params",
           "// (mnd3d_params.h) and calls mn_d3d_call; a refused one answers E_NOTIMPL; the vtables by interface index.",
           '#include "proxy.h"', ""]
    for mid in sorted(calls):
        c = calls[mid]
        iface, _, mname = c.name.partition("::")
        params = proxy_decl(model, c)
        ret = c.ret
        if iface:
            sig = f"static {ret} STDMETHODCALLTYPE px_{mid}({', '.join(['mn_proxy *This'] + params)})"
        elif c.name in rules.wrap:  # guest/proxy.cpp's export calls it
            sig = f'extern "C" {ret} WINAPI mn_px_{mname}({", ".join(params) or "void"})'
        else:
            sig = f'extern "C" {ret} WINAPI {mname}({", ".join(params) or "void"})'
        body = [sig + " {", f"    // {c.name}"]
        if c.mode == "refuse" or c.mode == "special":
            body.append(f'    mn_d3d_unhandled({mid}, "{c.name}", {"1" if c.mode == "refuse" else "0"});')
            if ret == "HRESULT":
                body.append("    return E_NOTIMPL;")
            elif ret != "void":
                body.append(f"    return ({ret})0;")
            body.append("}")
            out += body
            continue
        body.append(f"    struct mn_d3d_p_{mid} mn_p = {{}};")
        if iface:
            body.append("    mn_p.self = (uintptr_t)This;")
        body += proxy_fill(model, c)
        made = [f for f in c.fields if f.kind == "WMF_HANDLE_OUT"]
        rename = {p.name: member(i, p.name) for i, p in enumerate(c.method.params)}
        for f in c.fields:
            if f.one_if:
                m = rename[f.param]
                body.append(f"    mn_p.{m}_n = {rename[f.one_if]} ? 1 : (uint32_t){rename[f.count]};")
            if f.objects:  # a getter's objects come back as the handles the guest knows them by
                m = rename[f.param]
                body.append(f"    std::vector<uint64_t> mn_o_{m}({rename[f.count]});")
                body.append(f"    mn_p.{m} = (uintptr_t)mn_o_{m}.data();")
        has_ret = any(p.name == "__ret" for p in c.method.params)
        # a command list's, a D3D11 context's or a D3D10 device's: in order when handed between threads (guest/proxy.cpp)
        call = f"mn_d3d_record(This, {mid}, &mn_p)" if RECORDERS.match(iface) else f"mn_d3d_call({mid}, &mn_p)"
        if (ret == "void" or (has_ret and ret != "HRESULT")) and not made:  # the status tells the caller nothing
            body.append(f"    {call};")
        else:
            body.append(f"    int32_t mn_s = {call};")
        if ret == "HRESULT":
            body.append("    HRESULT mn_hr = mn_s ? mn_d3d_hr(mn_s) : (HRESULT)(uint32_t)mn_p.ret;")
        for f in made:
            m = rename[f.param]
            if f.iface == "riid":
                riid = next(c.method.params[j - 1] for j, p in enumerate(c.method.params) if p.name == f.param)
                which, rp = "-1", f"&{rename[riid.name]}"
            else:
                which, rp = str(index.get(f.iface, -1)), "nullptr"
            ok = "SUCCEEDED(mn_hr) && " if ret == "HRESULT" else "!mn_s && "
            body.append(f"    if ({m}) *(void **){m} = {ok}mn_p.{m} ? mn_proxy_out(mn_p.{m}, {which}, {rp}, "
                        f"mn_p.{m}_ifaces) : nullptr;")
        for f in c.fields:
            if f.objects:
                m = rename[f.param]
                body.append(f"    if ({m})")
                body.append(f"        for (size_t mn_i = 0; mn_i < mn_o_{m}.size(); mn_i++)")
                body.append(f"            {m}[mn_i] = ({f.objects} *)mn_proxy_known(mn_o_{m}[mn_i], {index.get(f.objects, -1)});")
        if ret == "HRESULT":
            body.append("    return mn_hr;")
        elif any(p.name == "__ret" for p in c.method.params):  # the caller's own struct, not the host's pointer
            body.append(f"    return {member(next(i for i, p in enumerate(c.method.params) if p.name == '__ret'), '__ret')};")
        elif ret != "void":
            body += [f"    {ret} mn_r{{}};", "    if (!mn_s) std::memcpy(&mn_r, &mn_p.ret, sizeof mn_r);",
                     "    return mn_r;"]
        body.append("}")
        out += body
    # vtables
    out.append("")
    out.append("const void *mn_d3d_vtables[MN_D3D_IFACES][MN_D3D_MAX_SLOTS];")
    out.append("static void build(void) {")
    for n, k in index.items():
        decl = declaring(model, n)
        for slot, y in enumerate(decl):
            if y is None:
                fn = ["(const void *)mn_px_QueryInterface", "(const void *)mn_px_AddRef",
                      "(const void *)mn_px_Release"][slot]
            else:
                mid = by_name[f"{y}::{model.interfaces[n].methods[slot].name}"]
                fn = f"(const void *)px_{mid}"
                if calls[mid].mode == "special" or calls[mid].name in rules.own | rules.wrap:  # proxy.cpp's code
                    fn = f'mn_d3d_special("{calls[mid].name}", {fn})'
            out.append(f"    mn_d3d_vtables[{k}][{slot}] = {fn};")
    out += ["}", "static int built = (build(), 1);", ""]
    maxslots = max(len(model.interfaces[n].methods) for n in index)
    return "\n".join(out), maxslots


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--headers", required=True)
    ap.add_argument("--rules", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("what", choices=["manifest", "calls"])
    a = ap.parse_args(argv)
    try:
        model = load(a.headers, a.rules)
        files = {"manifest.txt": "".join(f"{r[0]} {r[1]} {r[2]}\n" for r in manifest(model))}
        if a.what == "calls":
            calls = classify(model, parse_rules(read(a.rules), model))
            rules = parse_rules(read(a.rules), model)
            rules = parse_rules(read(a.rules), model)
            params = emit_params(calls, rules.own)
            tail = "\n".join(iface_decls(model))
            proxies, maxslots = emit_proxies(model, rules, calls)
            tail += f"\n#define MN_D3D_MAX_SLOTS {maxslots}\n"
            for mid in sorted(calls):  # the methods with code of our own (a wrap's may fill the params itself), by name
                if calls[mid].name in rules.own | rules.wrap:
                    c_name = calls[mid].name.replace("::", "_")
                    tail += f"#define MN_D3D_ID_{c_name} {mid}\ntypedef struct mn_d3d_p_{mid} mn_d3d_p_{c_name};\n"
            head, _, end = params.rpartition("\n#endif\n")  # the header's own guard, its last
            params = head + "\n" + tail + "\n#endif\n" + end
            files = {"mnd3d_params.h": params, "calls.c": emit_calls(calls), "ifaces.c": emit_ifaces(model, rules),
                     "stubs.cpp": emit_stubs(model, rules, calls), "proxies.cpp": proxies,
                     "layout.cpp": emit_layout(model, calls), "roundtrip-cases.c": emit_roundtrip(calls)}
    except ValueError as e:  # RuleError too
        print(f"gen.py: {e}", file=sys.stderr)
        return 1
    os.makedirs(a.out, exist_ok=True)
    for name, text in files.items():
        with open(os.path.join(a.out, name), "w") as f:
            f.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

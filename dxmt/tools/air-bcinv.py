#!/usr/bin/env python3
"""air-bcinv.py [--allow FILE] <ours.bc> <ref15.bc> | --dump-allow: the AIR writer's record inventory (DXMTRS step 4W,
P9b). Reads both files' bitstreams (an optional Darwin wrapper is skipped) and fails, naming each cause, when
  - a (block, record code) of ours is not in the allowlist;
  - a STRUCT_NAME value of ours is not in the reference's (this catches dxilOpaquePtrReservedName and a shadow struct
    written twice, which LLVM 15 renames "<name>.0");
  - an attribute kind (PARAMATTR_GRP_CODE_ENTRY, enum, integer and type attributes) is above 84, LLVM 15's last;
  - there is an OPAQUE_POINTER record (TYPE_BLOCK_ID_NEW code 25).
Prints "ok" and exits 0, or one "inv: <cause>" line per failure and exits 1 (2 on a file it cannot parse).

The allowlist is written once, below, as "<block> <code> <name>" lines: the pairs
L15/include/llvm/Bitcode/LLVMBitCodes.h defines, intersected with what the writer emits (every bitc:: record code in
AIRBitcodeWriter.cpp and AIRValueEnumerator.cpp, plus Task 3's rows: the identification block,
MODULE_CODE_SOURCE_FILENAME and _HASH, CST_CODE_POISON, FUNC_CODE_INST_FREEZE and _UNOP, the sync scope names block's
SYNC_SCOPE_NAME). LLVM 15's reader skips unknown module-level records silently and LLVM 23's passes add records the C++
output lacks, so the reference module is not the list. A writer change that emits a new code fails here, naming it,
until the line is added. --allow FILE reads the list from FILE instead (the --dump-allow format), for the harness's
mutation checks."""
import sys

ALLOW = """
0 1 BLOCKINFO_CODE_SETBID
8 1 MODULE_CODE_VERSION
8 2 MODULE_CODE_TRIPLE
8 3 MODULE_CODE_DATALAYOUT
8 5 MODULE_CODE_SECTIONNAME
8 7 MODULE_CODE_GLOBALVAR
8 8 MODULE_CODE_FUNCTION
8 9 MODULE_CODE_ALIAS_OLD
8 11 MODULE_CODE_GCNAME
8 12 MODULE_CODE_COMDAT
8 16 MODULE_CODE_SOURCE_FILENAME
8 17 MODULE_CODE_HASH
9 2 PARAMATTR_CODE_ENTRY
10 3 PARAMATTR_GRP_CODE_ENTRY
11 1 CST_CODE_SETTYPE
11 2 CST_CODE_NULL
11 3 CST_CODE_UNDEF
11 4 CST_CODE_INTEGER
11 5 CST_CODE_WIDE_INTEGER
11 6 CST_CODE_FLOAT
11 7 CST_CODE_AGGREGATE
11 8 CST_CODE_STRING
11 9 CST_CODE_CSTRING
11 10 CST_CODE_CE_BINOP
11 11 CST_CODE_CE_CAST
11 12 CST_CODE_CE_GEP
11 13 CST_CODE_CE_SELECT
11 14 CST_CODE_CE_EXTRACTELT
11 15 CST_CODE_CE_INSERTELT
11 16 CST_CODE_CE_SHUFFLEVEC
11 19 CST_CODE_CE_SHUFVEC_EX
11 20 CST_CODE_CE_INBOUNDS_GEP
11 21 CST_CODE_BLOCKADDRESS
11 22 CST_CODE_DATA
11 26 CST_CODE_POISON
11 30 CST_CODE_INLINEASM
12 1 FUNC_CODE_DECLAREBLOCKS
12 2 FUNC_CODE_INST_BINOP
12 3 FUNC_CODE_INST_CAST
12 6 FUNC_CODE_INST_EXTRACTELT
12 7 FUNC_CODE_INST_INSERTELT
12 8 FUNC_CODE_INST_SHUFFLEVEC
12 10 FUNC_CODE_INST_RET
12 11 FUNC_CODE_INST_BR
12 12 FUNC_CODE_INST_SWITCH
12 13 FUNC_CODE_INST_INVOKE
12 15 FUNC_CODE_INST_UNREACHABLE
12 16 FUNC_CODE_INST_PHI
12 19 FUNC_CODE_INST_ALLOCA
12 20 FUNC_CODE_INST_LOAD
12 23 FUNC_CODE_INST_VAARG
12 26 FUNC_CODE_INST_EXTRACTVAL
12 27 FUNC_CODE_INST_INSERTVAL
12 28 FUNC_CODE_INST_CMP2
12 29 FUNC_CODE_INST_VSELECT
12 31 FUNC_CODE_INST_INDIRECTBR
12 34 FUNC_CODE_INST_CALL
12 36 FUNC_CODE_INST_FENCE
12 39 FUNC_CODE_INST_RESUME
12 41 FUNC_CODE_INST_LOADATOMIC
12 43 FUNC_CODE_INST_GEP
12 44 FUNC_CODE_INST_STORE
12 45 FUNC_CODE_INST_STOREATOMIC
12 46 FUNC_CODE_INST_CMPXCHG
12 47 FUNC_CODE_INST_LANDINGPAD
12 56 FUNC_CODE_INST_UNOP
12 58 FUNC_CODE_INST_FREEZE
12 59 FUNC_CODE_INST_ATOMICRMW
13 1 IDENTIFICATION_CODE_STRING
13 2 IDENTIFICATION_CODE_EPOCH
14 1 VST_CODE_ENTRY
14 2 VST_CODE_BBENTRY
15 1 METADATA_STRING_OLD
15 2 METADATA_VALUE
15 3 METADATA_NODE
15 4 METADATA_NAME
15 5 METADATA_DISTINCT_NODE
15 6 METADATA_KIND
15 10 METADATA_NAMED_NODE
16 11 METADATA_ATTACHMENT
17 1 TYPE_CODE_NUMENTRY
17 2 TYPE_CODE_VOID
17 3 TYPE_CODE_FLOAT
17 4 TYPE_CODE_DOUBLE
17 5 TYPE_CODE_LABEL
17 6 TYPE_CODE_OPAQUE
17 7 TYPE_CODE_INTEGER
17 8 TYPE_CODE_POINTER
17 10 TYPE_CODE_HALF
17 11 TYPE_CODE_ARRAY
17 12 TYPE_CODE_VECTOR
17 13 TYPE_CODE_X86_FP80
17 14 TYPE_CODE_FP128
17 15 TYPE_CODE_PPC_FP128
17 16 TYPE_CODE_METADATA
17 18 TYPE_CODE_STRUCT_ANON
17 19 TYPE_CODE_STRUCT_NAME
17 20 TYPE_CODE_STRUCT_NAMED
17 21 TYPE_CODE_FUNCTION
26 1 SYNC_SCOPE_NAME
"""

BLOCKS = {0: "BLOCKINFO", 8: "MODULE", 9: "PARAMATTR", 10: "PARAMATTR_GROUP", 11: "CONSTANTS", 12: "FUNCTION",
          13: "IDENTIFICATION", 14: "VALUE_SYMTAB", 15: "METADATA", 16: "METADATA_ATTACHMENT", 17: "TYPE_NEW",
          18: "USELIST", 19: "MODULE_STRTAB", 20: "GLOBALVAL_SUMMARY", 21: "OPERAND_BUNDLE_TAGS",
          22: "METADATA_KIND", 23: "STRTAB", 24: "FULL_LTO_GLOBALVAL_SUMMARY", 25: "SYMTAB", 26: "SYNC_SCOPE_NAMES"}
TYPE_BLOCK, TYPE_STRUCT_NAME, TYPE_OPAQUE_POINTER, GROUP_BLOCK, GROUP_ENTRY, LAST_KIND = 17, 19, 25, 10, 3, 84
CHAR6 = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._"


class BadStream(Exception):
    pass


class Stream:
    """LLVM's bitstream: abbreviation ids, blocks, BLOCKINFO, abbreviated and unabbreviated records."""

    def __init__(self, data):
        if len(data) >= 20 and int.from_bytes(data[:4], "little") == 0x0B17C0DE:  # the Darwin wrapper
            off, size = int.from_bytes(data[8:12], "little"), int.from_bytes(data[12:16], "little")
            data = data[off:off + size]
        if data[:4] != b"BC\xc0\xde":
            raise BadStream("not bitcode (no 'BC' 0xC0DE magic)")
        self.d, self.n, self.p = data, len(data) * 8, 32
        self.pairs, self.names, self.kinds = {}, [], []
        self.info = {}

    def read(self, w):
        if w == 0:
            return 0
        if self.p + w > self.n:
            raise BadStream("truncated stream")
        b = self.p >> 3
        v = (int.from_bytes(self.d[b:b + 9], "little") >> (self.p & 7)) & ((1 << w) - 1)
        self.p += w
        return v

    def vbr(self, w):
        hi, v, shift = 1 << (w - 1), 0, 0
        while True:
            c = self.read(w)
            v |= (c & (hi - 1)) << shift
            if not c & hi:
                return v
            shift += w - 1
            if shift > 70:
                raise BadStream("vbr too long")

    def align(self):
        self.p = (self.p + 31) & ~31

    def element(self, op):
        k = op[0]
        if k == "fixed":
            return self.read(op[1])
        if k == "vbr":
            return self.vbr(op[1])
        if k == "char6":
            return ord(CHAR6[self.read(6)])
        raise BadStream("bad array element")

    def abbrev(self):
        ops = []
        for _ in range(self.vbr(5)):
            if self.read(1):
                ops.append(("lit", self.vbr(8)))
                continue
            enc = self.read(3)
            if enc in (1, 2):
                ops.append(("fixed" if enc == 1 else "vbr", self.vbr(5)))
            elif enc in (3, 4, 5):
                ops.append((("array",), ("char6",), ("blob",))[enc - 3])
            else:
                raise BadStream("bad abbreviation encoding")
        return ops

    def record(self, block, code, ops):
        self.pairs[(block, code)] = self.pairs.get((block, code), 0) + 1
        if block == TYPE_BLOCK and code == TYPE_STRUCT_NAME:
            self.names.append("".join(chr(c) for c in ops))
        elif block == GROUP_BLOCK and code == GROUP_ENTRY:
            i = 2  # grpid, paramidx, then the attributes
            while i < len(ops):
                k = ops[i]
                if k in (0, 1, 5, 6):
                    if i + 1 >= len(ops):
                        raise BadStream("short attribute group")
                    self.kinds.append(ops[i + 1])
                    i += 2 + (k in (1, 6))
                elif k in (3, 4):
                    i += 1
                    for _ in range(1 if k == 3 else 2):
                        while i < len(ops) and ops[i] != 0:
                            i += 1
                        i += 1
                else:
                    raise BadStream("bad attribute encoding %d" % k)

    def block(self, bid, width, depth=0):
        abbrevs, cur = list(self.info.get(bid, [])), None
        while True:
            a = self.read(width)
            if a == 0:
                self.align()
                return
            if a == 1:
                nid, nw = self.vbr(8), self.vbr(4)
                self.align()
                words = self.read(32)
                if depth > 8:
                    raise BadStream("blocks nested too deep")
                self.sub(nid, nw, words, depth + 1)
            elif a == 2:
                ab = self.abbrev()
                if bid == 0:
                    if cur is None:
                        raise BadStream("DEFINE_ABBREV before SETBID")
                    self.info.setdefault(cur, []).append(ab)
                else:
                    abbrevs.append(ab)
            elif a == 3:
                code, n = self.vbr(6), self.vbr(6)
                ops = [self.vbr(6) for _ in range(n)]
                self.record(bid, code, ops)
                if bid == 0 and code == 1 and ops:
                    cur = ops[0]
            else:
                if a - 4 >= len(abbrevs):
                    raise BadStream("unknown abbreviation %d" % a)
                ab, vals, i = abbrevs[a - 4], [], 0
                while i < len(ab):
                    op = ab[i]
                    if op[0] == "lit":
                        vals.append(op[1])
                    elif op[0] in ("fixed", "vbr", "char6"):
                        vals.append(self.element(op))
                    elif op[0] == "array":
                        if i + 1 >= len(ab):
                            raise BadStream("array without an element type")
                        for _ in range(self.vbr(6)):
                            vals.append(self.element(ab[i + 1]))
                        i += 1
                    else:  # blob
                        n = self.vbr(6)
                        self.align()
                        b = self.p >> 3
                        vals.extend(self.d[b:b + n])
                        self.p += 8 * n
                        self.align()
                    i += 1
                if not vals:
                    raise BadStream("record with no code")
                self.record(bid, vals[0], vals[1:])
                if bid == 0 and vals[0] == 1 and len(vals) > 1:
                    cur = vals[1]

    def sub(self, bid, width, words, depth=0):
        start = self.p
        self.block(bid, width, depth)
        if self.p != start + 32 * words:
            raise BadStream("block %d: length word says %d words, the records end after %d" % (bid, words, (self.p - start) // 32))

    def parse(self):
        while self.n - self.p >= 32:
            if self.read(2) != 1:
                if self.d[self.p >> 3:].strip(b"\0"):
                    raise BadStream("top level: expected a block")
                return
            bid, w = self.vbr(8), self.vbr(4)
            self.align()
            self.sub(bid, w, self.read(32))
        return self


def load_allow(text):
    allow = {}
    for line in text.splitlines():
        f = line.split()
        if f:
            allow[(int(f[0]), int(f[1]))] = f[2]
    return allow


def main(argv):
    allow_text = ALLOW
    if argv[:1] == ["--dump-allow"]:
        sys.stdout.write(ALLOW.lstrip("\n"))
        return 0
    if argv[:1] == ["--allow"] and len(argv) > 1:
        allow_text, argv = open(argv[1]).read(), argv[2:]
    if len(argv) != 2:
        print("usage: air-bcinv.py [--allow FILE] <ours.bc> <ref15.bc> | --dump-allow", file=sys.stderr)
        return 2
    try:
        ours = Stream(open(argv[0], "rb").read()).parse()
        ref = Stream(open(argv[1], "rb").read()).parse()
    except (BadStream, OSError) as e:
        print("inv: cannot read: %s" % e, file=sys.stderr)
        return 2
    allow, bad = load_allow(allow_text), []
    for (b, c), n in sorted(ours.pairs.items()):
        if (b, c) not in allow:
            bad.append("record code %d in block %d (%s) is not in the allowlist (%d records)" % (c, b, BLOCKS.get(b, "?"), n))
    known = set(ref.names)
    for name in sorted(set(ours.names) - known):
        bad.append("STRUCT_NAME '%s' is not in the reference" % name)
    for k in sorted(set(ours.kinds)):
        if k > LAST_KIND:
            bad.append("attribute kind %d is above %d" % (k, LAST_KIND))
    if ours.pairs.get((TYPE_BLOCK, TYPE_OPAQUE_POINTER)):
        bad.append("OPAQUE_POINTER (TYPE_BLOCK_ID_NEW code 25) x%d" % ours.pairs[(TYPE_BLOCK, TYPE_OPAQUE_POINTER)])
    for line in bad:
        print("inv: " + line)
    if not bad:
        print("ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

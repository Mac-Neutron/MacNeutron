#!/bin/sh
# air-oracle.sh <leg> <corpus>        legs: prep full defs opt23 probe det
# air-oracle.sh check <out-dir> <corpus>
# air-oracle.sh det-compare <dir-a> <dir-b>     (det's comparison alone, for two folders)
# air-oracle.sh norm <in.ll> <out.ll>            (text23n's input-side token deletion alone; prints flags= attrs=)
# air-oracle.sh text23n <pre.ll> <out.bc> <work-stem> <flags-dropped> <attrs-dropped>   (the text23n row alone)
#
# DXMTRS step 4W's oracle harness: the C++ translator's LLVM 15 metallibs are the reference. Each is dumped to typed
# text and a type table by air15, read with LLVM 23, written again by llvm-air-write (the fork's tool, which calls
# LLVMWriteAIRBitcode), read back with LLVM 15 and compared, and Metal builds every pipeline from the result.
#
#   prep    air15 extract, dis (.ll15) and types (.types); LLVM 23's llvm-dis of the .bc, line 1 dropped (.ll23).
#           Into $W/t/<corpus>/. Every other leg runs it first for the modules that lack it (or whose input is newer).
#   full    the whole table (llvm-air-write --types --emit-ll), air15 pack, then the rows below. text15 and text23 must
#           be identical to the reference's, up to the modules whose `c` count did not match (posmis), which the defs
#           leg grades instead (ruling D2, below).
#   defs    only the type-level table 4b's translator can supply: --named-only, --ref on the helper bitcode, and the
#           f, g and s lines of every name the helpers define or declare removed from the .types copy, so the refs
#           alone type them. A second writer run with the full f/g/s lines and the refs, output discarded, must
#           exit 0 (P16's conflict check). Adds text23n.
#   opt23   as defs, with --passes P15 in the tool (opt refuses air64). stale entries are counted and listed.
#   probe   dxil-probe-15 against dxil-probe-23 on the corpus's .dxil files (and reject/*.dxil): the same output and
#           exit status per file; each helper .air through LLVM 23's llvm-dis.
#   det     full, defs and opt23 twice in separate processes, and the tool's --batch -j 8 against -j 1; compares only
#           *.bc, *.metallib, *.ll15, *.ll23 and the stats lines (no times). smite2 runs its first 500 sorted names.
#   check   grades metallibs another producer wrote under the corpus's names (4b's translator): verify, inventory
#           (the baseline's module of the same name is the reference), wrapper, and the Metal rows. The writer's stats
#           are read from a `stats ...` line in <out-dir>/<stem>.log if the producer keeps one (informational), else 0.
#
# Rows (a module counts when the row holds for it; "-" in the summary: the leg does not grade it):
#   writer    the tool exited 0 (check: the producer's file exists)
#   self      llvm-dis (23) of the output equals the tool's .post.ll (the module as the writer left it: casts inserted,
#             drops applied), line 1 dropped on both, block labels' "; preds =" comments (use-list order) left out.
#             Every leg.
#   text15    air15 dis of the output equals the reference's.            full
#             Ruling D1: a module equal only once every zero index of a constant getelementptr reads i64 0 counts, and
#             is listed in $O/gep0.txt (LLVM 23 folds the C++'s getelementptr inbounds (T, T* @g, i64 0, i64 0) to @g,
#             the writer's cast of @g to its first element's pointer is a CE bitcast, and LLVM 15's reader folds that
#             back into a GEP with i32 0 indices; a path 4b never takes). Metal must still build such a module (the
#             Metal row).
#   text23    llvm-dis (23) of both, line 1 dropped, equal.                full
#   text23n   the output and the writer's input (.pre.ll) both through `opt -passes=instsimplify` (-mtriple override,
#             used for this comparison only), attribute references stripped, equal once the drop-list tokens are
#             deleted from the input side; the deleted tokens number flags-dropped, and attrs-dropped equals the
#             drop-list entries in the input's `attributes #` lines (norm_py below holds the lists).  defs, opt23
#             The writer's llvm.scmp/ucmp lowering (ruling D4) is folded back on the output side, and their
#             declarations deleted from the input side (post_py below). The writer's bit-test rewrite (ruling D5) is
#             applied to the input side too (norm_py).
#   verify    air15 verify (LLVM 15 typed parse, verifier, the atomics and llvm.* checks) says ok.
#   inv       air-bcinv.py (P9b) says ok.
#   wrapper   magic, version, offset and CPU type of the Darwin wrapper equal the reference's. (helpers: informational)
#   posmis    modules whose tool run printed `positional mismatch`: LLVM 23's reader changed the instruction count
#             (it turns constant expressions LLVM 23 no longer has into instructions). Ruling D2: each is graded by a
#             sliced defs run instead (ONLY=$O/posmis.names, into $O/posmis), its result per module in $O/posmis.txt.
#   upgraded  ruling D6: modules whose llvm.* declaration names differ between the reference's typed text (.ll15) and
#             LLVM 23's reading of it (.ll23): LLVM 23's reader upgraded their intrinsics on the way in (buffers.dxil's
#             llvm.lifetime.*.p0i8: declarations moved, call-site attributes dropped, a dead cast left). Their full
#             text rows and opt23 text23n are not graded; they join D2's sliced defs run, listed in $O/upgraded.txt.
# After the modules, the Metal row (not for helpers; not for a sliced run):
#   dxil-translate <SRC> <args> --use <folder of our metallibs> is run once. Before it, the number of metallibs in the
#   folder must equal the input's (it substitutes a file only where one exists, so a missing one would pass on the
#   C++ AIR). After it, every `ok` line's air= is recomputed over our files for that shader in translation order
#   (<f>.metallib, <f>.1.metallib, ...; FNV-1a 64): used=<matched>/<ok lines>. The ok set and fail reasons are compared
#   with the baseline $W/in/<corpus>.txt (corpus-diff.py of this folder, plus the sorted `fail` lines, reasons=eq|ne).
#   The baseline is the run on the C++ metallibs; it is made once, here, if missing:
#   `XLATE <SRC> <args> --use <input folder>`. At the gate remove it so XLATE (G's base tool) makes it again.
#
# Summary line (stdout):
#   air-oracle <leg> <corpus>: n=<n> writer=<ok> self=<ok> text15=<eq|-> text23=<eq|-> text23n=<ok|-> posmis=<p>
#   verify=<ok> inv=<ok> wrapper=<ok> used=<k>/<s> metal=<ok>/<total> base=<ok>/<total> newfail=<k> reasons=<eq|ne>
#   casts=<i>/<c> usetyped=<u> dropped=<a>/<f> refs=<r> stale=<t>
# (every count is a number of modules, "-" when the leg does not grade the row; casts, usetyped, dropped, refs and
# stale are the tool's stats summed.) Details go to stderr and to $O/failures.txt, one line per module and row; the
# `stale <name>` lines of opt23 to $O/stale.txt. Exit 0 only when, for the rows the leg grades (ROWS):
#   writer = self = verify = inv = wrapper = n; used = s (and the Metal row ran); newfail = 0; reasons = eq;
#   every posmis and upgraded module passes its defs run (D2, D6); full: text15 = text23 = n - (posmis or upgraded);
#   defs: text23n = n; opt23: text23n = n - upgraded.
#
# Environment (dev-only; none is read by the library or the tool):
#   BUILD_DIR   the build folder (default: the main checkout's, found through git)
#   W           $BUILD_DIR/step4/w        S   $BUILD_DIR/step4
#   AIRW        $W/llvm23-build/bin/llvm-air-write, the integration build of P12
#   BIN23       the LLVM 23 bin folder (default $I23/bin; $I23 itself, default $BUILD_DIR/phase0/llvm-23, is the prefix)
#   XLATE       $W/tools/dxil-translate     A15   $W/bin/air15 (dxil-probe-15 and -23 sit beside it)
#   SRC_<corpus>  the shader folder, for --use (a '-' in the corpus name is '_' here); defaults in the table
#   Dev knobs: GATE=1 (inputs from $S/base/<C>.out; smite2 also gets --pipelines), JOBS (parallel modules, default 4),
#   ONLY=<file listing input file names> (a slice: no Metal row, output in <leg>/<corpus>.only), ROWS (override the
#   leg's graded rows), REFS (typed-ref bitcode for defs and opt23; default $W/in/helpers/*.air; empty: none),
#   OUT_ROOT ($W/out), NOMETAL=1 (skip the Metal row).
#
# Corpora (4a's ids and Corpora table):
#   name        4a id    input, development / gate                        XLATE arguments
#   shaders     C1       $W/in/shaders / $S/base/C1.out                   -
#   dxil        C2       $W/in/dxil / $S/base/C2.out                      -
#   dxbc        C4       $W/in/dxbc (310 only) / $S/base/C4.out           -
#   dxbc-320    C4-320   - / $S/base/C4-320.out                           --metal 320
#   d3d11       C5       - / $S/base/C5.out (G3)                          -
#   d3d11-320   C5-320   - / $S/base/C5-320.out                           --metal 320
#   dx11        C6       - / $S/base/C6.out (G4's dxmt/tests/dxbc-corpus)  -
#   dx11-320    C6-320   - / $S/base/C6-320.out                           --metal 320
#   smite2      C8       $W/in/smite2 (plain --out) / $S/base/C8.out (G6)  gate: --pipelines <capture>/pipelines.txt
#   helpers     -        $W/in/helpers (bare .air: no pack, no Metal row)  -
# C3 and C7 make no metallibs. SRC defaults: dxmt/tests/{shaders,dxil,dxbc,dxbc-corpus} beside this script, and for
# smite2 the maintainer-allowed capture, ~/Library/Application Support/MacNeutron/vm.0.7.2/runs/2437170/traces/dxmt,
# read in place and read-only (4-D7). SRC_d3d11 has no default.
set -u

self=$0
case $self in /*) ;; *) self=$(pwd)/$self ;; esac
here=$(cd "$(dirname "$self")" && pwd)

die() {
  echo "air-oracle: $*" >&2
  exit 2
}

# P15, from A/airconv_context.cpp:110-151 minus the two custom passes. The shim builds InstCombinePass() (no fixpoint
# verification, L23/include/llvm/Transforms/InstCombine/InstCombine.h:33), while a textual `instcombine` verifies the
# fixpoint and aborts the process when one more iteration would change the function (L23/lib/Passes/PassBuilder.cpp,
# parseInstCombineOptions), hence `instcombine<no-verify-fixpoint>` (ruling R7-5).
P15='annotation2metadata,forceattrs,inferattrs,function<eager-inv>(lower-expect,simplifycfg,sroa<modify-cfg>,early-cse),always-inline,ipsccp,globalopt,function(mem2reg),deadargelim,function<eager-inv>(instcombine<no-verify-fixpoint>,simplifycfg<switch-range-to-icmp>)'

# text23n's input side: delete the drop-list tokens (Task 3's flag rows) and count them, count the drop-list attribute
# entries in the `attributes #` lines (P8). Usage: norm_py <in.ll> <out.ll>; prints "flags=<n> attrs=<n>".
#   DROP23: LLVM 23-only attribute kinds Task 3 drops (guarantee-only hints: its ruling R3-3, the writer's
#   GuaranteeOnly). Any other 23-only kind is a writer error.
#   captures(...) other than none, and memory(...) with no exact LLVM 15 form, are dropped or weakened and counted.
norm_py() {
  python3 -I - "$@" << 'PY'
import re, sys
DROP23 = {"nofpclass", "range", "writable", "dead_on_unwind", "initializes", "dead_on_return", "nocreateundeforpoison"}
text = open(sys.argv[1]).read()
flags = 0
def strip(pattern, drop):
    global text
    def sub(m):
        global flags
        flags += len(re.findall(drop, m.group(2)))
        return m.group(1) + re.sub(drop, "", m.group(2))
    text = re.sub(pattern, sub, text)
strip(r"(\btrunc)((?: nuw| nsw)+)", r" (?:nuw|nsw)")
strip(r"(\bgetelementptr)((?: inbounds| nusw| nuw| inrange\([^)]*\))+)", r" (?:nusw|nuw|inrange\([^)]*\))")
# FMF on a cast (LLVM 23 prints them before nneg): LLVM 15's CAST record has none (Task 3's ruling R3-4). "fast" is one
# token, as the writer counts it (R3-1).
strip(r"(= (?:fptrunc|fpext|uitofp|sitofp))((?: (?:fast|nnan|ninf|nsz|arcp|contract|afn|reassoc))+)",
      r" (?:fast|nnan|ninf|nsz|arcp|contract|afn|reassoc)")
strip(r"(\b(?:zext|uitofp))( nneg)", r" nneg")
strip(r"(= or)( disjoint)", r" disjoint")
strip(r"(\bicmp)( samesign)", r" samesign")
def exact(args):
    # memory(...): per-location ModRef; an unlabeled item is the default for every location not listed, and with
    # none every unlisted location is "none" (memory(argmem: read) touches nothing else).
    loc = {}
    default = "none"
    for item in (a.strip() for a in args.split(",")):
        if ":" in item:
            k, v = (x.strip() for x in item.split(":"))
            loc[k] = v
        elif item:
            default = item
    a = loc.get("argmem", default)
    i = loc.get("inaccessiblemem", default)
    o = default
    others = [v for k, v in loc.items() if k not in ("argmem", "inaccessiblemem")]
    if any(v != o for v in others):
        return False
    if a == i == o:
        return True
    return o == "none" and (a == "none" or i == "none" or a == i)
# Groups referenced outside the llvm.scmp/ucmp lines, which the writer removes (ruling D4). LLVM prints only groups
# something references, so for any other module this counts every group.
used = set()
for line in text.split("\n"):
    if not line.startswith("attributes #") and not re.search(r"@llvm\.[su]cmp\.", line):
        used.update(re.findall(r"#(\d+)\b", line))
attrs = 0
for m in re.finditer(r"^attributes #(\d+) = \{(.*)\}\s*$", text, re.M):
    if m.group(1) not in used:
        continue
    for tok in re.findall(r'"[^"]*"(?:="[^"]*")?|[A-Za-z_][\w.-]*(?:\((?:[^()]|\([^()]*\))*\))?', m.group(2)):
        kind, _, rest = tok.partition("(")
        if kind in DROP23:
            attrs += 1
        elif kind == "captures" and rest.rstrip(")") != "none":
            attrs += 1
        elif kind == "memory" and not exact(rest.rstrip(")")):
            attrs += 1
# Inline drop-list attributes (P8): LLVM 23's IPSCCP puts range(...) on a function's return (4a's tessellation corpora:
# dxmt.get_domain_patch_index), printed on its define line. Deleted here; each distinct line head (the text up to the
# function name) counts its tokens once, as the writer counts a group once (R3-1). An intrinsic declaration's are
# counted and kept: LLVM 23's reader sets them again from its table (R3-2); llvm.[su]cmp's declaration is skipped, the
# writer removes it before it counts (ruling D4, R7-6).
# ponytail: define/declare return positions only, the one place any corpus has them; a drop-list attribute on a
# parameter or a call site stays in the text and fails the row ("text differs"), and wants handling here then.
DROPTOK = re.compile(r"(?<= )(?:(?:range|nofpclass|initializes)\((?:[^()]|\([^()]*\))*\)|"
                     r"(?:writable|dead_on_unwind|dead_on_return|nocreateundeforpoison)\b) ?")
heads = set()
def ret_drop(m):
    global attrs
    if m.group(2).startswith(("@llvm.scmp.", "@llvm.ucmp.")):
        return m.group(0)
    found = DROPTOK.findall(m.group(1))
    if found and m.group(1) not in heads:
        heads.add(m.group(1))
        attrs += len(found)
    return m.group(0) if m.group(2).startswith("@llvm.") else DROPTOK.sub("", m.group(1)) + m.group(2)
text = re.sub(r"^((?:define|declare) [^@\n]*)(@.*)$", ret_drop, text, flags=re.M)
# Ruling D5: the writer writes each constant-index extractelement of `bitcast <N x iW> %v to <K x i1>` or of
# `load <K x i1>` as LLVM 15's bit test (legalizeBitTests: the word's extractelement, and with the bit, icmp ne 0; the
# load loads <K/32 x i32>; the bitcast goes). The same rewrite here, with the two new values unnamed as the writer
# leaves them: the function's numbered values are scaled by 1000 to make room (LLParser takes increasing numbers with
# gaps, and opt prints them renumbered). A module with numbered types is left alone (its %N could be a type).
NAME = r'%(?:[-\w.$]+|"[^"]*")'
LOAD = re.compile(r'^  (%s) = load <(\d+) x i1>, (.*)$' % NAME)
CAST = re.compile(r'^  (%s) = bitcast <(\d+) x i(\d+)> (%s) to <\d+ x i1>$' % (NAME, NAME))
EXTRACT = re.compile(r'^  (%s) = extractelement <\d+ x i1> (%s), (i\d+) (\d+)$' % (NAME, NAME))
NUM = re.compile(r'%(\d+)(?![-\w.$])')
DEF = re.compile(r'^(?:  %(\d+) = |(\d+):)')
def bit_tests(body):
    words = {}
    for l in body:
        m = LOAD.match(l)
        if m and int(m.group(2)) % 32 == 0:
            words[m.group(1)] = (int(m.group(2)) // 32, 32, m.group(1))
        m = CAST.match(l)
        if m:
            words[m.group(1)] = (int(m.group(2)), int(m.group(3)), m.group(4))
    if not words:
        return body
    defs = {int(m.group(1) or m.group(2)) for m in map(DEF.match, body[1:]) if m}
    scale = lambda t: NUM.sub(lambda m: "%%%d" % (int(m.group(1)) * 1000) if int(m.group(1)) in defs else m.group(0), t)
    args = [int(n) for n in NUM.findall(body[0])]
    last = max(args, default=-1) + (0 if re.match(r'^\S+:', body[1]) else 1)
    out = [body[0]]
    for l in body[1:]:
        m = LOAD.match(l)
        if m and m.group(1) in words:
            l = l.replace("load <%s x i1>," % m.group(2), "load <%d x i32>," % words[m.group(1)][0], 1)
        if CAST.match(l) and CAST.match(l).group(1) in words:
            continue
        m = EXTRACT.match(l)
        if m and m.group(2) in words:
            n, w, v = words[m.group(2)]
            k = int(m.group(4))
            bit = 1 << (k % w)
            bit -= (1 << w) if bit >> (w - 1) else 0
            out.append("  %%%d = extractelement <%d x i%d> %s, %s %d" % (last + 1, n, w, scale(v), m.group(3), k // w))
            out.append("  %%%d = and i%d %%%d, %d" % (last + 2, w, last + 1, bit))
            last += 2
            l = "  %s = icmp ne i%d %%%d, 0" % (m.group(1), w, last)
        d = DEF.match(l)
        if d:
            last = int(d.group(1) or d.group(2)) * 1000
            if d.group(2):
                l = "%d:%s" % (last, l[d.end():])
        out.append(scale(l))
    return out
if not re.search(r'^%\d+ = type ', text, re.M):
    lines, done, i = text.split("\n"), [], 0
    while i < len(lines):
        if lines[i].startswith("define ") and lines[i].endswith("{"):
            j = lines.index("}", i)
            done += bit_tests(lines[i:j + 1])
            i = j + 1
        else:
            done.append(lines[i])
            i += 1
    text = "\n".join(done)
open(sys.argv[2], "w").write(text)
print("flags=%d attrs=%d" % (flags, attrs))
PY
}

# post_py <opt.ll> <out.txt> <in|out>: text23n's last step on each side: line 1, the attribute groups and references
# dropped. The writer lowers llvm.scmp/ucmp to icmp and select (ruling D4: LLVM 15 has neither), so the output side
# folds its four lines (lt, gt, select gt 1 0, select lt -1 that) back into the call, and the input side loses the
# declarations; scalar forms only (a vector's would stay unfolded and differ).
post_py() {
  python3 -I - "$@" << 'PY'
import re, sys
t = open(sys.argv[1]).read().split("\n", 1)[1]
t = re.sub(r"^attributes #\d+ = .*\n", "", t, flags=re.M)
t = re.sub(r" #\d+( |,|$)", r"\1", t, flags=re.M)
if sys.argv[3] == "out":
    lowered = re.compile(r"^  (?P<lt>%\S+) = icmp (?P<k>[su])lt (?P<ty>i\d+) (?P<a>\S+), (?P<b>\S+)\n"
                         r"  (?P<gt>%\S+) = icmp (?P=k)gt (?P=ty) (?P=a), (?P=b)\n"
                         r"  (?P<sel>%\S+) = select i1 (?P=gt), (?P<rt>i\d+) 1, (?P=rt) 0\n"
                         r"  (?P<r>%\S+) = select i1 (?P=lt), (?P=rt) -1, (?P=rt) (?P=sel)$", re.M)
    t = lowered.sub(lambda m: "  %s = call %s @llvm.%scmp.%s.%s(%s %s, %s %s)" % (
        m["r"], m["rt"], m["k"], m["rt"], m["ty"], m["ty"], m["a"], m["ty"], m["b"]), t)
else:
    t = re.sub(r"\n(?:; Function Attrs: [^\n]*\n)?declare [^\n]*@llvm\.[su]cmp\.[^\n]*\n", "", t)
open(sys.argv[2], "w").write(t)
PY
}

# used: recompute the air= hash (FNV-1a 64 over the shader's metallibs in translation order) of every `ok` line.
# Usage: used_py <dxil-translate output> <folder of our metallibs>; prints "<matched> <ok lines>" and a line per mismatch.
used_py() {
  python3 -I - "$@" << 'PY'
import os, sys
ok = hit = 0
for line in open(sys.argv[1], errors="replace"):
    f = line.split()
    if len(f) < 3 or f[0] != "ok":
        continue
    air = next((p[4:] for p in f if p.startswith("air=")), None)
    ok += 1
    h, k = 0xCBF29CE484222325, 0
    while True:
        p = os.path.join(sys.argv[2], f[1] + (".%d" % k if k else "") + ".metallib")
        if not os.path.exists(p):
            break
        for b in open(p, "rb").read():
            h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
        k += 1
    if k and air == "%016x" % h:
        hit += 1
    else:
        print("used: %s: air=%s, our %d file(s) hash to %016x" % (f[1], air, k, h), file=sys.stderr)
print(hit, ok)
PY
}

config() {
  if [ -z "${BUILD_DIR:-}" ]; then
    common=$(git -C "$here" rev-parse --path-format=absolute --git-common-dir 2> /dev/null) || die "BUILD_DIR is not set"
    BUILD_DIR=$(dirname "$common")/build
  fi
  W=${W:-$BUILD_DIR/step4/w}
  S=${S:-$BUILD_DIR/step4}
  I23=${I23:-$BUILD_DIR/phase0/llvm-23}
  BIN23=${BIN23:-$I23/bin}
  AIRW=${AIRW:-$W/llvm23-build/bin/llvm-air-write}
  XLATE=${XLATE:-$W/tools/dxil-translate}
  A15=${A15:-$W/bin/air15}
  JOBS=${JOBS:-4}
  OUT_ROOT=${OUT_ROOT:-$W/out}
  ROWS_USER=${ROWS:-}
  export BUILD_DIR W S I23 BIN23 AIRW XLATE A15 JOBS OUT_ROOT
}

corpus_setup() {
  corpus=$1
  CID=-
  DEVIN=
  GATEIN=
  SRCDEF=
  ARGS=
  EXT=metallib
  case $corpus in
    shaders) CID=C1 DEVIN=$W/in/shaders GATEIN=$S/base/C1.out SRCDEF=$here/../tests/shaders ;;
    dxil) CID=C2 DEVIN=$W/in/dxil GATEIN=$S/base/C2.out SRCDEF=$here/../tests/dxil ;;
    dxbc) CID=C4 DEVIN=$W/in/dxbc GATEIN=$S/base/C4.out SRCDEF=$here/../tests/dxbc ;;
    dxbc-320) CID=C4-320 GATEIN=$S/base/C4-320.out SRCDEF=$here/../tests/dxbc ARGS="--metal 320" ;;
    d3d11) CID=C5 GATEIN=$S/base/C5.out ;;
    d3d11-320) CID=C5-320 GATEIN=$S/base/C5-320.out ARGS="--metal 320" ;;
    dx11) CID=C6 GATEIN=$S/base/C6.out SRCDEF=$here/../tests/dxbc-corpus ;;
    dx11-320) CID=C6-320 GATEIN=$S/base/C6-320.out SRCDEF=$here/../tests/dxbc-corpus ARGS="--metal 320" ;;
    smite2)
      CID=C8 DEVIN=$W/in/smite2 GATEIN=$S/base/C8.out
      SRCDEF="$HOME/Library/Application Support/MacNeutron/vm.0.7.2/runs/2437170/traces/dxmt"
      ;;
    helpers) DEVIN=$W/in/helpers EXT=air ;;
    *) die "unknown corpus '$corpus' (shaders dxil dxbc dxbc-320 d3d11 d3d11-320 dx11 dx11-320 smite2 helpers)" ;;
  esac
  if [ "${GATE:-}" = 1 ] && [ -n "$GATEIN" ]; then
    INDIR=$GATEIN
  elif [ -n "$DEVIN" ] && [ -d "$DEVIN" ]; then
    INDIR=$DEVIN
  else
    INDIR=$GATEIN
  fi
  [ -n "$INDIR" ] && [ -d "$INDIR" ] || die "no input folder for corpus $corpus (looked for '$DEVIN' and '$GATEIN')"
  eval "srcvar=\${SRC_$(echo "$corpus" | tr - _):-}"
  SRC=${srcvar:-$SRCDEF}
  if [ "${GATE:-}" = 1 ] && [ "$corpus" = smite2 ]; then ARGS="--pipelines $SRC/pipelines.txt"; fi
  T=$W/t/$corpus
  BASE=$W/in/$corpus.txt
  export corpus CID INDIR EXT SRC ARGS T BASE
}

# The input file names of the corpus (ONLY: a slice), one per line, sorted.
list_names() {
  ls "$INDIR" | /usr/bin/grep "\.$EXT\$" | LC_ALL=C sort > "$1.all"
  if [ -n "${ONLY:-}" ]; then
    /usr/bin/grep -Fx -f "$ONLY" "$1.all" > "$1"
  else
    mv "$1.all" "$1"
  fi
  rm -f "$1.all"
}

dis23() { # <in.bc> <out.ll>: LLVM 23's text, line 1 (ModuleID) dropped. Read from stdin: a module with no
  # SOURCE_FILENAME record otherwise takes the file's path as its source_filename, and the text follows the folder.
  "$BIN23/llvm-dis" -o "$2.tmp" < "$1" && tail -n +2 "$2.tmp" > "$2" && rm -f "$2.tmp"
}

prep_mod() { # <stem>
  in=$INDIR/$1.$EXT
  t=$T/$1
  if [ -s "$t.ll23" ] && [ -s "$t.types" ] && [ ! "$in" -nt "$t.ll23" ]; then return 0; fi
  if [ "$EXT" = air ]; then cp "$in" "$t.bc"; else "$A15" extract "$in" "$t.bc"; fi || return 1
  "$A15" dis "$t.bc" "$t.ll15" && "$A15" types "$t.bc" "$t.types" && dis23 "$t.bc" "$t.ll23"
}


# ---------------------------------------------------------------- the per-module worker (xargs -P runs one per stem)

# text23n <pre.ll> <out.bc> <work-stem> <flags-dropped> <attrs-dropped>: prints "ok" or why not.
text23n() {
  cnt=$(norm_py "$1" "$3.nrm-pre") || { echo "cannot normalize $1"; return; }
  opt="$BIN23/opt -mtriple=unknown-apple-macosx14.0.0 -passes=instsimplify -S"
  if ! { $opt "$3.nrm-pre" -o "$3.nrm-in" && $opt "$2" -o "$3.nrm-out"; } 2> "$3.nrm-err"; then
    echo "opt failed: $(head -n 1 "$3.nrm-err")"
    return
  fi
  for side in in out; do
    post_py "$3.nrm-$side" "$3.nrm-$side.txt" $side
  done
  want_flags=${cnt#flags=}
  want_flags=${want_flags%% *}
  want_attrs=${cnt##*attrs=}
  if ! cmp -s "$3.nrm-in.txt" "$3.nrm-out.txt"; then
    echo "text differs ($(diff "$3.nrm-in.txt" "$3.nrm-out.txt" | head -n 3 | tr '\n' '|'))"
    return
  fi
  [ "$want_flags" = "$4" ] || { echo "flags-dropped=$4 but $want_flags drop-list tokens in the input"; return; }
  [ "$want_attrs" = "$5" ] || { echo "attrs-dropped=$5 but $want_attrs drop-list entries in the input's attribute groups"; return; }
  echo ok
}

# gep0_eq <ours.ll15> <ref.ll15>: equal once every zero index of a constant getelementptr (one level of nested
# parentheses inside it at most; deeper ones stay as they are) reads i64 0. Ruling D1, the cause in the header's text15.
gep0_eq() {
  python3 -I - "$1" "$2" << 'PY'
import re, sys
gep = re.compile(r"getelementptr (?:inbounds )?\((?:[^()]|\([^()]*\))*\)")
def norm(path):
    return gep.sub(lambda m: re.sub(r", i(?:32|64) 0(?=[,)])", ", i64 0", m.group(0)), open(path).read())
sys.exit(0 if norm(sys.argv[1]) == norm(sys.argv[2]) else 1)
PY
}

# self_eq <post.ll> <ours.ll23>: equal, line 1 of the .post.ll dropped. A block label's "; preds =" comment lists the
# block's users in use-list order, which the bitcode does not carry (the writer writes no use lists, R2-6, and P15's
# passes reorder them; finding F-T4-2), so that comment is left out of the comparison (ruling R7-4); the terminators it
# summarizes are compared.
self_eq() {
  tail -n +2 "$1" | cmp -s - "$2" && return 0
  tail -n +2 "$1" | sed -E 's/[[:space:]]*; preds = .*$//' > "$2.np1"
  sed -E 's/[[:space:]]*; preds = .*$//' "$2" > "$2.np2"
  cmp -s "$2.np1" "$2.np2"
  r=$?
  rm -f "$2.np1" "$2.np2"
  return $r
}

# wrapper_ok <ours.bc> <ref.bc>: magic, version, offset and CPU type (words 0, 1, 2 and 4) equal.
wrapper_ok() {
  wa=$(od -An -tx4 -N20 "$1" 2> /dev/null | tr -s ' \n' '  ')
  wb=$(od -An -tx4 -N20 "$2" 2> /dev/null | tr -s ' \n' '  ')
  set -- $wa
  [ $# -eq 5 ] || return 1
  wa="$1 $2 $3 $5"
  set -- $wb
  [ $# -eq 5 ] && [ "$wa" = "$1 $2 $3 $5" ]
}

# The llvm.* declaration names of a typed or opaque text, sorted (ruling D6).
llvm_decls() { /usr/bin/grep -o '^declare [^@]*@llvm\.[A-Za-z0-9_.]*' "$1" | sed 's/.*@//' | LC_ALL=C sort; }

# One module, after its prep: writes (or takes) the output, then every row. Sets the row variables.
grade() { # <stem>
  stem=$1
  name=$stem.$EXT
  t=$T/$stem
  o=$O/$stem
  fail() { echo "$stem $1: $2" >> "$O/failures.txt"; }
  if [ "$LEG" = check ]; then
    P=$CHECKDIR/$name
    # the producer's own log, if it keeps one next to its metallibs, may carry the writer's stats line (informational)
    cp "$CHECKDIR/$stem.log" "$o.log" 2> /dev/null || : > "$o.log"
    if [ -f "$P" ]; then
      writer=1
      "$A15" extract "$P" "$o.bc" 2> "$o.extract.log" || { writer=0; fail writer "extract: $(head -n 1 "$o.extract.log")"; }
    else
      fail writer "no $name in the producer's folder"
    fi
  else
    case $LEG in
      full) { "$AIRW" "$t.bc" --types "$t.types" --emit-ll "$o" -o "$o.bc" > "$o.log" 2>&1; } 2> /dev/null ;;
      *)
        if [ -n "$HELPNAMES" ]; then
          awk -v names="$HELPNAMES" 'BEGIN { while ((getline l < names) > 0) drop[l] = 1 }
            { rest = substr($0, 3); if (match(rest, /^[@%]"[^"]*"/)) key = substr(rest, 1, RLENGTH); else { split(rest, p, " "); key = p[1] } }
            !(($1 == "f" || $1 == "g" || $1 == "s") && (key in drop))' "$t.types" > "$o.types"
        else
          cp "$t.types" "$o.types"
        fi
        { "$AIRW" "$t.bc" --types "$o.types" --named-only $REFARGS $PASSARGS --emit-ll "$o" -o "$o.bc" > "$o.log" 2>&1; } 2> /dev/null
        ;;
    esac
    rc=$?
    if [ $rc -eq 0 ] && [ -s "$o.bc" ]; then
      writer=1
    else
      why=$(/usr/bin/grep -m 1 '^error:' "$o.log" || head -n 1 "$o.log")
      fail writer "exit $rc: ${why:-no output}"
      rm -f "$o.bc"
    fi
    if [ $writer = 1 ] && [ "$LEG" != full ] && [ -n "$REFARGS" ]; then
      # P16's conflict check: the full f/g/s lines and the refs together; the output is discarded.
      if ! { "$AIRW" "$t.bc" --types "$t.types" --named-only $REFARGS $PASSARGS -o "$o.conflict.bc" > "$o.conflict.log" 2>&1; } 2> /dev/null; then
        writer=0
        fail writer "ref conflict check: $(/usr/bin/grep -m 1 '^error:' "$o.conflict.log" || head -n 1 "$o.conflict.log")"
      fi
      rm -f "$o.conflict.bc"
    fi
    /usr/bin/grep -q '^positional mismatch' "$o.log" && posmis=1
    case $LEG in full | opt23) [ "$(llvm_decls "$t.ll15")" = "$(llvm_decls "$t.ll23")" ] || upg=1 ;; esac
    /usr/bin/grep '^stale ' "$o.log" | sed "s/^/$stem /" >> "$O/stale.txt"
    if [ $writer = 1 ]; then
      if [ "$EXT" = metallib ]; then
        P=$USE/$name
        "$A15" pack "$INDIR/$name" "$o.bc" "$P" 2> "$o.pack.log" || { writer=0; fail writer "pack: $(head -n 1 "$o.pack.log")"; }
      else
        P=$o.bc
      fi
    fi
  fi
  if /usr/bin/grep -q '^stats ' "$o.log" 2> /dev/null; then
    eval "$(/usr/bin/grep '^stats ' "$o.log" | tail -n 1 | sed -E 's/stats inst-casts=([0-9]+) const-casts=([0-9]+) use-typed=([0-9]+) attrs-dropped=([0-9]+) flags-dropped=([0-9]+) refs=([0-9]+) stale=([0-9]+).*/ic=\1 cc=\2 ut=\3 ad=\4 fd=\5 rf=\6 st=\7/')"
  fi
  [ $writer = 1 ] || return 0

  case " $ROWS " in
    *" text15 "*) "$A15" dis "$P" "$o.ll15" 2> "$o.dis.log" || fail text15 "air15 dis: $(head -n 1 "$o.dis.log")" ;;
  esac
  case " $ROWS " in
    *" self "* | *" text23 "*) dis23 "$o.bc" "$o.ll23" 2> "$o.dis23.log" || fail text23 "llvm-dis 23: $(head -n 1 "$o.dis23.log")" ;;
  esac
  case " $ROWS " in
    *" self "*)
      if [ -s "$o.post.ll" ] && [ -s "$o.ll23" ] && self_eq "$o.post.ll" "$o.ll23"; then self=1; else fail self "llvm-dis of the output differs from the writer's .post.ll"; fi
      ;;
  esac
  if [ $posmis = 0 ] && [ $upg = 0 ]; then
    case " $ROWS " in
      *" text15 "*)
        if [ -s "$o.ll15" ] && cmp -s "$o.ll15" "$t.ll15"; then
          text15=1
        elif [ -s "$o.ll15" ] && gep0_eq "$o.ll15" "$t.ll15"; then
          text15=1
          echo "$stem" >> "$O/gep0.txt"
        else
          fail text15 "typed text differs ($(diff "$t.ll15" "$o.ll15" 2> /dev/null | head -n 3 | tr '\n' '|'))"
        fi
        ;;
    esac
    case " $ROWS " in
      *" text23 "*)
        if [ -s "$o.ll23" ] && cmp -s "$o.ll23" "$t.ll23"; then text23=1; else fail text23 "opaque text differs ($(diff "$t.ll23" "$o.ll23" 2> /dev/null | head -n 3 | tr '\n' '|'))"; fi
        ;;
    esac
  fi
  case " $ROWS " in
    *" verify "*)
      v=$("$A15" verify "$P" 2> /dev/null)
      if [ "$v" = ok ]; then verify=1; else fail verify "$v"; fi
      ;;
  esac
  case " $ROWS " in
    *" inv "*)
      iv=$(python3 -I "$here/air-bcinv.py" "$o.bc" "$t.bc" 2>&1)
      if [ "$iv" = ok ]; then inv=1; else fail inv "$(echo "$iv" | tr '\n' ';')"; fi
      ;;
  esac
  case " $ROWS " in
    *" wrapper "*)
      if wrapper_ok "$o.bc" "$t.bc"; then
        wrapper=1
      else
        fail wrapper "words differ: ours $(od -An -tx4 -N20 "$o.bc" | tr -s ' \n' ' ')/ reference $(od -An -tx4 -N20 "$t.bc" | tr -s ' \n' ' ')"
      fi
      ;;
  esac
  case " $ROWS " in
    *" text23n "*)
      if [ -s "$o.pre.ll" ] && [ $upg = 0 ]; then
        r=$(text23n "$o.pre.ll" "$o.bc" "$o" "$fd" "$ad")
        if [ "$r" = ok ]; then text23n=1; else fail text23n "$r"; fi
      fi
      ;;
  esac
}

worker() { # <stem>
  writer=0 self=0 text15=0 text23=0 text23n=0 posmis=0 upg=0 verify=0 inv=0 wrapper=0
  ic=0 cc=0 ut=0 ad=0 fd=0 rf=0 st=0
  if prep_mod "$1"; then
    grade "$1"
  else
    echo "$1 prep: failed" >> "$O/failures.txt"
  fi
  line="$1"
  for row in writer self text15 text23 text23n verify inv wrapper; do
    case " $ROWS " in
      *" $row "*) eval "line=\"\$line $row=\$$row\"" ;;
      *) line="$line $row=-" ;;
    esac
  done
  printf '%s posmis=%s upg=%s ic=%s cc=%s ut=%s ad=%s fd=%s rf=%s st=%s\n' "$line" "$posmis" "$upg" "$ic" "$cc" "$ut" "$ad" "$fd" "$rf" "$st" >> "$O/results.txt"
}

# ---------------------------------------------------------------- legs

run_workers() { # <worker-name>; the stems of $1.names, JOBS at a time
  sed "s/\.$EXT\$//" "$1" | xargs -P "$JOBS" -n 1 sh "$self" "$2" > /dev/null 2> "$3"
  [ ! -s "$3" ] || { echo "air-oracle: worker stderr:" >&2; head -n 5 "$3" >&2; }
}

leg_prep() {
  corpus_setup "$1"
  mkdir -p "$T"
  list_names "$T.names"
  export LEG=prep
  run_workers "$T.names" __prep "$T.err"
  n=0 ok=0
  while read -r f; do
    n=$((n + 1))
    [ -s "$T/${f%.$EXT}.ll23" ] && ok=$((ok + 1))
  done < "$T.names"
  echo "air-oracle prep $corpus: n=$n ok=$ok"
  [ "$ok" = "$n" ] && [ "$n" -gt 0 ]
}

# Everything the writer legs share: the module list, the folders, the tool options.
leg_setup() { # <leg> <corpus>
  LEG=$1
  corpus_setup "$2"
  suffix=
  [ -n "${ONLY:-}" ] && suffix=.only
  O=$OUT_ROOT/$LEG/$corpus$suffix
  USE=$O/use
  rm -rf "$O"
  mkdir -p "$O" "$USE" "$T"
  : > "$O/results.txt"
  : > "$O/failures.txt"
  : > "$O/stale.txt"
  : > "$O/gep0.txt"
  REFARGS= PASSARGS= HELPNAMES=
  case $LEG in
    full) ROWS="writer self text15 text23 verify inv wrapper" ;;
    defs | opt23)
      ROWS="writer self text23n verify inv wrapper"
      refs=${REFS-$(ls "$W"/in/helpers/*.air 2> /dev/null)}
      for r in $refs; do REFARGS="$REFARGS --ref $r"; done
      if [ -n "$refs" ]; then
        HELPNAMES=$O/helper.names
        : > "$HELPNAMES"
        for r in $refs; do
          "$A15" types "$r" - | awk '$1 ~ /^[fgs]$/ { rest = substr($0, 3); if (match(rest, /^[@%]"[^"]*"/)) print substr(rest, 1, RLENGTH); else { split(rest, p, " "); print p[1] } }' >> "$HELPNAMES"
        done
      fi
      [ "$LEG" = opt23 ] && PASSARGS="--passes $P15"
      ;;
    check) ROWS="writer verify inv wrapper" ;;
  esac
  [ "$EXT" = air ] && ROWS=$(echo "$ROWS" | sed 's/ wrapper//') # informational: the reference wrapper is Apple's
  [ -z "$ROWS_USER" ] || ROWS=$ROWS_USER
  export LEG O USE ROWS REFARGS PASSARGS HELPNAMES
}

metal_row() { # sets MET_*; needs O USE INDIR SRC ARGS BASE
  MET_USED=- MET_OK=- MET_BASE=- MET_NEWFAIL=- MET_REASONS=- MET_NOTE= MET_RAN=0
  if [ -n "${ONLY:-}" ] || [ "${NOMETAL:-}" = 1 ] || [ "$EXT" != metallib ]; then MET_NOTE="no Metal row"; return; fi
  nin=$(ls "$INDIR" | /usr/bin/grep -c '\.metallib$')
  nuse=$(ls "$USE" | /usr/bin/grep -c '\.metallib$')
  if [ "$nin" != "$nuse" ]; then
    MET_NOTE="Metal not run: $nuse metallibs in the --use folder, $nin in the input (a missing file would pass on the C++ AIR)"
    return
  fi
  [ -d "$SRC" ] || die "no shader folder $SRC for corpus $corpus (set SRC_$(echo "$corpus" | tr - _))"
  [ -x "$XLATE" ] || die "no dxil-translate at $XLATE"
  if [ ! -s "$BASE" ]; then
    echo "air-oracle: making the baseline $BASE (the run on the C++ metallibs)" >&2
    "$XLATE" "$SRC" $ARGS --use "$INDIR" > "$BASE.tmp" 2> "$BASE.err"
    mv "$BASE.tmp" "$BASE"
  fi
  "$XLATE" "$SRC" $ARGS --use "$USE" > "$O/metal.txt" 2> "$O/metal.err"
  MET_RAN=1
  u=$(used_py "$O/metal.txt" "$USE" 2>> "$O/failures.txt")
  MET_USED=${u% *}/${u#* }
  s=$(/usr/bin/grep '^summary ' "$O/metal.txt" | head -n 1 | awk '{print $2}')
  MET_OK=${s:-0/0}
  s=$(/usr/bin/grep '^summary ' "$BASE" | head -n 1 | awk '{print $2}')
  MET_BASE=${s:-0/0}
  python3 -I "$here/corpus-diff.py" "$BASE" "$O/metal.txt" > "$O/corpus-diff.txt" 2>&1
  MET_NEWFAIL=$(/usr/bin/grep -cE '^(new-fail|missing|corpus-diff: no) ' "$O/corpus-diff.txt")
  /usr/bin/grep '^fail ' "$BASE" | LC_ALL=C sort > "$O/base.fails"
  /usr/bin/grep '^fail ' "$O/metal.txt" | LC_ALL=C sort > "$O/metal.fails"
  if cmp -s "$O/base.fails" "$O/metal.fails"; then MET_REASONS=eq; else MET_REASONS=ne; fi
  /usr/bin/grep -E '^(new-fail|missing) ' "$O/corpus-diff.txt" | head -n 10 >> "$O/failures.txt"
}

summarize() { # after the modules: the Metal row, the summary line, the first failures; returns the exit status
  n=$(wc -l < "$O/results.txt" | tr -d ' ')
  agg=$(awk '
    { for (i = 2; i <= NF; i++) { split($i, kv, "="); k = kv[1]; v = kv[2]
        if (v == "-") { if (!(k in ok)) ok[k] = -1 }
        else if (k == "posmis" || k == "upg" || k ~ /^(ic|cc|ut|ad|fd|rf|st)$/) sum[k] += v
        else { if (ok[k] < 0) ok[k] = 0; ok[k] += v } } }
    END { for (k in ok) printf "%s=%s ", k, (ok[k] < 0 ? "-" : ok[k]); for (k in sum) printf "%s=%s ", k, sum[k] }' "$O/results.txt")
  get() { # <key> <default>
    gv=$(echo " $agg" | sed -n -E "s/.* $1=([^ ]*).*/\1/p")
    echo "${gv:-$2}"
  }
  metal_row
  t15=$(get text15 -) t23=$(get text23 -) t23n=$(get text23n -) pm=$(get posmis 0)
  line="air-oracle $LEG $corpus: n=$n writer=$(get writer -) self=$(get self -) text15=$t15 text23=$t23 text23n=$t23n posmis=$pm"
  line="$line verify=$(get verify -) inv=$(get inv -) wrapper=$(get wrapper -)"
  line="$line used=$MET_USED metal=$MET_OK base=$MET_BASE newfail=$MET_NEWFAIL reasons=$MET_REASONS"
  line="$line casts=$(get ic 0)/$(get cc 0) usetyped=$(get ut 0) dropped=$(get ad 0)/$(get fd 0) refs=$(get rf 0) stale=$(get st 0)"
  echo "$line"
  [ -z "$MET_NOTE" ] || echo "air-oracle: $MET_NOTE" >&2
  if [ -s "$O/failures.txt" ]; then
    echo "air-oracle: failures (first 25 of $(wc -l < "$O/failures.txt" | tr -d ' ') in $O/failures.txt):" >&2
    head -n 25 "$O/failures.txt" | cut -c 1-240 >&2
  fi
  rc=0
  need() { # <row> <value> <want>
    [ "$2" = "$3" ] || { echo "air-oracle: unmet: $1=$2 (want $3)" >&2; rc=1; }
  }
  [ "$n" -gt 0 ] || { echo "air-oracle: no input modules" >&2; rc=1; }
  for row in writer self verify inv wrapper; do
    case " $ROWS " in *" $row "*) need "$row" "$(get $row 0)" "$n" ;; esac
  done
  ug=$(get upg 0)
  bd=$(/usr/bin/grep -cE ' (posmis|upg)=1 ' "$O/results.txt")
  case " $ROWS " in
    *" text15 "*) need text15 "$t15" $((n - bd)) ;;
  esac
  case " $ROWS " in
    *" text23 "*) need text23 "$t23" $((n - bd)) ;;
  esac
  case " $ROWS " in *" text23n "*) need text23n "$t23n" $((n - ug)) ;; esac
  if [ "$bd" -gt 0 ]; then
    # Ruling D2: LLVM 23's reader changed these modules' instruction counts; ruling D6: it upgraded their intrinsics.
    # A sliced defs run grades them instead.
    /usr/bin/grep -E ' (posmis|upg)=1 ' "$O/results.txt" | awk '{ print $1 }' | sed "s/\$/.$EXT/" > "$O/posmis.names"
    awk '/ upg=1 / { print $1 }' "$O/results.txt" > "$O/upgraded.txt"
    ROWS= ONLY=$O/posmis.names OUT_ROOT=$O/posmis NOMETAL=1 sh "$self" defs "$corpus" > "$O/posmis.summary" 2> "$O/posmis.err"
    awk '{ ok = 1; for (i = 2; i <= NF; i++) { split($i, kv, "="); if (kv[1] ~ /^(writer|self|text23n|verify|inv|wrapper)$/ && kv[2] != "-" && kv[2] != 1) ok = 0 }
      print $1 ": defs " (ok ? "ok" : "fail") " (" $0 ")" }' "$O/posmis/defs/$corpus.only/results.txt" > "$O/posmis.txt" 2> /dev/null
    pmok=$(/usr/bin/grep -c ': defs ok ' "$O/posmis.txt")
    echo "air-oracle: posmis $pm (ruling D2) and upgraded intrinsics $ug (ruling D6, $O/upgraded.txt) graded by defs: $pmok/$bd ok, in $O/posmis.txt" >&2
    need posmis-defs "$pmok" "$bd"
  fi
  [ ! -s "$O/gep0.txt" ] || echo "air-oracle: text15 equal only up to constant GEP zero index types (ruling D1): $(wc -l < "$O/gep0.txt" | tr -d ' ') modules, in $O/gep0.txt" >&2
  if [ -z "${ONLY:-}" ] && [ "${NOMETAL:-}" != 1 ] && [ "$EXT" = metallib ]; then
    if [ "$MET_RAN" = 1 ]; then
      need used "$MET_USED" "${MET_USED#*/}/${MET_USED#*/}"
      need newfail "$MET_NEWFAIL" 0
      need reasons "$MET_REASONS" eq
    else
      echo "air-oracle: unmet: the Metal row did not run" >&2
      rc=1
    fi
  fi
  return $rc
}

leg_writer() { # <leg> <corpus>
  leg_setup "$1" "$2"
  [ -x "$AIRW" ] || die "no llvm-air-write at $AIRW (set AIRW)"
  [ -x "$A15" ] || die "no air15 at $A15 (sh dxmt/tools/air-tools.sh)"
  list_names "$O/names"
  export CHECKDIR=
  run_workers "$O/names" __worker "$O/worker.err"
  summarize
}

leg_check() { # <out-dir> <corpus>
  CHECKDIR=$(cd "$1" && pwd) || die "no folder $1"
  export CHECKDIR
  leg_setup check "$2"
  USE=$CHECKDIR
  export USE
  list_names "$O/names"
  run_workers "$O/names" __worker "$O/worker.err"
  summarize
}

leg_probe() {
  corpus_setup "$1"
  p15=$(dirname "$A15")/dxil-probe-15
  p23=$(dirname "$A15")/dxil-probe-23
  [ -x "$p15" ] && [ -x "$p23" ] || die "no dxil-probe-15/-23 beside $A15"
  [ -d "$SRC" ] || die "no shader folder $SRC"
  n=0 eq=0 bad=
  for f in "$SRC"/*.dxil "$SRC"/reject/*.dxil; do
    [ -f "$f" ] || continue
    n=$((n + 1))
    pa=$("$p15" "$f" 2>&1; echo "rc=$?")
    pb=$("$p23" "$f" 2>&1; echo "rc=$?")
    if [ "$pa" = "$pb" ]; then eq=$((eq + 1)); else bad="$bad $(basename "$f")"; fi
  done
  h=0 hok=0
  for f in "$W"/in/helpers/*.air; do
    [ -f "$f" ] || continue
    h=$((h + 1))
    "$BIN23/llvm-dis" -o /dev/null "$f" 2> /dev/null && hok=$((hok + 1))
  done
  echo "air-oracle probe $corpus: n=$n probe=$eq helpers=$hok/$h"
  [ -z "$bad" ] || echo "probe: different output for:$bad" >&2
  [ "$n" -gt 0 ] && [ "$eq" = "$n" ] && [ "$h" -gt 0 ] && [ "$hok" = "$h" ]
}

# det_compare <dir-a> <dir-b>: *.bc *.metallib *.ll15 *.ll23 byte-equal, and the stats/error lines of the logs equal.
det_compare() {
  files=0 diffs=0
  for d in "$1" "$2"; do [ -d "$d" ] || die "no folder $d"; done
  for pat in '*.bc' '*.metallib' '*.ll15' '*.ll23'; do
    (cd "$1" && find . -name "$pat" | LC_ALL=C sort) > "$1/.det.list"
    (cd "$2" && find . -name "$pat" | LC_ALL=C sort) > "$2/.det.list"
    if ! cmp -s "$1/.det.list" "$2/.det.list"; then
      echo "det: the lists of $pat differ" >&2
      diffs=$((diffs + 1))
    fi
    while read -r f; do
      files=$((files + 1))
      if ! { [ -f "$2/$f" ] && cmp -s "$1/$f" "$2/$f"; }; then
        echo "det: $f differs" >&2
        diffs=$((diffs + 1))
      fi
    done < "$1/.det.list"
  done
  for log in "$1"/*.log; do
    [ -f "$log" ] || continue
    x=$(/usr/bin/grep -E '^(stats|error:|positional mismatch|stale )' "$log")
    y=$(/usr/bin/grep -E '^(stats|error:|positional mismatch|stale )' "$2/$(basename "$log")" 2> /dev/null)
    files=$((files + 1))
    if [ "$x" != "$y" ]; then
      echo "det: $(basename "$log") differs" >&2
      diffs=$((diffs + 1))
    fi
  done
  rm -f "$1/.det.list" "$2/.det.list"
  echo "det-compare: files=$files diffs=$diffs"
  [ "$files" -gt 0 ] && [ "$diffs" -eq 0 ]
}

leg_det() {
  corpus_setup "$1"
  droot=$W/out/det/$corpus
  rm -rf "$droot"
  mkdir -p "$droot"
  slice=
  sfx=
  if [ "$corpus" = smite2 ]; then
    ls "$INDIR" | /usr/bin/grep '\.metallib$' | LC_ALL=C sort | head -n 500 > "$droot/slice.txt"
    slice=$droot/slice.txt
    sfx=.only
  fi
  rc=0
  runs=0 same=0 bruns=0 bsame=0
  for L in full defs opt23; do
    for r in a b; do
      OUT_ROOT=$droot/$r NOMETAL=1 ONLY=$slice sh "$self" "$L" "$corpus" > "$droot/$L.$r.summary" 2> "$droot/$L.$r.err"
    done
    runs=$((runs + 1))
    OA=$droot/a/$L/$corpus$sfx
    if det_compare "$OA" "$droot/b/$L/$corpus$sfx" > "$droot/$L.cmp" 2>&1; then
      same=$((same + 1))
    else
      rc=1
      head -n 8 "$droot/$L.cmp" >&2
    fi
    # the tool's --batch: -j 8 against -j 1 (and against the single runs' .bc)
    : > "$droot/$L.list"
    while read -r f; do
      s=${f%.$EXT}
      [ -s "$OA/$s.bc" ] || continue
      types=$T/$s.types
      [ "$L" = full ] || types=$OA/$s.types
      echo "$T/$s.bc $types BATCH/$s.bc" >> "$droot/$L.list"
    done < "$OA/names"
    batch_opts=
    case $L in
      defs | opt23)
        for ref in ${REFS-$(ls "$W"/in/helpers/*.air 2> /dev/null)}; do batch_opts="$batch_opts --ref $ref"; done
        batch_opts="$batch_opts --named-only"
        [ "$L" = opt23 ] && batch_opts="$batch_opts --passes $P15"
        ;;
    esac
    for j in 1 8; do
      mkdir -p "$droot/batch$j"
      sed "s#BATCH#$droot/batch$j#" "$droot/$L.list" > "$droot/$L.list$j"
      "$AIRW" --batch "$droot/$L.list$j" -j $j $batch_opts 2>&1 | sed "s#$droot/batch$j/##" | /usr/bin/grep -E '^[^ ]*: (stats|error:|positional mismatch|stale )' > "$droot/batch$j.log"
    done
    bruns=$((bruns + 1))
    bok=1
    det_compare "$droot/batch1" "$droot/batch8" > "$droot/$L.bcmp" 2>&1 || bok=0
    cmp -s "$droot/batch1.log" "$droot/batch8.log" || { echo "det: batch $L logs differ between -j 1 and -j 8" >&2; bok=0; }
    nb=0
    for f in "$droot"/batch8/*.bc; do
      [ -f "$f" ] || continue
      nb=$((nb + 1))
      cmp -s "$f" "$OA/$(basename "$f")" || { echo "det: batch $L $(basename "$f") differs from the single run" >&2; bok=0; }
    done
    [ "$nb" -gt 0 ] || { echo "det: batch $L wrote nothing" >&2; bok=0; }
    if [ $bok = 1 ]; then bsame=$((bsame + 1)); else rc=1; head -n 8 "$droot/$L.bcmp" >&2; fi
    rm -rf "$droot/batch1" "$droot/batch8"
  done
  echo "air-oracle det $corpus: legs=3 runs-equal=$same/$runs batch-equal=$bsame/$bruns"
  [ "$same" = "$runs" ] && [ "$bsame" = "$bruns" ] && [ $rc = 0 ]
}

# ---------------------------------------------------------------- main

config
cmd=${1:-}
case $cmd in
  __worker | __prep)
    [ -n "${2:-}" ] || exit 0
    if [ "$cmd" = __worker ]; then worker "$2"; else prep_mod "$2"; fi
    exit 0
    ;;
  prep)
    [ $# -eq 2 ] || die "usage: air-oracle.sh prep <corpus>"
    leg_prep "$2"
    ;;
  full | defs | opt23)
    [ $# -eq 2 ] || die "usage: air-oracle.sh $cmd <corpus>"
    leg_writer "$cmd" "$2"
    ;;
  probe)
    [ $# -eq 2 ] || die "usage: air-oracle.sh probe <corpus>"
    leg_probe "$2"
    ;;
  det)
    [ $# -eq 2 ] || die "usage: air-oracle.sh det <corpus>"
    leg_det "$2"
    ;;
  det-compare)
    [ $# -eq 3 ] || die "usage: air-oracle.sh det-compare <dir-a> <dir-b>"
    det_compare "$2" "$3"
    ;;
  norm)
    [ $# -eq 3 ] || die "usage: air-oracle.sh norm <in.ll> <out.ll>"
    norm_py "$2" "$3"
    ;;
  text23n)
    [ $# -eq 6 ] || die "usage: air-oracle.sh text23n <pre.ll> <out.bc> <work-stem> <flags-dropped> <attrs-dropped>"
    r=$(text23n "$2" "$3" "$4" "$5" "$6")
    echo "$r"
    [ "$r" = ok ]
    ;;
  check)
    [ $# -eq 3 ] || die "usage: air-oracle.sh check <out-dir> <corpus>"
    leg_check "$2" "$3"
    ;;
  *) die "usage: air-oracle.sh prep|full|defs|opt23|probe|det <corpus> | check <out-dir> <corpus> | det-compare <a> <b>" ;;
esac

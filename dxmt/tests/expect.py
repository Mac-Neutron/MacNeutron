#!/usr/bin/env python3
"""expect.py <exec output> <group>: checks one behaviour group's words from a d3d12_dxil_exec run against its CPU
reference (DXIL SM 6.8 plan). Prints "match", the first mismatch ("<group> thread <t> slot <s>: got <hex> want <hex>"),
"<group> <rest of its line>" when the group failed, or "<group> missing".

Each group has expect_<group, - as _>(got) returning 1024 words, thread-major, 16 per thread (common.hlsli's Put);
None skips a word, and `got` serves self-references. Words a group never writes are 0 (Out is zeroed before each
group). The helpers mirror common.hlsli's inputs. Waves are 32 lanes of consecutive threads (d3d12_device.cpp)."""
import struct, sys


def f32(x):  # float32 bits of x
    return struct.unpack("<I", struct.pack("<f", x))[0]


def in_word(i):  # InWord(i)
    return (i * 2654435761) & 0xffffffff


def in_float_bits(i):  # asuint(InFloat(i)): (i - 512) / 37 in float32
    return f32((i - 512) / 37.0)


def tex_rgba(x, y):  # Tex's texel (x, y) as its four bytes
    return (x * 32, y * 32, (x + y) * 16, 255)


def wave(i):
    return i // 32


def lane(i):
    return i % 32


def per_thread(slots):  # 1024 words from slots(t) -> the first words of thread t; the rest 0
    want = []
    for t in range(64):
        words = list(slots(t))
        want += words + [0] * (16 - len(words))
    return want


# Slot 0: DXC folds IsHelperLane() to false in compute (no op 221). QuadAny/QuadAll vote over threads 4q .. 4q+3.
def expect_helperquad(got):
    def slots(t):
        quad = range(t & ~3, (t & ~3) + 4)
        w = [in_word(i) for i in quad]
        return [0, int(any(x & 7 == 0 for x in w)), int(all(x & 1 for x in w)), int(5 in quad),
                int(all(i < 62 for i in quad))]
    return per_thread(slots)


# D3D's msad4: result i is accum[i] plus |ref_j - src_i_j| for each reference byte j that isn't 0, where src_i is the
# 32 bits starting 8i bits into source.x | source.y << 32.
def msad(ref, src, accum):
    for j in range(4):
        r, s = (ref >> 8 * j) & 255, (src >> 8 * j) & 255
        accum += abs(r - s) if r else 0
    return accum & 0xffffffff


def expect_msad(got):
    def slots(t):
        source = in_word(t + 64) | in_word(t + 128) << 32
        accum = [in_word(t + 192) & 0xffff, 1, 2, 3]
        return [msad(in_word(t), (source >> 8 * i) & 0xffffffff, accum[i]) for i in range(4)]
    return per_thread(slots)


# Per 32-lane wave, m is the lanes with the same key ((w >> 9) & 3); the prefixes fold the lanes of m below this one,
# in lane order, wrapping at 32 bits, from 0 (sum, or, xor), 1 (product) or all ones (and). Slot 1 (m's lanes 32-127)
# is 0. Slot 2: a key-3 lane holds a NaN and matches only itself. Slots 10-14 run on the threads t % 3 != 0 (the others
# write 0xe15e to slot 10), and WaveMatch and the prefixes count only those lanes.
def expect_wavematch(got):
    def fold(xs):  # sum, product, and, or, xor, count bits over the lanes' words xs
        product, band, bor, bxor = 1, 0xffffffff, 0, 0
        for x in xs:
            product, band, bor, bxor = product * ((x >> 8 & 3) + 1), band & x, bor | x & 0xf0f0, bxor ^ x
        return sum(x & 0xff for x in xs), product & 0xffffffff, band, bor, bxor, sum(x & 16 != 0 for x in xs)

    def slots(t):
        ws = [in_word(i) for i in range(wave(t) * 32, wave(t) * 32 + 32)]
        on = [(wave(t) * 32 + j) % 3 != 0 for j in range(32)]
        key = [x >> 9 & 3 for x in ws]
        group = [j for j in range(32) if key[j] == key[lane(t)]]
        m = sum(1 << j for j in group)
        total, product, band, bor, bxor, count = fold([ws[j] for j in group if j < lane(t)])
        # f32(0) for an empty prefix on purpose: an empty sum is +0.0; Task 2's -0.0 is only a left-out lane's operand.
        want = [m, 0, 1 << lane(t) if key[lane(t)] == 3 else m, total, product, band, bor, bxor, count, f32(total)]
        if not on[lane(t)]:
            return want + [0xe15e]
        total, product, band, _, _, count = fold([ws[j] for j in group if j < lane(t) and on[j]])
        return want + [sum(1 << j for j in group if on[j]), total, product, band, count]
    return per_thread(slots)


# [WaveSize(32)] at cs_6_6 (tag 11) and at cs_6_8 (tag 23, max 0), and [WaveSize(16, 64, 32)]: accepted, 32-lane waves.
def expect_wavesize32(got):
    return per_thread(lambda t: [32, lane(t)])


expect_wavesize32r = expect_wavesize = expect_wavesize32


# Programmable offsets (SM 6.7) from base texel (4, 4): Load and SampleLevel (point, clamp) take the offset's low 4
# bits, sign-extended; GatherRed/GatherGreen take og as is, footprint (4..5, 4..5) + og clamped to [0, 7], components
# (i0,j1) (i1,j1) (i1,j0) (i0,j0). Slots 4 and 5: Load with the constant offset (12, -13) and with (o.x, -13), constants
# wrapped as runtime values are. Slots hold the texel bytes packed (common.hlsli's tex_rgba).
def expect_offsets(got):
    k_off = [-4, -1, 0, 3, 12, 13, -13, -15]
    wrap = lambda v: ((v & 15) ^ 8) - 8
    clamp = lambda v: min(max(v, 0), 7)
    pack = lambda b: b[0] | b[1] << 8 | b[2] << 16 | b[3] << 24

    def slots(t):
        w = in_word(t)
        ox, oy = wrap(k_off[w & 7]), wrap(k_off[(w >> 3) & 7])
        gx, gy = k_off[w & 3], k_off[(w >> 3) & 3]
        i0, i1, j0, j1 = clamp(4 + gx), clamp(5 + gx), clamp(4 + gy), clamp(5 + gy)
        texel = pack(tex_rgba(4 + ox, 4 + oy))
        return [texel, texel, pack([tex_rgba(i, 0)[0] for i in (i0, i1, i1, i0)]),
                pack([tex_rgba(0, j)[1] for j in (j1, j1, j0, j0)]),
                pack(tex_rgba(4 + wrap(12), 4 + wrap(-13))), pack(tex_rgba(4 + ox, 4 + wrap(-13)))]
    return per_thread(slots)


# Compute derivatives on 1D quads: thread t is quad q = t >> 2's lane l = t & 3 (one 64-thread group), v = t * t.
# Coarse from lanes 0-2 of the quad, fine from this lane's row (x) or column (y). uv steps 2^k / 8, k = q % 5: LOD k
# (Depth's 4 mips clamp it to 3; Tex at uv * (1, 2): k + 1), LODs as round(lod * 64). Depth's mip m holds 0.125 + 0.25 m;
# SampleCmp (LESS_EQUAL, reference 0.5) is 1 from mip 2 up.
def expect_deriv(got):
    v = lambda i: float(i * i)
    clamp = lambda m: min(max(m, 0), 3)
    mip = lambda m: f32(0.125 + 0.25 * m)

    def slots(t):
        q, l, k = t >> 2, t & 3, (t >> 2) % 5
        b = 4 * q
        return [f32(v(b + 1) - v(b)), f32(v(b + 2) - v(b)),
                f32(v(b + (l | 1)) - v(b + (l & 2))), f32(v(b + (l | 2)) - v(b + (l & 1))),
                64 * k, 64 * min(k, 3), mip(clamp(k)), mip(clamp(k - 1)), f32(1.0 if clamp(k) >= 2 else 0.0), 64 * (k + 1)]
    return per_thread(slots)


# 2D compute quads (one group of W x H threads, H = 64 / W or 6 for W = 6): row gi is thread (x, y) = (gi % W, gi // W),
# in the 2x2 quad at (x & ~1, y & ~1), v = x² + 3y² + xy. Coarse derivatives from the quad's top-left, top-right and
# bottom-left; fine from this thread's row (x) or column (y). The quad reads return the group index of (x ^ 1, y),
# (x, y ^ 1), (x ^ 1, y ^ 1) and lane 3 (x | 1, y | 1). Tex's LOD at (x & 1, y & 1) * 2^k / 8 is k = (x/2 + y/2) % 4.
def quads_2d(w, h):
    v = lambda x, y: float(x * x + 3 * y * y + x * y)

    def slots(t):
        if t >= w * h:
            return []
        x, y = t % w, t // w
        bx, by = x & ~1, y & ~1
        gi = lambda x, y: y * w + x
        return [f32(v(bx + 1, by) - v(bx, by)), f32(v(bx, by + 1) - v(bx, by)),
                f32(v(bx + 1, y) - v(bx, y)), f32(v(x, by + 1) - v(x, by)),
                gi(x ^ 1, y), gi(x, y ^ 1), gi(x ^ 1, y ^ 1), gi(x | 1, y | 1), x | y << 8, 64 * ((x // 2 + y // 2) % 4)]
    return per_thread(slots)


def expect_deriv2d(got):
    return quads_2d(8, 8)


def expect_deriv2dwide(got):
    return quads_2d(32, 2)


def expect_deriv2dodd(got):
    return quads_2d(6, 6)


# Dynamic resources (dxil/heap.hlsl): each even slot 0-14, read through the descriptor heaps, equals the odd slot after
# it, read through the table or a root binding. CPU anchors, so reads that went nowhere can't pass as two equal zeros:
# slot 11 is RWTyped after the heap write, ((i + 1) & 63) * 7 + 1; slot 1 In's word i (so slot 0 isn't all zero); slot
# 3 Tex's green sampled linear at the corner above row y = i >> 3, times 510: rows y - 1 and y (32 apart) blend, 64y - 32
# (row 0 clamps to 0), which a point sampler can't give.
# heap-swap prints the dispatch after heap2's binding: its slot 0 is a raw view of zeros, so slots 0 and 1 read 0.
def expect_heap(got, raw=in_word):
    want = [got[i | 1] for i in range(1024)]
    for t in range(64):
        want[t * 16 + 1], want[t * 16 + 11] = raw(t), ((t + 1) & 63) * 7 + 1
        want[t * 16 + 3] = max(64 * (t >> 3) - 32, 0)
    return want


def expect_heap_swap(got):
    return expect_heap(got, lambda t: 0)


# Shader Model 6.8's Barrier() (dxil/barrier68.hlsl), barriers.hlsl's words: each thread reads another's groupshared
# word and RWTyped element across a synced barrier (threads 32-63 are another simdgroup).
def expect_barrier68(got):
    return per_thread(lambda t: [in_word(63 - t), ((t + 1) & 63) * 3 + 1, ((t + 2) & 63) + 7 + ((t + 3) & 63) * 5])


# Shader Model 6.8's comparison sampling (dxil/cmp68.hlsl), deriv's quads: thread t's quad q = t >> 2 has LOD k = q % 5
# (Depth's 4 mips clamp a sample's to 3). The point comparison (LESS_EQUAL) reads 1.0 where the reference is at most mip
# m's 0.125 + 0.25 m, else 0.0: SampleCmpGrad at LOD k (and k + 1 for the doubled gradients), SampleCmpBias at k - 1 and
# k + 1. Slots 3 and 4 the LODs as round(lod * 64).
def expect_cmp68(got):
    def cmp(reference, m):
        return f32(1.0 if reference <= 0.125 + 0.25 * min(max(m, 0), 3) else 0.0)

    def slots(t):
        k = (t >> 2) % 5
        return [cmp(0.5, k), cmp(0.5, k - 1), cmp(0.5, k + 1), 64 * min(k, 3), 64 * k, cmp(0.5, k), cmp(0.1, 0),
                cmp(0.7, k + 1), cmp(0.5, k)]
    return per_thread(slots)



# Five locations (raw, structured, groupshared, typed buffer, typed texture), each returning 0, a, c (floatatomics.hlsl);
# slot 15, the raw location, is 0 again after its last exchange.
def expect_floatatomics(got):
    return per_thread(lambda t: [f32(0.0), f32(t * 0.5 + 0.25), f32(t * 4.0 + 1.0)] * 5)


# 64-bit atomics (dxil/atomic64.hlsli): V adds to both halves; q(j) is Q(j).
M64, V = (1 << 64) - 1, 0x100000001


def q(j):
    return in_word(j) << 32 | in_word(j + 256)


def s64(x):  # a 64-bit word as signed
    return x - (1 << 64) if x >> 63 else x


# atomic64.hlsli's thread-owned sequence of kind k on a zeroed location: [o1, o2, o3, o4].
def own64(k, x0, a, b, c):
    if k == 9:  # CompareExchange (x0, a), (x0, b), (a, c)
        x, olds = x0, []
        for compare, v in ((x0, a), (x0, b), (a, c)):
            olds.append(x)
            x = v if x == compare else x
        return olds + [x]
    if k == 10:  # CompareStore (x0, a), (x0, b), then Exchange c; o1 = o2 = 0
        x = x0
        for compare, v in ((x0, a), (x0, b)):
            x = v if x == compare else x
        return [0, 0, x, c]
    f = {0: lambda o, v: (o + v) & M64, 11: lambda o, v: (o + v) & M64, 1: lambda o, v: o & v, 2: lambda o, v: o | v,
         3: lambda o, v: o ^ v, 4: lambda o, v: min(o, v, key=s64), 5: lambda o, v: max(o, v, key=s64), 6: min, 7: max,
         8: lambda o, v: v}[k]
    x, olds = x0, []
    for v in (a, b, c):
        olds.append(x)
        x = f(x, v)
    return olds + [x]


# The linearizability multiset identity: {init} + {f(old_i, v_i)} == {old_i} + {final}, over 64-bit ints.
def linearizable(olds, vals, init, final, f):
    return sorted([init] + [f(o, v) & M64 for o, v in zip(olds, vals)]) == sorted(olds + [final])


def word64(got, t, slot):  # thread t's 64-bit value at slots (slot, slot + 1)
    return got[t * 16 + slot] | got[t * 16 + slot + 1] << 32


def own64_words(t):  # thread t's slots 0-7: its own sequence (atomic64.hlsli's OPERANDS and OWN), as 32-bit words
    k = t % 12
    x0, a, b, c = (0xffffffff, 1, 0xffffffff, V) if k == 11 else (q(t), q(t + 64), q(t + 128), q(t + 192))
    words = []
    for o in own64(k, x0, a, b, c):
        words += [o & 0xffffffff, o >> 32]
    return words


# Slots 0-7 the thread's own sequence (raw for threads 0-31, structured for 32-63); 8-9, 10-11 and 12 shared qwords of
# a64 checked as multisets (ValueError names a bad one); 13 an out-of-bounds add (0); 14-15 the finals of threads 0-2.
def expect_atomic64(got):
    vals = [(i >> 3) << 32 | (in_word(i) & 0xffff) for i in range(64)]
    adds = [word64(got, t, 8) for t in range(64)]
    if sorted(adds) != sorted(k * V & M64 for k in range(64)):
        raise ValueError("slots 8-9: the four-view adds' olds aren't {0..63} * V")
    maxes = [word64(got, t, 10) for t in range(64)]
    if word64(got, 1, 14) != max(vals) or not linearizable(maxes, vals, 0, max(vals), max):
        raise ValueError("slots 10-11: the UMax olds aren't linearizable")
    if sorted(got[t * 16 + 12] for t in range(64)) != list(range(64)):
        raise ValueError("slot 12: the CAS-counter tickets aren't {0..63}")

    def slots(t):
        finals = {0: [0x40, 0x40], 1: [max(vals) & 0xffffffff, max(vals) >> 32], 2: [0x40, 0]}.get(t, [0, 0])
        return own64_words(t) + got[t * 16 + 8:t * 16 + 13] + [0] + finals
    return per_thread(slots)


# atomic64typed.hlsl: slots 0-7 the thread's own sequence (typed for threads 0-31, texel for 32-63); 8-9 (an add through
# a raw and a typed view of one qword), 10-11 (texel (x, 1)'s max, from threads i & 3 == x) and 12 (a CAS counter on
# texel (0, 0)) checked as multisets; 13 an add out of the typed view (0); 14-15 the finals of threads 0-6.
def expect_atomic64typed(got):
    vals = [(in_word(i) >> 28) << 32 | i for i in range(64)]
    maxima = [max(vals[x::4]) for x in range(4)]
    if sorted(word64(got, t, 8) for t in range(64)) != sorted(k * V & M64 for k in range(64)):
        raise ValueError("slots 8-9: the raw and typed adds' olds aren't {0..63} * V")
    for x in range(4):
        olds = [word64(got, t, 10) for t in range(x, 64, 4)]
        if word64(got, x + 1, 14) != maxima[x] or not linearizable(olds, vals[x::4], 0, maxima[x], max):
            raise ValueError(f"slots 10-11: texel ({x}, 1)'s max olds aren't linearizable")
    if sorted(got[t * 16 + 12] for t in range(64)) != list(range(64)):
        raise ValueError("slot 12: the CAS-counter tickets aren't {0..63}")

    def slots(t):
        final = {0: 64 * V, 5: 0x40}.get(t, maxima[t - 1] if 1 <= t <= 4 else 0)
        return own64_words(t) + got[t * 16 + 8:t * 16 + 13] + [0, final & 0xffffffff, final >> 32]
    return per_thread(slots)


# 4,096 adds of V on Out's qword 0 (words 0-1: 4096 * V), four on each of 1,024 qwords of a64, 4,096 on a typed element
# and 4,096 on a texel; every old's k marked once in Out's bitmaps (words 128-639 all ones), no malformed old (word 2).
def expect_atomic64c(got):
    return [0x1000, 0x1000] + [0] * 126 + [0xffffffff] * 512 + [0] * 384


expect_atomic64c_rootheap = expect_atomic64c


# gsatomic64.hlsl (Task 14c): slots 0-7 the thread's own sequence on G[i] (GS[i] for the signed kinds); 8-9 the adds of
# In's words into Total, 10-11 the UMax on M and 12 the CAS counter on C, checked as multisets; 13 unused (0); 14-15 the
# finals of threads 0-2 (Total, M, C) by plain loads after a barrier.
def expect_gsatomic64(got):
    adds = [in_word(i) for i in range(64)]
    total = sum(adds) & M64
    if word64(got, 0, 14) != total or not linearizable([word64(got, t, 8) for t in range(64)], adds, 0, total,
                                                         lambda o, v: o + v):
        raise ValueError("slots 8-9: the groupshared adds' olds aren't linearizable")
    vals = [(i >> 3) << 32 | (in_word(i) & 0xffff) for i in range(64)]
    if word64(got, 1, 14) != max(vals) or not linearizable([word64(got, t, 10) for t in range(64)], vals, 0, max(vals),
                                                             max):
        raise ValueError("slots 10-11: the groupshared UMax olds aren't linearizable")
    if sorted(got[t * 16 + 12] for t in range(64)) != list(range(64)):
        raise ValueError("slot 12: the groupshared CAS-counter tickets aren't {0..63}")

    def slots(t):
        final = {0: total, 1: max(vals), 2: 0x40}.get(t, 0)
        return own64_words(t) + got[t * 16 + 8:t * 16 + 13] + [0, final & 0xffffffff, final >> 32]
    return per_thread(slots)


# gsatomic64c.hlsl (Task 14c): 1,024 adds of V on A and of 1 on B (from 0xfffffe00); every old's k marked once (words
# 0-31 for A, 32-63 for B), no malformed old (word 64), and the finals: A = 1024 * V, B = 0x100000200 (words 65-68).
def expect_gsatomic64c(got):
    return [0xffffffff] * 64 + [0, 0x400, 0x400, 0x200, 1] + [0] * (1024 - 69)


def main(path, group):
    for line in open(path, encoding="utf-8", errors="replace"):
        parts = line.split()
        if parts[:2] != ["group", group]:
            continue
        if parts[2:3] != ["ok"]:
            print(group, " ".join(parts[2:]))
            return 1
        if len(parts) != 3 + 1024:
            print(f"{group} ok with {len(parts) - 3} words")
            return 1
        got = [int(x, 16) for x in parts[3:]]
        try:
            want = globals()["expect_" + group.replace("-", "_")](got)
        except ValueError as e:
            print(group, e)
            return 1
        for i, (g, w) in enumerate(zip(got, want)):
            if w is not None and g != w:
                print(f"{group} thread {i // 16} slot {i % 16}: got {g:08x} want {w:08x}")
                return 1
        print("match")
        return 0
    print(group, "missing")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))

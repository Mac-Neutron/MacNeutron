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
# (i0,j1) (i1,j1) (i1,j0) (i0,j0). Slots hold the texel bytes packed (common.hlsli's tex_rgba).
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
                pack([tex_rgba(0, j)[1] for j in (j1, j1, j0, j0)])]
    return per_thread(slots)


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
        want = globals()["expect_" + group.replace("-", "_")](got)
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

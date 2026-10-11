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

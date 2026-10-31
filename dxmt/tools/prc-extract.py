#!/usr/bin/env python3
"""prc-extract.py <recording.pipelines> <out-dir> | --selftest [<cache.cs.dxil>]

Unpacks a DXMTPRC1 pipeline recording (src/d3d12/d3d12_pipeline_record.{hpp,cpp}, DXMT_PIPELINE_RECORD) into dxil-translate's
capture layout (src/d3d12/d3d12_dxil_dump.cpp): <out-dir>/dxil/ holds the shaders with a DXIL part, <out-dir>/dxbc/ the others,
each as <role>-<fnv1a64>.dxil, the root signatures they use as rs-<fnv1a64>.bin, and a pipelines.txt with one
"cs cs=<h> rs=<h>" line per compute pipeline (graphics pipelines only contribute their shaders: their lines would need the
descriptor's formats, which this reader does not parse). Test tool, reads only; DXMTRS step 4 (corpora C10, C11).
"""
import os
import struct
import sys
import tempfile

MAGIC, HEADER, BLOB, GRAPHICS, COMPUTE = b"DXMTPRC1", 36, 1, 2, 3
MASK = 0xFFFFFFFFFFFFFFFF
ZERO = bytes(20)


def fnv1a(data):  # Checksum and CaptureHash are the same FNV-1a 64
    h = 0xCBF29CE484222325
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & MASK
    return h


def has_dxil(blob):  # a container with a DXIL part (d3d12_dxil_dump.cpp's DXILStage)
    if len(blob) < 32 or blob[:4] != b"DXBC":
        return False
    for i in range(struct.unpack_from("<I", blob, 28)[0]):
        if 32 + 4 * (i + 1) > len(blob):
            break
        off = struct.unpack_from("<I", blob, 32 + 4 * i)[0]
        if off + 12 <= len(blob) and blob[off:off + 4] == b"DXIL":
            return True
    return False


def read(path):  # Read() of d3d12_pipeline_record.cpp: (blobs {id: bytes}, [(kind, payload)], skipped)
    data = open(path, "rb").read()
    blobs, pipelines, bad, at = {}, [], 0, len(MAGIC)
    if data[:len(MAGIC)] != MAGIC:
        raise SystemExit("prc-extract: %s is not a DXMTPRC1 recording" % path)
    while at < len(data):
        if len(data) - at < HEADER:
            bad += 1
            break
        kind, size, total = struct.unpack_from("<IIQ", data, at)
        rid = data[at + 16:at + 36]
        if size > len(data) - at - HEADER:  # cut short
            bad += 1
            break
        payload = data[at + HEADER:at + HEADER + size]
        at += HEADER + size
        if kind < BLOB or kind > COMPUTE or fnv1a(payload) != total:
            bad += 1
        elif kind == BLOB:
            blobs[rid] = payload
        else:
            pipelines.append((kind, payload))
    return blobs, pipelines, bad


def extract(recording, out):
    blobs, pipelines, bad = read(recording)
    folders, written, lines = {}, set(), {}
    for name in ("dxbc", "dxil"):
        folders[name] = os.path.join(out, name)
        os.makedirs(folders[name], exist_ok=True)
        lines[name] = []

    def put(folder, name, blob):
        if (folder, name) not in written:
            written.add((folder, name))
            open(os.path.join(folders[folder], name), "wb").write(blob)

    compute = graphics = dangling = 0
    for kind, payload in pipelines:
        count = 2 if kind == COMPUTE else 4  # root signature, then CS or VS, PS, GS
        if len(payload) < 20 * count:
            bad += 1
            continue
        ids = [payload[i:i + 20] for i in range(0, 20 * count, 20)]
        if any(i != ZERO and i not in blobs for i in ids):
            dangling += 1
            continue
        rs = blobs.get(ids[0])  # none: the shader carries its own ("rs=embedded", d3d12_pipeline.hpp)
        rs_name = "rs-%016x.bin" % fnv1a(rs) if rs is not None else None
        roles = ["cs"] if kind == COMPUTE else ["vs", "ps", "gs"]
        for role, sid in zip(roles, ids[1:]):
            if sid == ZERO:
                continue
            shader = blobs[sid]
            folder = "dxil" if has_dxil(shader) else "dxbc"
            name = "%s-%016x.dxil" % (role, fnv1a(shader))
            put(folder, name, shader)
            if rs is not None:
                put(folder, rs_name, rs)
            if kind == COMPUTE:
                lines[folder].append("cs cs=%s rs=%s\n" % (name[3:19], rs_name[3:19] if rs is not None else "embedded"))
        compute, graphics = compute + (kind == COMPUTE), graphics + (kind == GRAPHICS)
    for name in lines:
        open(os.path.join(folders[name], "pipelines.txt"), "w").writelines(lines[name])
    if dangling:
        print("prc-extract: %d pipelines name a blob the recording lacks (not written)" % dangling, file=sys.stderr)
    return "prc-extract: %d blobs, %d compute, %d graphics, %d skipped" % (len(blobs), compute, graphics, bad)


def selftest(dxil_path):
    def record(kind, payload, id_, sum_=None):
        return struct.pack("<IIQ", kind, len(payload), fnv1a(payload) if sum_ is None else sum_) + id_ + payload

    cs = open(dxil_path, "rb").read()  # a DXIL compute shader
    rts0 = struct.pack("<6I", 1, 0, 24, 0, 24, 0)  # a root signature 1.0 with nothing in it
    rs = b"DXBC" + bytes(16) + struct.pack("<IIII", 1, 36 + 8 + len(rts0), 1, 36) + b"RTS0" + struct.pack("<I", len(rts0)) + rts0
    ids = {"cs": b"c" * 20, "rs": b"r" * 20, "dxbc": b"d" * 20}
    rec = MAGIC
    rec += record(BLOB, cs, ids["cs"]) + record(BLOB, rs, ids["rs"]) + record(BLOB, rs, ids["dxbc"])  # rs: no DXIL part
    rec += record(COMPUTE, ids["rs"] + ids["cs"] + bytes(8), b"1" * 20)
    rec += record(COMPUTE, ZERO + ids["dxbc"] + bytes(8), b"2" * 20)  # a DXBC shader with no root signature id
    rec += record(GRAPHICS, bytes(80), b"3" * 20, sum_=1)  # wrong checksum
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "game.pipelines")
        open(path, "wb").write(rec)
        summary = extract(path, os.path.join(tmp, "out"))
        cs_h, rs_h, dx_h = "%016x" % fnv1a(cs), "%016x" % fnv1a(rs), "%016x" % fnv1a(rs)
        want = {"dxil": ["cs-%s.dxil" % cs_h, "pipelines.txt", "rs-%s.bin" % rs_h],
                "dxbc": ["cs-%s.dxil" % dx_h, "pipelines.txt"]}
        for folder, names in want.items():
            assert sorted(os.listdir(os.path.join(tmp, "out", folder))) == sorted(names), (folder, os.listdir(os.path.join(tmp, "out", folder)))
        assert open(os.path.join(tmp, "out", "dxil", "pipelines.txt")).read() == "cs cs=%s rs=%s\n" % (cs_h, rs_h)
        assert open(os.path.join(tmp, "out", "dxbc", "pipelines.txt")).read() == "cs cs=%s rs=embedded\n" % dx_h
        assert summary == "prc-extract: 3 blobs, 2 compute, 0 graphics, 1 skipped", summary
    print("selftest ok")


if __name__ == "__main__":
    args = sys.argv[1:]
    if args[:1] == ["--selftest"] and len(args) <= 2:
        here = os.path.dirname(os.path.abspath(__file__))
        selftest(args[1] if len(args) == 2 else os.path.join(here, "..", "tests", "shaders", "cache.cs.dxil"))
    elif len(args) == 2:
        print(extract(*args))
    else:
        sys.exit(__doc__)

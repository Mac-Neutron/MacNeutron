import struct, sys, zlib
src, dst, grow = sys.argv[1], sys.argv[2], int(sys.argv[3])
f = open(src, 'rb'); h = f.read(512)
magic, ver, bfo, bfs, cbits, size, crypt, l1size, l1off = struct.unpack('>IIQIIQIIQ', h[:48])
assert magic == 0x514649fb and crypt == 0
ctype = 0
if ver >= 3:
    hlen = struct.unpack('>I', h[100:104])[0]
    incompat = struct.unpack('>Q', h[72:80])[0]
    if hlen > 104: ctype = h[104]
    assert incompat & ~0x8 == 0, hex(incompat)  # bit 3: compression type field
assert ctype == 0, 'zstd'
cs = 1 << cbits
f.seek(l1off); l1 = struct.unpack('>%dQ' % l1size, f.read(8 * l1size))
out = open(dst, 'wb'); out.truncate(max(size, grow))
l2n = cs // 8; x = 62 - (cbits - 8)
for i, e in enumerate(l1):
    l2off = e & 0x00fffffffffffe00
    if not l2off: continue
    f.seek(l2off); l2 = struct.unpack('>%dQ' % l2n, f.read(cs))
    for j, d in enumerate(l2):
        vo = (i * l2n + j) * cs
        if vo >= size or d == 0: continue
        if d & (1 << 62):
            desc = d & ((1 << 62) - 1)
            off = desc & ((1 << x) - 1); ns = ((desc >> x) & ((1 << (62 - x)) - 1)) + 1
            f.seek(off); data = zlib.decompressobj(-12).decompress(f.read(ns * 512 - (off & 511)))[:cs]
        else:
            off = d & 0x00fffffffffffe00
            if not off or (d & 1 and not off): continue
            f.seek(off); data = f.read(cs)
        if data.strip(b'\0'):
            out.seek(vo); out.write(data)
out.close(); print('ok', size)

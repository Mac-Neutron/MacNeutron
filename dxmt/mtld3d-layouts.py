# mtld3d's repr(C) structs as two builds' DWARF lays them out (dxmt/mtld3d-layout32-check.sh): reads
# `dwarfdump --debug-info` of the x86-64 and the i686 mtld3d.so, and the Rust sources naming the structs to compare,
# and prints each struct whose size or field offsets differ, then "<n> structs, <m> differ". A declared struct DWARF
# lacks (unused by mtld3d.so, or behind a cfg) is listed as absent, and counts as differing when it is one of the shared
# crate's params (the PE/Unix calls'). Alignment alone DWARF doesn't say: the crates' own layout tests check it (the
# D3D9 gate runs them for i686). A source file is named with the crate and the folder its modules start from, as
# <crate>=<src folder>=<file>.
#   python3 -I mtld3d-layouts.py <x86-64 dump> <i686 dump> <crate=src=file...>
import re
import sys

DIE = re.compile(r'^0x[0-9a-f]+:( +)(DW_TAG_\w+)')
ATTR = re.compile(r'^\s+(DW_AT_\w+)\s+\((.*)\)$')
CRATES = ('mtld3d_shared::', 'mtld3d_mtl::')
# Compared by size: a pointer and its zero high half in a 32-bit build (WMTMemoryPointer's insides).
SIZE_ONLY = {'mtld3d_mtl::wmt::Ptr'}


def layouts(path):
    """{qualified struct name: (size, ((member, offset), ...))} of the structs under CRATES."""
    out, stack, cur, die = {}, [], None, None

    def close():
        nonlocal cur
        if cur and not cur['variant'] and cur['name'].startswith(CRATES):
            out.setdefault(cur['name'], (cur['size'], tuple(cur['members'])))
        cur = None

    for line in open(path, errors='replace'):
        m = DIE.match(line)
        if m:
            depth, tag = len(m.group(1)), m.group(2)
            while stack and stack[-1][0] >= depth:
                stack.pop()
            if cur and depth <= cur['depth']:
                close()
            stack.append([depth, tag, None])
            die = {'depth': depth, 'tag': tag, 'name': None}
            if tag == 'DW_TAG_variant_part' and cur:
                cur['variant'] = True
            continue
        a = ATTR.match(line)
        if not a or not die:
            continue
        key, value = a.groups()
        member = cur and die['tag'] == 'DW_TAG_member' and die['depth'] == cur['depth'] + 2
        if key == 'DW_AT_name':
            die['name'] = stack[-1][2] = value.strip('"')
            if member:
                cur['members'].append([die['name'], None])
        elif key == 'DW_AT_byte_size' and die['tag'] == 'DW_TAG_structure_type' and (not cur or die['depth'] <= cur['depth']):
            close()
            ns = '::'.join(s[2] for s in stack[:-1] if s[1] == 'DW_TAG_namespace' and s[2])
            cur = {'name': ns + '::' + (die['name'] or '?'), 'size': int(value, 0), 'members': [],
                   'depth': die['depth'], 'variant': False}
        elif key == 'DW_AT_data_member_location' and member and cur['members']:
            cur['members'][-1][1] = int(value, 0)
    close()
    return {k: (s, tuple(tuple(m) for m in ms)) for k, (s, ms) in out.items()}


def repr_c(files):
    """The qualified names of the structs the sources declare #[repr(C...)]."""
    names = set()
    for spec in files:
        crate, src, f = spec.split('=', 2)
        mod = f[len(src):].lstrip('/').removesuffix('.rs').removesuffix('/mod')
        prefix = '::'.join([crate] + [p for p in mod.split('/') if p and p != 'lib'])
        pending = False
        for line in open(f):
            s = line.strip()
            if s.startswith('#[repr(C'):
                pending = True
            elif pending:
                m = re.match(r'(?:pub(?:\([^)]*\))? )?struct (\w+)', s)
                if m:
                    names.add(prefix + '::' + m.group(1))
                    pending = False
                elif not (s.startswith('#[') or s.startswith('//')):
                    pending = False
    return names


def base(name):
    return re.sub(r'<.*>$', '', name)


wide, narrow = layouts(sys.argv[1]), layouts(sys.argv[2])
names = repr_c(sys.argv[3:])
checked = differ = 0
found = {base(k) for k in set(wide) & set(narrow)}
for n in sorted(names - found):
    must = n.startswith('mtld3d_shared::params::') and not n.endswith('::U64After4')  # the probe's own struct
    print('absent%s: %s' % (' (a params struct)' if must else '', n))
    differ += must
for k in sorted(set(wide) & set(narrow)):
    if base(k) not in names:
        continue
    checked += 1
    if (wide[k][0] != narrow[k][0]) if base(k) in SIZE_ONLY else (wide[k] != narrow[k]):
        differ += 1
        print(k)
        print('  x86-64: size %d %s' % (wide[k][0], ' '.join('%s@%s' % m for m in wide[k][1])))
        print('  i686:   size %d %s' % (narrow[k][0], ' '.join('%s@%s' % m for m in narrow[k][1])))
print('%d structs, %d differ' % (checked, differ))

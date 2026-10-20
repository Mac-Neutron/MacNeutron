#!/usr/bin/env python3
"""Generates the Steam forwarding descriptor table from lsteamclient's sources (spec §5.9).

    gen.py <lsteamclient dir> <out dir>

Writes <out>/descriptors.c (one mn_call_desc per entry of the 64-bit __wine_unix_call_funcs table, in table order,
sized and offset by the compiler on each side, and its WoW64 twin from the wow64_ params, whose W32_PTR pointers are
4 bytes: MN_PTR32), <out>/guest_table.c (the guest's forwarding tables) and <out>/report.txt (shape counts, every buffer and its count field,
every unsupported call with the reason). The rules, which the report lists:

  - `const char *` is a string in, unless a byte count (cb*, cub*) follows it: then a buffer in.
  - `char *`, `void *`, `const void *` and pointers to bytes are buffers when a count field follows within two
    fields, before any other pointer (name starting cb/cch/cub, or containing size/max/len/count/bytes/num), or when
    the next pointer is such a count's `uint32_t *` (in and out: ISteamInventory's property calls); otherwise
    unsupported.
  - A pointer to any other complete type is that type, copied both ways (in only when const), or an array of it when a
    count field follows, or a count behind the next pointer (`uint32_t *`, in and out: ISteamInventory's
    GetResultItems). Parallel arrays (a name with Array or Prices: ISteamInventory's GenerateItems, ExchangeItems,
    TradeItems, StartPurchase, GetItemsWithPrices) take the first count after the run of pointers that follows them.
    One element where Steam writes or reads many is a host heap overflow.
  - Pointers to pointers, pointees that hold pointers, and `void *_ret` are unsupported.
  - `struct u_iface`, `struct u_buffer`, function pointers and interface objects (`w_I…`, `w32_I…`, `struct w_iface *`) are host
    or guest values carried as they are: not described.
OVERRIDES fixes or adds fields by call; CHECKED lists the calls whose description was reviewed by hand (Task 6).
"""
import os
import re
import sys

INT = re.compile(r'^(const\s+)?(u?int(8|16|32|64)_t|int|unsigned( int)?|bool|uint32|int32|uint64|int64|size_t|'
                 r'HAuthTicket|SteamAPICall_t|AppId_t|DepotId_t|HSteamPipe|HSteamUser)$')
COUNT_NAME = re.compile(r'^(cb|cch|cub|cc)|size|max|len|count|bytes|num', re.I)
BYTE_TYPES = {'char', 'void', 'uint8_t', 'int8_t', 'unsigned char'}

# call name -> field name -> description (a dict of mn_field values as C expressions) or None for "not described".
OVERRIDES = {
    # The host keeps its own path buffer (only ISteamHTMLSurface callbacks use it; their URLs are not carried).
    'steamclient_init': {'g_tmppath': None},
    # The guest's destination buffer, as long as the host buffer `buf` names.
    'steamclient_get_unix_buffer': {'ptr': {'kind': 'MN_BUF', 'size': '1',
                                            'size_offset': 'offsetof(struct steamclient_get_unix_buffer_params, buf) + 8',
                                            'size_width': '8'}},
    # Callback records hold the guest's own function and object pointers, which the host only carries: bytes.
    'steamclient_next_callback': {'callback': {'kind': 'MN_BUF', 'size': '1',
                                               'size_offset': 'offsetof(struct steamclient_next_callback_params, size)',
                                               'size_width': '4'}},
    # The message header: the host fills id, user and size; m_pubParam stays the guest's value.
    'steamclient_Steam_BGetCallback': {'w_msg': {'kind': 'MN_FIXED', 'size': 'sizeof(w_CallbackMsg_t)'}},
    # The callback's data goes into the buffer the guest allocated for it (m_pubParam, m_cubParam bytes).
    'steamclient_callback_message_receive': {'w_msg': {
        'kind': 'MN_FIXED', 'flags': 'MN_INNER', 'size': 'sizeof(w_CallbackMsg_t)',
        'inner_ptr': 'offsetof(w_CallbackMsg_t, m_pubParam)', 'inner_size': 'offsetof(w_CallbackMsg_t, m_cubParam)'}},
}
CHECKED = set()
# Calls the Windows half makes with NULL params.
NO_PARAMS = {'steamclient_init_registry'}


def strip_comments(s):
    return re.sub(r'/\*.*?\*/', '', re.sub(r'//[^\n]*', '', s), flags=re.S)


def parse_structs(text):
    """name -> list of field declarations (strings without ';'), top-level fields only."""
    out = {}
    for m in re.finditer(r'struct\s+(\w+)\s*\{', text):
        name, i, depth = m.group(1), m.end(), 1
        start = i
        while depth and i < len(text):
            depth += {'{': 1, '}': -1}.get(text[i], 0)
            i += 1
        body = text[start:i - 1]
        fields, cur, d = [], '', 0
        for ch in body:
            if ch in '({[':
                d += 1
            elif ch in ')}]':
                d -= 1
            if ch == ';' and d == 0:
                if cur.strip():
                    fields.append(' '.join(cur.split()))
                cur = ''
            else:
                cur += ch
        out.setdefault(name, fields)
    return out


def field_name(decl):
    m = re.search(r'\(\s*\*\s*(?:W_CDECL\s+|W_STDCALL\s+)?(\w+)\s*\)', decl)  # function pointer or pointer to array
    if m:
        return m.group(1), decl.replace(m.group(0), '(*)')
    m = re.search(r'(\w+)\s*(\[[^\]]*\])?\s*$', decl)
    return m.group(1), decl[:m.start()].strip() + (m.group(2) or '')


def w32_decl(field):
    """The declaration inside lsteamclient's W32_PTR(decl, name, type) (a WoW64 struct's 4-byte pointer), else None."""
    m = re.match(r'^W32_PTR\s*\((.*)\)$', field.strip())
    if not m:
        return None
    args, depth, cur = [], 0, ''
    for ch in m.group(1):
        depth += {'(': 1, ')': -1}.get(ch, 0)
        if ch == ',' and depth == 0:
            args.append(cur); cur = ''
        else:
            cur += ch
    return args[0].strip()


def wow64_overrides(name):
    """OVERRIDES for the WoW64 table: its params and callback structs, its pointers 4 bytes. The guest answers
    steamclient_get_unix_buffer itself (client.c), so the host never runs it."""
    if name == 'steamclient_get_unix_buffer':
        return {'ptr': {'kind': 'MN_UNSUPPORTED'}}
    out = {}
    for fname, d in OVERRIDES.get(name, {}).items():
        if d is not None:
            d = {k: v.replace(f'struct {name}_params', f'struct wow64_{name}_params').replace('w_CallbackMsg_t', 'w32_CallbackMsg_t')
                 for k, v in d.items()}
            d['flags'] = 'MN_PTR32' if d.get('flags', '0') == '0' else d['flags'] + ' | MN_PTR32'
        out[fname] = d
    return out


def main():
    src, out_dir = sys.argv[1], sys.argv[2]
    texts = {f: strip_comments(open(os.path.join(src, f), errors='replace').read())
             for f in os.listdir(src) if f.endswith('.h')}
    structs = {}
    for t in texts.values():
        for k, v in parse_structs(t).items():
            structs.setdefault(k, v)
    table = strip_comments(open(os.path.join(src, 'unixlib_generated.cpp')).read())
    m = re.search(r'__wine_unix_call_funcs\[\]\s*=\s*\{(.*?)\};', table, re.S)
    names = [n.strip() for n in m.group(1).split(',') if n.strip()]

    # Struct types whose bodies hold pointers can't be copied as bytes.
    typedefs = {}
    for t in texts.values():
        for td in re.finditer(r'typedef\s+(?:struct\s+)?(\w+)\s+(\w+)\s*;', t):
            typedefs[td.group(2)] = td.group(1)

    def holds_pointers(tname, seen=()):
        tname = typedefs.get(tname, tname)
        body = structs.get(tname)
        if body is None or tname in seen:
            return False
        return any('*' in f or any(holds_pointers(w, seen + (tname,)) for w in re.findall(r'\w+', f)) for f in body)

    tables = {}
    for wow64 in (False, True):
        descs, report, counts = [], [], {}
        for idx, name in enumerate(names):
            sname = ('wow64_' if wow64 else '') + name + '_params'
            fields = structs.get(sname)
            if fields is None:
                if name in NO_PARAMS:
                    descs.append((name, None, [], []))
                else:
                    descs.append((name, sname, None, ['no params struct']))
                continue
            decls = [w32_decl(f) for f in fields]
            ptr32s = [d is not None for d in decls]
            parsed = [field_name(d if d is not None else f) for d, f in zip(decls, fields)]
            out, reasons = [], []
            for i, (fname, ftype) in enumerate(parsed):
                ov = wow64_overrides(name) if wow64 else OVERRIDES.get(name, {})
                if fname in ov:
                    if ov[fname] is not None:
                        out.append((fname, ov[fname]))
                        counts['override'] = counts.get('override', 0) + 1
                    continue
                t = ftype.replace('struct ', '').strip()
                if t == 'u_buffer' and fname == '_ret':
                    # A host pointer and length (a string Steam returned): the bytes come back as a guest copy.
                    out.append((fname, {'kind': 'MN_RET_BUF'}))
                    counts['returned buffer'] = counts.get('returned buffer', 0) + 1
                    continue
                if '(*)' in t and '(' in t.split('(*)')[1][:2]:
                    continue  # function pointer: a guest value the host carries back
                if '*' not in t:
                    continue
                if t.startswith(('u_iface', 'u_buffer')) or re.match(r'(const )?w(32)?_I\w+ \*$', t) or t.startswith('w_iface'):
                    continue
                const = t.startswith('const ')
                base = t[6:] if const else t
                stars = base.count('*')
                pointee = base.replace('*', '').strip()
                flags = 'MN_IN_ONLY' if const else '0'
                if ptr32s[i]:
                    flags = 'MN_PTR32' if flags == '0' else flags + ' | MN_PTR32'
                # A W32_PTR field is an opaque struct ptr32 in C: its pointee is sized by the type it names.
                ptype = re.sub(r'^const\s+', '', ftype.strip())
                ptype = ptype[:-1].strip() if ptype.endswith('*') else ptype
                count = count_ptr = None
                for j in range(i + 1, min(i + 3, len(parsed))):
                    cname, ctype = parsed[j]
                    if '*' in ctype:
                        if ctype.replace(' ', '') == 'uint32_t*' and COUNT_NAME.search(cname):
                            count_ptr = cname  # the count behind a pointer, which the callee rewrites
                        break  # a count after the next pointer belongs to that pointer
                    if INT.match(ctype.replace('struct ', '').strip()) and COUNT_NAME.search(cname):
                        count = cname
                        break
                if not count and not count_ptr and re.search(r'Array|Prices', fname):
                    for cname, ctype in parsed[i + 1:]:
                        if '*' in ctype:
                            continue  # another of the parallel arrays
                        if INT.match(ctype.replace('struct ', '').strip()) and COUNT_NAME.search(cname):
                            count = cname
                        break
                access = f'((struct {sname} *)0)->{fname}'
                size_of = f'sizeof({ptype})' if ptr32s[i] else f'sizeof(*{access})'
                if stars > 1 and '(*)' not in t:
                    out.append((fname, {'kind': 'MN_UNSUPPORTED'})); reasons.append(f'{fname}: pointer to pointer')
                    kind = 'unsupported'
                elif fname == '_ret' and pointee == 'void':
                    out.append((fname, {'kind': 'MN_UNSUPPORTED'})); reasons.append('_ret: returns a host pointer')
                    kind = 'unsupported'
                elif typedefs.get(pointee, pointee) not in structs and re.match(r'(w_)?Steam\w*$', pointee) \
                        and not any(re.search(r'typedef[^;]*\b' + pointee + r'\s*;', t) and '{' in t for t in ()):
                    tn = re.escape(typedefs.get(pointee, pointee))
                    if any(re.search(r'\b(struct\s+)?' + tn + r'\s*\{', t) or re.search(r'\}\s*' + tn + r'\s*;', t)
                           for t in texts.values()):
                        out.append((fname, {'kind': 'MN_FIXED', 'flags': flags, 'size': size_of}))
                        kind = 'fixed'
                    else:
                        out.append((fname, {'kind': 'MN_UNSUPPORTED'})); reasons.append(f'{fname}: {pointee} is incomplete')
                        kind = 'unsupported'
                elif holds_pointers(pointee):
                    out.append((fname, {'kind': 'MN_UNSUPPORTED'})); reasons.append(f'{fname}: {pointee} holds pointers')
                    kind = 'unsupported'
                elif pointee == 'char' and const and not (count and count.lower().startswith(('cb', 'cub'))):
                    out.append((fname, {'kind': 'MN_STR_IN', 'flags': flags}))
                    kind = 'string'
                elif pointee in BYTE_TYPES and '(*)' not in t:
                    if count:
                        out.append((fname, {'kind': 'MN_BUF', 'flags': flags, 'size': '1',
                                            'size_offset': f'offsetof(struct {sname}, {count})',
                                            'size_width': f'sizeof(((struct {sname} *)0)->{count})'}))
                        report.append(f'buffer {name}.{fname} <- {count}')
                        kind = 'buffer'
                    elif count_ptr:
                        out.append((fname, {'kind': 'MN_BUF', 'flags': 'MN_COUNT_PTR' if flags == '0' else
                                            flags + ' | MN_COUNT_PTR', 'size': '1',
                                            'size_offset': f'offsetof(struct {sname}, {count_ptr})', 'size_width': '4'}))
                        report.append(f'buffer {name}.{fname} <- *{count_ptr}')
                        kind = 'buffer'
                    elif pointee in ('uint8_t', 'int8_t'):
                        out.append((fname, {'kind': 'MN_FIXED', 'flags': flags, 'size': '1'}))
                        kind = 'fixed'
                    else:
                        out.append((fname, {'kind': 'MN_UNSUPPORTED'})); reasons.append(f'{fname}: {t} with no count field')
                        kind = 'unsupported'
                elif count and '(*)' not in t:
                    out.append((fname, {'kind': 'MN_BUF', 'flags': flags, 'size': size_of,
                                        'size_offset': f'offsetof(struct {sname}, {count})',
                                        'size_width': f'sizeof(((struct {sname} *)0)->{count})'}))
                    report.append(f'array  {name}.{fname} <- {count}')
                    kind = 'array'
                elif count_ptr and '(*)' not in t:
                    out.append((fname, {'kind': 'MN_BUF', 'flags': 'MN_COUNT_PTR' if flags == '0' else
                                        flags + ' | MN_COUNT_PTR', 'size': size_of,
                                        'size_offset': f'offsetof(struct {sname}, {count_ptr})', 'size_width': '4'}))
                    report.append(f'array  {name}.{fname} <- *{count_ptr}')
                    kind = 'array'
                else:
                    out.append((fname, {'kind': 'MN_FIXED', 'flags': flags, 'size': size_of}))
                    kind = 'fixed'
                counts[kind] = counts.get(kind, 0) + 1
            descs.append((name, sname, out, reasons))
        tables[wow64] = (descs, report, counts)
    descs, report, counts = tables[False]

    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, 'descriptors.c'), 'w') as f:
        f.write('/* Generated by eac-vm/steam-forward/gen.py from lsteamclient: do not edit. */\n'
                '#include <stddef.h>\n#include "unixlib.h" /* includes unixlib_generated.h */\n#include "marshal.h"\n\n'
                'static const struct mn_field mn_unsupported_field[] = { { 0, MN_UNSUPPORTED } };\n\n')
        # The 64-bit table (f<idx>, mn_calls) and the WoW64 one (w<idx>, mn_calls_wow64): same calls, same order.
        for prefix, table, wow64 in (('f', 'mn_calls', False), ('w', 'mn_calls_wow64', True)):
            tdescs = tables[wow64][0]
            for idx, (name, sname, out, _) in enumerate(tdescs):
                if not out:
                    continue
                f.write(f'static const struct mn_field {prefix}{idx}[] = {{\n')
                for fname, d in out:
                    vals = [f'offsetof(struct {sname}, {fname})', d['kind'], d.get('flags', '0'), d.get('size', '0'),
                            d.get('size_offset', '0'), d.get('size_width', '0'), '0', d.get('inner_ptr', '0'),
                            d.get('inner_size', '0')]
                    f.write('    { ' + ', '.join(vals) + ' },\n')
                f.write('};\n')
            f.write(f'\nconst struct mn_call_desc {table}[] = {{\n')
            for idx, (name, sname, out, _) in enumerate(tdescs):
                if out is None:
                    f.write(f'    {{ "{name}", 0, 1, mn_unsupported_field }},\n')
                elif sname is None:
                    f.write(f'    {{ "{name}", 0, 0, NULL }},\n')
                else:
                    f.write(f'    {{ "{name}", sizeof(struct {sname}), {len(out)}, {f"{prefix}{idx}" if out else "NULL"} }},\n')
            f.write('};\n')
        f.write('const unsigned int mn_ncalls = sizeof(mn_calls) / sizeof(mn_calls[0]);\n')
    # The guest's Unix-call tables: each entry forwards its own index, the WoW64 ones with their 32-bit params; the
    # guest answers steamclient_get_unix_buffer's WoW64 call itself (client.c).
    with open(os.path.join(out_dir, 'guest_table.c'), 'w') as f:
        f.write('/* Generated by eac-vm/steam-forward/gen.py: do not edit. */\n'
                '#include "unixlib.h"\n#include "client.h"\n\n'
                'NTSTATUS mn_wow64_get_unix_buffer(void *params);\n')
        for idx, (name, _, _, _) in enumerate(descs):
            f.write(f'static NTSTATUS f{idx}(void *p) {{ return mn_client_call({idx}, p); }}\n')
            if name != 'steamclient_get_unix_buffer':
                f.write(f'static NTSTATUS w{idx}(void *p) {{ return mn_client_call_wow64({idx}, p); }}\n')
        f.write('\nconst unixlib_entry_t __wine_unix_call_funcs[] = {\n')
        f.write(''.join(f'    f{idx},\n' for idx in range(len(descs))))
        f.write('};\nconst unixlib_entry_t __wine_unix_call_wow64_funcs[] = {\n')
        f.write(''.join('    mn_wow64_get_unix_buffer,\n' if name == 'steamclient_get_unix_buffer' else f'    w{idx},\n'
                        for idx, (name, _, _, _) in enumerate(descs)))
        f.write('};\n')
    with open(os.path.join(out_dir, 'report.txt'), 'w') as f:
        for label, wow64 in (('', False), ('wow64 ', True)):
            tdescs, treport, tcounts = tables[wow64]
            unsupported = [(n, r) for n, _, o, r in tdescs if r]
            f.write(f'{label}calls {len(tdescs)}, unsupported {len(unsupported)}\n')
            for k in sorted(tcounts):
                f.write(f'{label}fields {k} {tcounts[k]}\n')
            f.write('\n'.join(sorted(treport)) + '\n\n')
            for n, r in unsupported:
                f.write(f'{label}unsupported {n}: {"; ".join(r)}\n')
    for label, wow64 in (('', False), ('wow64: ', True)):
        tdescs, _, tcounts = tables[wow64]
        print(f'{label}{len(tdescs)} calls, {len([1 for d in tdescs if d[3]])} unsupported; fields: {tcounts}')

if __name__ == '__main__':
    main()

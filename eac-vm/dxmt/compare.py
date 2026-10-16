#!/usr/bin/env python3
"""The 4b-2 gate's comparison (plan Global Constraints, "The comparison rule"), fixed before any run.

compare.py <name> <macOS run 1> <macOS run 2> <guest run> <prefix>...
    A program's lines are those starting with one of its prefixes. The two macOS runs must give the same number;
    a position where they agree is required, one where they differ (a timing, a counter) is masked. The guest must give
    that many lines and equal the macOS runs at every required position. No required line is a failure. Each run's
    exit status (<file>.status beside it, if there) must be the same; "timeout" is a timeout. A guest line from the
    guest winemetal.so ("mn-winemetal: ...", a refused call) is a failure.
compare.py host <host output>
    The VM host's own error lines fail the gate: the executor's (each failed queued call is one) and the host
    winemetal forwarder's.
compare.py self-test
    Each rule's failure case must fail, and an agreeing run pass.
Prints "ok ..." or "FAIL ...", and exits 1 on a failure.
"""
import os
import sys
import tempfile


def lines(path, prefixes):
    with open(path, errors='replace') as f:
        return [l.rstrip('\r\n') for l in f if l.startswith(tuple(prefixes))]


def status(path):
    p = path[:-4] + '.status' if path.endswith('.txt') else path + '.status'
    try:
        with open(p) as f:
            return f.read().strip()
    except FileNotFoundError:
        return None


def compare(name, ref1, ref2, guest, prefixes):
    s1, s2, sg = status(ref1), status(ref2), status(guest)
    if sg == 'timeout':
        return f'FAIL {name}: timeout'
    if s1 != s2:
        return f'FAIL {name}: the macOS runs exited {s1} and {s2}'
    if sg != s1:
        return f'FAIL {name}: the guest exited {sg}, macOS {s1}'
    refused = lines(guest, ['mn-winemetal:'])
    if refused:
        return f'FAIL {name}: {refused[0]}'
    r1, r2, g = lines(ref1, prefixes), lines(ref2, prefixes), lines(guest, prefixes)
    if len(r1) != len(r2):
        return f'FAIL {name}: the macOS runs differ in their lines ({len(r1)}, {len(r2)})'
    required = [i for i in range(len(r1)) if r1[i] == r2[i]]
    if not required:
        return f'FAIL {name}: no required lines ({len(r1)} lines, all masked)'
    if len(g) != len(r1):
        return f'FAIL {name}: the guest gave {len(g)} lines, macOS {len(r1)}; first guest lines: {g[:3]}'
    for i in required:
        if g[i] != r1[i]:
            return f'FAIL {name}: line {i + 1}: guest [{g[i]}], macOS [{r1[i]}]'
    return f'ok   {name}: {len(required)} of {len(r1)} lines required, all the same'


def host(path):
    with open(path, errors='replace') as f:
        bad = [l.rstrip('\n') for l in f if l.startswith(('mn-executor: error', 'mn-winemetal:'))]
    return f'FAIL host: {len(bad)} error lines, first [{bad[0]}]' if bad else 'ok   host: no error lines'


def self_test():
    d = tempfile.mkdtemp()

    def f(name, text, st='0'):
        p = os.path.join(d, name + '.txt')
        with open(p, 'w') as out:
            out.write(text)
        if st is not None:
            with open(os.path.join(d, name + '.status'), 'w') as out:
                out.write(st + '\n')
        return p
    ref = 'wine: noise\ncopy a ok 1\ncopy b ok 2\ntiming 3.5\n'
    cases = [
        ('agree', f('r1', ref), f('r2', ref.replace('3.5', '4.1')), f('g', 'other\r\ncopy a ok 1\r\ncopy b ok 2\ntiming 9\n'), 'ok'),
        ('changed', f('c1', ref), f('c2', ref), f('cg', 'copy a ok 1\ncopy b ok 3\ntiming 3.5\n'), 'FAIL'),
        ('all-masked', f('m1', 'copy 1\n'), f('m2', 'copy 2\n'), f('mg', 'copy 1\n'), 'FAIL'),
        ('missing', f('s1', ref), f('s2', ref.replace('3.5', '4.1')), f('sg', 'copy a ok 1\ncopy b ok 2\n'), 'FAIL'),
        ('refs-differ', f('d1', ref), f('d2', 'copy a ok 1\n'), f('dg', ref), 'FAIL'),
        ('timeout', f('t1', ref), f('t2', ref), f('tg', ref, 'timeout'), 'FAIL timeout: timeout'),
        ('exit-status', f('e1', ref), f('e2', ref), f('eg', ref, '137'), 'FAIL'),
        ('refused', f('u1', ref), f('u2', ref), f('ug', ref + 'mn-winemetal: X (slot 47) is refused: 0xc0000002\n'), 'FAIL'),
        ('no-status', f('n1', ref, None), f('n2', ref, None), f('ng', ref, None), 'ok'),
    ]
    failures = 0
    for name, r1, r2, g, want in cases:
        got = compare(name, r1, r2, g, ['copy', 'timing'])
        if not got.startswith(want):
            print(f'FAIL self-test {name}: {got}')
            failures += 1
    for name, text, want in [('host-clean', 'mn-executor: context 1\nmn-executor: context 1 gone\n', 'ok'),
                             ('host-error', 'mn-executor: context 1\nmn-executor: error: asynchronous op failed (4114, 3221225485)\n', 'FAIL'),
                             ('host-winemetal', 'mn-winemetal: slot 18 raised NSInvalidArgumentException\n', 'FAIL')]:
        got = host(f(name, text, None))
        if not got.startswith(want):
            print(f'FAIL self-test {name}: {got}')
            failures += 1
    print('self-test: ' + ('FAIL' if failures else f'ok ({len(cases) + 3} cases)'))
    return failures == 0


if __name__ == '__main__':
    a = sys.argv[1:]
    if a == ['self-test']:
        sys.exit(0 if self_test() else 1)
    if len(a) == 2 and a[0] == 'host':
        out = host(a[1])
    elif len(a) >= 5:
        out = compare(a[0], a[1], a[2], a[3], a[4:])
    else:
        sys.exit(__doc__)
    print(out)
    sys.exit(0 if out.startswith('ok') else 1)

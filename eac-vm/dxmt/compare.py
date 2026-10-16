#!/usr/bin/env python3
"""The 4b-2 gate's comparison (plan Global Constraints, "The comparison rule"), fixed before any run.

compare.py <name> <macOS run 1> <macOS run 2> <guest run> <prefix>...
    A program's lines are those starting with one of its prefixes. The two macOS runs must give the same number;
    a position where they agree is required, one where they differ (a timing, a counter) is masked. The guest must give
    that many lines and equal the macOS runs at every required position. No required line is a failure.
compare.py host <host output>
    The VM host's own error lines (the executor's, the host winemetal forwarder's) fail the gate.
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


def compare(name, ref1, ref2, guest, prefixes):
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

    def f(name, text):
        p = os.path.join(d, name)
        with open(p, 'w') as out:
            out.write(text)
        return p
    ref = 'wine: noise\ncopy a ok 1\ncopy b ok 2\ntiming 3.5\n'
    cases = [
        ('agree', f('r1', ref), f('r2', ref.replace('3.5', '4.1')), f('g', 'other\r\ncopy a ok 1\r\ncopy b ok 2\ntiming 9\n'), 'ok'),
        ('changed', f('c1', ref), f('c2', ref), f('cg', 'copy a ok 1\ncopy b ok 3\ntiming 3.5\n'), 'FAIL'),
        ('all-masked', f('m1', 'copy 1\n'), f('m2', 'copy 2\n'), f('mg', 'copy 1\n'), 'FAIL'),
        ('missing', f('s1', ref), f('s2', ref), f('sg', 'copy a ok 1\ntiming 3.5\n'), 'FAIL'),
        ('refs-differ', f('d1', ref), f('d2', 'copy a ok 1\n'), f('dg', ref), 'FAIL'),
    ]
    failures = 0
    for name, r1, r2, g, want in cases:
        got = compare(name, r1, r2, g, ['copy', 'timing'])
        if not got.startswith(want):
            print(f'FAIL self-test {name}: {got}')
            failures += 1
    for name, text, want in [('host-clean', 'mn-executor: context 1\nmn-executor: context 1 gone\n', 'ok'),
                             ('host-error', 'mn-executor: context 1\nmn-winemetal: context 1: 2 queued calls failed\n', 'FAIL')]:
        got = host(f(name, text))
        if not got.startswith(want):
            print(f'FAIL self-test {name}: {got}')
            failures += 1
    print('self-test: ' + ('FAIL' if failures else f'ok ({len(cases) + 2} cases)'))
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

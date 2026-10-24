#!/usr/bin/env python3
"""Direct3D forwarding's coverage (spec §7, M2 Task 12), from MACNEUTRON_VM_D3D_TRACE=1's two traces.

coverage.py <guest trace> <host trace>
    Every method the guest's programs called (the guest trace: unix.c's, a "# process" section each) has a rule that
    forwards it and reached the host (the host trace: dispatch.c's, a "# context" section each). A line is
    "<id> <name> <calls>", and in the guest trace "<id> <name> <calls> refused|unhandled|answered" for a method the
    guest answered itself (rules.txt's refuse, a special without guest code, or by design: M4's dynamic maps, which
    need no host count): refused and unhandled fail (but AS_DXMT's), as does a forwarded method with no host count, or
    a guest trace with no method at all (nothing was forwarded).
coverage.py self-test
    Each rule's failure case must fail, and a covered run pass.
Prints "coverage: ok ..." or "coverage: FAIL ...", and exits 1 on a failure.
"""
import os
import sys
import tempfile


# Refused in the guest with the E_NOTIMPL native DXMT answers itself (rules.txt: "the ones DXMT answers E_NOTIMPL
# itself"): the program gets what forwarding would give it (SMITE 2 calls OpenExistingHeapFromAddress, M3 Task 10).
AS_DXMT = {'ID3D12Device::OpenSharedHandleByName', 'ID3D12Device3::OpenExistingHeapFromAddress',
           'ID3D12Device5::CreateStateObject', 'ID3D12Device7::AddToStateObject',
           # List3 to List7's methods behind caps DXMT reports off (rules.txt)
           'ID3D12GraphicsCommandList3::SetProtectedResourceSession', 'ID3D12GraphicsCommandList4::InitializeMetaCommand',
           'ID3D12GraphicsCommandList4::ExecuteMetaCommand', 'ID3D12GraphicsCommandList4::BuildRaytracingAccelerationStructure',
           'ID3D12GraphicsCommandList4::EmitRaytracingAccelerationStructurePostbuildInfo',
           'ID3D12GraphicsCommandList4::CopyRaytracingAccelerationStructure', 'ID3D12GraphicsCommandList4::SetPipelineState1',
           'ID3D12GraphicsCommandList4::DispatchRays', 'ID3D12GraphicsCommandList5::RSSetShadingRate',
           'ID3D12GraphicsCommandList5::RSSetShadingRateImage', 'ID3D12GraphicsCommandList6::DispatchMesh',
           'ID3D12Device9::CreateShaderCacheSession', 'ID3D12Device9::ShaderCacheControl'}


def read(path):
    """{id: [name, calls, why]}, summed over the sections; why is None for a forwarded method."""
    out = {}
    try:
        f = open(path, errors='replace')
    except FileNotFoundError:
        return out
    with f:
        for line in f:
            w = line.split()
            if len(w) < 3 or w[0].startswith('#'):
                continue
            e = out.setdefault(int(w[0]), [w[1], 0, None])
            e[1] += int(w[2])
            if len(w) > 3:
                e[2] = w[3]
    return out


def coverage(guest, host):
    g, h = read(guest), read(host)
    if not g:
        return 'coverage: FAIL no method in the guest trace (nothing forwarded)'
    bad = [f'{name} {why}' if why else f'{name} never reached the host'
           for i, (name, n, why) in sorted(g.items())
           if (why and why != 'answered' and not (why == 'refused' and name in AS_DXMT))
           or (not why and not h.get(i, [0, 0])[1])]
    if bad:
        return f'coverage: FAIL {len(bad)} of {len(g)} methods: ' + ', '.join(bad[:5])
    return f'coverage: ok ({len(g)} methods, {sum(e[1] for e in g.values())} calls, all forwarded)'


def self_test():
    d = tempfile.mkdtemp()

    def f(name, text):
        p = os.path.join(d, name)
        with open(p, 'w') as out:
            out.write(text)
        return p
    guest = '# process 7\n20 ID3D12Device::CreateFence 2\n# process 8\n20 ID3D12Device::CreateFence 1\n4 D3D12CreateDevice 1\n'
    host = '# context 1\n20 ID3D12Device::CreateFence 4\n4 D3D12CreateDevice 1\n'
    cases = [
        ('covered', f('g1', guest), f('h1', host), 'coverage: ok (2 methods, 4 calls'),
        ('refused', f('g2', guest + '79 ID3D12GraphicsCommandList::SetPredication 1 refused\n'), f('h2', host),
         'coverage: FAIL 1 of 3 methods: ID3D12GraphicsCommandList::SetPredication refused'),
        ('not-reached', f('g3', guest), f('h3', '# context 1\n4 D3D12CreateDevice 1\n'),
         'coverage: FAIL 1 of 2 methods: ID3D12Device::CreateFence never reached'),
        ('no-host-trace', f('g4', guest), os.path.join(d, 'none'), 'coverage: FAIL 2 of 2'),
        ('empty', f('g5', '# process 9\n'), f('h5', host), 'coverage: FAIL no method'),
        ('as-dxmt', f('g6', guest + '159 ID3D12Device3::OpenExistingHeapFromAddress 3 refused\n'), f('h6', host),
         'coverage: ok (3 methods, 7 calls'),
        ('answered', f('g7', guest + '576 ID3D11DeviceContext::Map 40000 answered\n'), f('h7', host),
         'coverage: ok (3 methods, 40004 calls'),
    ]
    failures = 0
    for name, g, h, want in cases:
        got = coverage(g, h)
        if not got.startswith(want):
            print(f'FAIL self-test {name}: {got}')
            failures += 1
    print('self-test: ' + ('FAIL' if failures else f'ok ({len(cases)} cases)'))
    return failures == 0


if __name__ == '__main__':
    a = sys.argv[1:]
    if a == ['self-test']:
        sys.exit(0 if self_test() else 1)
    if len(a) != 2:
        sys.exit(__doc__)
    out = coverage(a[0], a[1])
    print(out)
    sys.exit(0 if out.startswith('coverage: ok') else 1)

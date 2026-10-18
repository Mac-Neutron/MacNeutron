# Direct3D forwarding M2: D3D12 and DXGI forwarding Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A guest program's D3D12 and DXGI calls run on native DXMT inside `macneutron-vm`: generated guest proxies
and host stubs over today's winemetal transport, with shared memory, mirrored fences and per-thread rings; the 19
headless D3D12 programs print through forwarding what they print on the macOS runtime.

**Architecture:** `eac-vm/d3d/gen.py` reads the widl-generated headers native DXMT compiles against and
`eac-vm/d3d/rules.txt`, and writes call tables in the winemetal marshaller's own form (`struct mn_wm_call`), guest
proxies and host stubs. The guest side is a plain PE `mnd3d.dll` (proxies; `d3d12.dll` and `dxgi.dll` forward their
exports to it), linked into the prefix's `system32` and chosen by `WINEDLLOVERRIDES`, reaching the transport through
the builtin `winemetal.dll` (a new export) and our guest `winemetal.so`; the host side is a dispatcher in
`macneutron-vm` that loads native DXMT on the first forwarded call. A loopback build links the same
proxies, marshaller and stubs into one Mac library, which M1's native test harness runs first.

**Tech Stack:** Python 3 (the generator, its tests), C (guest runtime, unix lib, marshaller), C++20 (host stubs, against
native DXMT's headers), llvm-mingw x86-64 (PE), the builder's x86-64 Linux toolchain (unix lib), meson-built native
DXMT (M1), POSIX sh.

**Spec:** `docs/superpowers/specs/2026-10-17-macneutron-vm-d3d-forwarding-design.md` (§4.2 to §4.7, §4.9, §6, §7,
§8 M2). Baseline and M1: research README §15, §17.

## Global Constraints

- Gate (spec §8 M2): the loopback test; the headless D3D12 programs through forwarding match macOS (the 19 rows of
  M1's gate, `compare.py`'s rule).
- Handles are the guest's choice; a creation record carries the new one. `AddRef` and `Release` count in the guest;
  only the last `Release` reaches the host, deferred until every ring that used the object is past it.
- `QueryInterface` is answered in the guest from the interfaces the host object reported.
- Cached in the guest after the first call: `GetDesc` and its siblings, `GetGPUVirtualAddress`,
  `GetCPUDescriptorHandleForHeapStart`, `GetGPUDescriptorHandleForHeapStart`, `GetDescriptorHandleIncrementSize`,
  `CheckFeatureSupport`.
- Asynchronous, batched on the calling thread's ring: every command-list recording call, descriptor writes
  (`Create*View`, `CreateSampler`, `CopyDescriptors*`), `ExecuteCommandLists`, `Signal`, `Present`. Synchronous:
  creations a game checks for failure, `Map`, and every method with output data.
- A call on one thread that uses an object created on another waits for that creation; `ExecuteCommandLists` waits,
  on the host, until the lists it names are fully recorded on their own rings.
- Mappable memory lives in guest-shared arenas; `Map` returns the guest's own address; GPU-only resources never touch
  guest RAM; descriptor handles, GPU virtual addresses and fence values pass through as opaque values.
- Failures (§6): a method without a rule stops the build; an interface the host object doesn't have is
  `E_NOINTERFACE`, logged; a malformed record stops its ring, logged; an arena that can't grow is `E_OUTOFMEMORY`.
  Out of scope (§11): `d3d12video`, `d3d11on12`, the SDK debug layers: `E_NOINTERFACE`.
- `MACNEUTRON_VM_D3D=guest` runs today's path (DXMT in the guest). Forwarding becomes the VM's default only when it
  ships (spec §9: after the last milestone; M5 ran second): in M2 it is `MACNEUTRON_VM_D3D=forward`, on in the gates.
- Anti-cheat unchanged: the game loads Wine builtin DLLs in the guest; nothing hooks or injects.
- DXMT fork changes are native-only, exported one commit at a time (format-patch, checked on the stable series), as
  M1's ledger rules. Use `/usr/bin/grep` in scripts; commits end with the session's trailer lines; a privacy scan
  before committing docs.

## Rulings made writing this plan

- **The generator reads the widl-generated headers, not the IDL:** native DXMT's own include set
  (`build/dxmt-native/install/include-native`: `d3d12/d3d12.h` and `directx/dxgi*.h`), whose C vtables
  (`typedef struct ID3D12FenceVtbl { ... }`) list every interface's methods in vtable order with their parameter
  types: 62 D3D12 and 44 DXGI interfaces. Parsing widl's output is reading the IDL as the compiler sees it, and the host
  calls exactly the vtables DXMT implements. DXMT's private interfaces (`IMTLD3D12DeviceExt`,
  `IMTLD3D12CommandListExt`, `IMTLD3D12TemporalScaler`, `IMTLDXGIAdapter`, ...: C++ in DXMT) are declared in
  `rules.txt` in the same prototype form.
- **Rules are inferred where a pointer is unambiguous**, and required elsewhere: a `const T *` of a struct with no
  pointers is in, `sizeof(T)`; a non-const `T *` is out; an interface pointer is a handle in, `T **` a handle out;
  `REFIID riid` + `void **` is a created object. Every other pointer (a `void *`, an array, a struct with pointers or
  `WCHAR`, an in-out) needs a rule, or the build stops: the spec's guarantee, with fewer hand-written lines.
- **One marshaller:** the generator emits `struct mn_wm_call` tables, and `eac-vm/metal/wm/marshal.c` packs and
  unpacks them, with two new field kinds: `WMF_WSTR_IN` (guest UTF-16 to native UTF-32) and `WMF_UNION` (a
  discriminant and one nested field list per value). Structs with `WCHAR` arrays (DXGI's adapter and output
  descriptions) are specials.
- **One guest module:** the proxies are in `mnd3d.dll`, and `d3d12.dll` and `dxgi.dll` forward their exports to it
  (the spec's two stand-ins, as forwarders): a D3D12 device made on a DXGI adapter, or a DXGI object asked of a D3D12
  one, must be the same module's proxies.
- **Native DLLs in the prefix, as Proton places DXVK:** Wine searches its own lib folder before any `WINEDLLPATH`
  entry (`dlls/ntdll/unix/loader.c`, `set_dll_path`), so builtin stand-ins couldn't sit beside guest DXMT's. The three
  are plain PE DLLs in `/opt/mn-wine/lib/wine/mn-d3d/`, which `run-game.sh` links into the prefix's `system32`; the
  mode is `WINEDLLOVERRIDES` alone (`d3d12,dxgi=n` forwards, `=b` is today's path). A plain DLL has no unix lib of its
  own: `mnd3d.dll` calls the builtin `winemetal.dll`'s new `WMTForward` (fork 0076, the `guest_vm` build only), which
  reaches slot `MN_WM_D3D` (155) of our guest `winemetal.so`: one transport and one context a process.
- **Op numbers:** `MN_OP_D3D = 0x10000`; a method's record is `MN_OP_D3D + id`. Ids 0 to 15 are the family's own
  (0 release, 1 add arena, 2 attach the notify queue, 3 the trace's counts); methods start at 16, numbered by the
  manifest in header order, declared methods only (an inherited method has its declaring interface's id).
- **Two ring-state halves:** the executor's 64 bytes of `state` a ring: `[0..3]` winemetal's (today), `[4..7]` the
  D3D dispatcher's.
- **Native DXMT loads lazily**, on the first `MN_OP_D3D` record, from `dxmt/` beside `macneutron-vm`: a run on today's
  path never loads it. Its Objective-C classes are renamed in the native build (fork 0075): native and Wine
  `winemetal` both define `CacheReader` and `CacheWriter`.
- **Fence completions on the host** go through a relay event (fork 0075: a `win32_native_event.h` kind whose
  `SetEvent` calls a function): `SetEventOnCompletion` with the guest's event posts `MN_NOTIFY_EVENT` to the context's
  notify queue, as winemetal's listener does; each `Signal` registers one that writes the fence's mirror slot.
- **The DXGI swapchain and its frame-latency object are M3's** (§4.8 presentation): M2's DXGI is the factory, adapters
  and outputs the headless programs use.
- **Loopback first:** `libmnd3d-loopback.dylib` (proxies, marshaller and stubs in one Mac process over a loopback
  transport, `MN_D3D_LOOPBACK`: guest `WCHAR` is the native one there) links M1's 19 native programs; their outputs
  must match the macOS references as natively. It is the spec's loopback test, and the place each task's rows go green
  before the VM.

## Review Focus

- `WCHAR` across the boundary: `SetName(L"...")` with non-ASCII text, and DXGI adapter descriptions (`Description[128]`)
  read back: UTF-16 in the guest, UTF-32 natively. Pinned in Task 3's round-trip cases and Task 9's adapter-description
  row in `wire-test`.
- An event the guest closes before its fence completes: the relay posts the guest's handle value, and the guest's
  `NtSetEvent` on a closed handle fails harmlessly. Pinned in Task 7's loopback case.
- The last `Release` on one thread while another thread's ring still records with the object: deferred until that ring
  passes it. Pinned in Task 8's `threads` program.
- An arena that is full when a mappable resource is created: the host answers "another arena" and the guest retries
  once; an arena that can't be had is `E_OUTOFMEMORY`. Pinned in Task 6's loopback cases.
- A record larger than the ring takes (shader bytecode or a root signature over `MN_WM_SCRATCH_IN`, 128 KiB): through
  the thread's scratch blob. Pinned in Task 9's `wire-test` case with 1 MiB of bytecode.

---

### Task 1: Native DXMT for hosting (fork 0075)

**Files:**
- Modify (fork): `src/util/win32_native_event.h` (a relay kind), `src/dxmt/dxmt_buffer.cpp` (CPU-placed buffers from
  the memory hook natively), `src/winemetal/winemetal.h`, `src/winemetal/unix/winemetal_unix.c` (the hook's setter,
  natively), `src/nativemetal/meson.build` (the class renames)
- Test: `eac-vm/dxmt-native/tests/win32-test.cpp` (`relay` case), `eac-vm/dxmt-native/tests/hook-test.cpp` (new)

**Interfaces:**
- Produces: `HANDLE mn_win32_relay_event(void (*fn)(void *ctx), void *ctx)`: an event whose `SetEvent` calls `fn(ctx)`
  (once a set, on the setter's thread) instead of waking waiters; `CloseHandle` frees it.
  `void WMTSetMemoryHooks(void *(*alloc)(uint64_t length, void *ctx), void (*free)(void *ptr, void *ctx), void *ctx)`
  (native only, exported by `winemetal.dylib`): `BufferAllocation`'s CPU-placed memory comes from `alloc` while hooks
  are set (`NULL` from it: the allocation fails as an out-of-memory one does today); unset, `aligned_malloc` as now.

- [ ] **Step 1: Write the failing tests:** `win32-test`'s `relay`: a relay event's `SetEvent` from another thread calls
  `fn` once with `ctx`, `WaitForSingleObject` on it is `WAIT_FAILED`. `hook-test.cpp` (native, against
  `install/lib`): with hooks counting calls over a 64 MiB `mmap` arena, an `UPLOAD` committed buffer's `Map` pointer
  lies inside the arena and `free` runs when the buffer's last reference goes; with no hooks, `Map` points outside.
- [ ] **Step 2: Run** `sh eac-vm/dxmt-native/tests/win32.sh` and the hook test. Expected: both fail to compile.
- [ ] **Step 3: Implement** the three changes; the class renames are `-DCacheReader=DXMTNativeCacheReader
  -DCacheWriter=DXMTNativeCacheWriter` in `nativemetal`'s `c_args`.
- [ ] **Step 4: Run** `win32.sh`, the hook test, `tests/build.sh` and `eac-vm/dxmt-native/gate.sh`. Expected:
  `win32: ok`, `hook: ok`, `build: ok`, 19 of 19; `nm` of `winemetal.dylib` has no `_OBJC_CLASS_$_CacheReader`.
- [ ] **Step 5: Commit** in the fork, export 0075 (applies on the stable series), commit here.

### Task 2: The generator's model and manifest

**Files:**
- Create: `eac-vm/d3d/gen.py`, `eac-vm/d3d/rules.txt` (the private interfaces' declarations first),
  `eac-vm/d3d/tests/test_gen.py`

**Interfaces:**
- Produces: `gen.py --headers <include-native> --rules rules.txt --out <dir> manifest` writes `<dir>/manifest.txt`,
  one line a method: `<id> <interface>::<method> <slot>` (slot: its vtable index). Python: `parse_headers(paths) ->
  Model` with `Model.interfaces: {name: Interface(name, iid, base, methods: [Method(name, ret, params: [Param(type,
  name)])])}`, `Model.structs: {name: Struct(fields: [Field(type, name, array_len)], union: bool)}`,
  `Model.enums`, `Model.typedefs`.

- [ ] **Step 1: Write the failing tests** (`python3 -m unittest eac-vm/d3d/tests/test_gen.py`): the headers give 62
  D3D12 interfaces and 44 DXGI ones; `ID3D12Fence`'s methods are IUnknown's 3, ID3D12Object's 4, `GetDevice`,
  `GetCompletedValue`, `SetEventOnCompletion`, `Signal` in that order; `ID3D12Device5`'s base is `ID3D12Device4`;
  `D3D12_RESOURCE_BARRIER` has a union; `ID3D12Resource::GetDesc` takes the explicit aggregate-return form
  (`D3D12_RESOURCE_DESC *__ret`, widl's `WIDL_EXPLICIT_AGGREGATE_RETURNS` block: the PE ABI and native DXMT's, M1);
  the IID of `ID3D12Device` is `189819f1-1db6-4b57-be54-1821339b85f7`;
  `rules.txt`'s `IMTLD3D12DeviceExt` has `GetTemporalScalerScaleRange` and `CreateTemporalScaler`; ids are stable
  (manifest of the same input twice is identical) and start at 16.
- [ ] **Step 2: Run them.** Expected: fail (no `gen.py`).
- [ ] **Step 3: Implement** the parser over widl's C blocks (`typedef struct <I>Vtbl { BEGIN_INTERFACE ... }`,
  `DEFINE_GUID(IID_<I>, ...)` or `__CRT_UUID_DECL`, `typedef struct <S> { ... } <S>;`, `typedef enum`), skipping the
  `__cplusplus` blocks.
- [ ] **Step 4: Run them.** Expected: pass.
- [ ] **Step 5: Commit.**

### Task 3: Rules, call tables and the marshaller's new kinds

**Files:**
- Modify: `eac-vm/d3d/gen.py`, `eac-vm/d3d/rules.txt`, `eac-vm/metal/wm/marshal.c`, `marshal.h`, `calls.h`
- Test: `eac-vm/d3d/tests/test_gen.py`, `eac-vm/metal/wm/tests/marshal-test.m` (new kinds), generated
  `build/eac-vm-work/d3d-gen/layout.cpp` (host layout asserts), `eac-vm/d3d/tests/roundtrip.c`

**Interfaces:**
- Consumes: Task 2's `Model`.
- Produces: `mn_wm_pack_call(const struct mn_wm_call *, void *params, struct mn_wm_out *)`,
  `mn_wm_unpack_call(...)` (as `mn_wm_unpack` with the slot replaced by the call) and `mn_wm_host_call_table`
  (today's `mn_wm_host_call` parameters, the slot replaced by `const struct mn_wm_call *call` and an
  `NTSTATUS_T (*invoke)(void *params)`): today's slot forms become wrappers over these. `MN_WM_MAX_REGIONS` goes
  from 64 to 256: a graphics pipeline's description is five bytecodes, a cached blob, stream output and up to 32 input
  elements each with a semantic name. Field kinds `WMF_WSTR_IN` (NUL-terminated UTF-16 in, a native `wchar_t`
  string on the host) and `WMF_UNION` (`count_offset` is the discriminant's offset; `nested` indexes a
  `struct mn_wm_union { uint16_t nvalues; const uint32_t *values; const uint8_t *nested; }` in `mn_wm_unions[]`).
  `gen.py ... calls` writes `<dir>/calls.c` (`const struct mn_wm_call mn_d3d_calls[]`, indexed by id, and the params
  struct of each method: `struct mn_d3d_p_<id> { uint64_t self; <args in wire types> }`), `<dir>/layout.cpp` and
  `<dir>/roundtrip-cases.c`. Rules syntax, one line a rule: `<Interface>::<Method> <param> in|out|inout
  count=<param>|size=<param>|bytes=<n>|string|wstring|union=<field>` and `<Interface>::<Method> async|sync|special`;
  `struct <S>.<field> ...` the same for fields.

- [ ] **Step 1: Write the failing tests:** `test_gen.py`: the defaults classify `CreateCommittedResource`'s
  `const D3D12_HEAP_PROPERTIES *` as in, `sizeof` 20, its `void **` with `REFIID` as a created object;
  `CopyDescriptors`' arrays need rules (a model with them removed makes `gen.py calls` exit 1, naming them); every
  D3D12 and DXGI method has a mode. `marshal-test`: a `WMF_WSTR_IN` of `"caché"` arrives as the native wide
  string; a `WMF_UNION` of two `D3D12_RESOURCE_BARRIER`s (a transition and a UAV barrier) translates each resource
  handle. `layout.cpp`: compiled with the native headers, every struct a rule or a default names has the size and
  offsets the generator computed for the guest (x86-64 Windows), except the ones listed as specials. `roundtrip.c`:
  for each generated case, pack with the guest tables and unpack with the host's in one process; fields equal.
- [ ] **Step 2: Run them.** Expected: fail.
- [ ] **Step 3: Implement** the rules for D3D12 and DXGI (`rules.txt`), the classification, the tables and the
  marshaller changes; the D3D12 recording and descriptor-write methods `async`, the rest by the Global Constraints.
- [ ] **Step 4: Run** `python3 -m unittest`, `make -C eac-vm/metal/wm marshal-test`, the layout compile and the
  round-trip test. Expected: all pass; `memory-test` and `marshal-test`'s winemetal cases still pass.
- [ ] **Step 5: Commit.**

### Task 4: The host dispatcher

**Files:**
- Create: `eac-vm/d3d/host/dispatch.cpp`, `eac-vm/d3d/host/dispatch.h`
- Modify: `eac-vm/host/macneutron-vm.swift` (the executor's handler and hooks: `mn_vm_dispatch`, `mn_vm_hooks`),
  `eac-vm/host/build.sh` (generated stubs, native DXMT copied into `build/eac-vm-work/dxmt/`),
  `eac-vm/metal/wm/handles.h` (`MN_HK_D3D`)
- Test: `eac-vm/d3d/tests/dispatch-test.cpp`

**Interfaces:**
- Consumes: Task 3's tables and `mn_wm_host_call_table`.
- Produces: `int mn_vm_dispatch(uint32_t ctx_id, uint64_t *ring_state, uint32_t op, const void *in, uint32_t len,
  void *reply, uint32_t cap, uint32_t *n)` (the executor's `mn_op_fn`): `op >= MN_OP_D3D` to the D3D family, else
  `mn_wm_dispatch`. Generated `<dir>/stubs.cpp`: `NTSTATUS mn_d3d_stub_<id>(void *params)` each, calling the native
  method through the object's interface pointer for the method's declaring interface (queried once a handle, kept
  with it). The handle table's objects are `MN_HK_D3D`, released with the interface's `Release`. A creation's reply
  carries the IIDs the native object answers among its family (the requested interface's derivation chain and the
  rules' companions). `int mn_d3d_load(const char *dir)`: `dlopen`s `d3d12.dylib` and `dxgi.dylib`, sets the
  memory hooks (Task 6), once; its failure is logged `mn-d3d: error: native DXMT: <dlerror>` and every D3D record
  answers `E_FAIL`. Async failures log `mn-d3d: error: <Interface>::<Method> 0x<hr>`. Device removal (§6): once a
  stub returns `DXGI_ERROR_DEVICE_REMOVED`, `_HUNG` or `_RESET`, the context is removed and every later D3D record
  answers `DXGI_ERROR_DEVICE_REMOVED`, logged once with the first method that said so.

- [ ] **Step 1: Write the failing test** `dispatch-test`: through `mn_vm_dispatch` directly (no transport), an export
  record `D3D12CreateDevice` with a guest-picked handle creates a device; `ID3D12Device::CreateFence` on that handle
  makes a fence whose `GetCompletedValue` record answers its initial value; a winemetal op still reaches
  `mn_wm_dispatch`; a record for an unknown id, or shorter than its params, answers `MN_EINVAL` and logs; a test
  table entry whose stub returns `DXGI_ERROR_DEVICE_REMOVED` makes the next `CreateFence` answer it too.
- [ ] **Step 2: Run it.** Expected: fail to link.
- [ ] **Step 3: Implement** the dispatcher, the load, the generated stubs' compile in `build.sh`, the Swift change.
- [ ] **Step 4: Run** `dispatch-test`, `eac-vm/tests/turn.sh`, `eac-vm/tests/appnap.sh` and the metal gate's
  `memory-test`. Expected: pass; a VM run on today's path logs nothing from `mn-d3d`.
- [ ] **Step 5: Commit.**

### Task 5: The guest runtime and the loopback

**Files:**
- Create: `eac-vm/d3d/guest/proxy.c`, `proxy.h` (proxy objects, `QueryInterface`, reference counts, caches, local
  `ID3DBlob`, the trace), `eac-vm/d3d/guest/unix.c` (the unix side, compiled into the guest `winemetal.so` as slot
  `MN_WM_D3D`: `mn_wm_pack_call`, the transport, `mn_wm_unpack_call`, the notify listener), `eac-vm/d3d/tests/loopback.c` (transport stand-ins, after
  `eac-vm/metal/wm/tests/loopback.c`), `eac-vm/d3d/build-loopback.sh`, `eac-vm/d3d/loopback-gate.sh`
- Generated: `<dir>/proxies.c` (vtables and methods for each interface, filling the params struct and calling
  `mn_d3d_call(id, params)`)

**Interfaces:**
- Consumes: Tasks 3 and 4.
- Produces: `HRESULT mn_d3d_call(uint32_t id, void *params)` (PE side: `WMTForward(MN_WM_D3D, &args)`) and
  `NTSTATUS mn_d3d_unix_call(void *args)` (unix side, `__wine_unix_call_funcs[MN_WM_D3D]` of the guest
  `winemetal.so`; `MN_WM_SLOTS` becomes 156). `build/d3d-loopback/libmnd3d-loopback.dylib` exporting
  `D3D12CreateDevice`, `D3D12SerializeRootSignature`, `D3D12SerializeVersionedRootSignature`, `CreateDXGIFactory`,
  `CreateDXGIFactory1`, `CreateDXGIFactory2` and the other `d3d12.def` and `dxgi.def` exports. `loopback-gate.sh`:
  M1's `gate.sh` with the programs linked to the loopback library (`build-tests.sh` gains `MN_D3D_PROGRAMS_LIB`), its
  runs in `build/d3d-loopback/runs`; prints `d3d-loopback gate: all 19 programs passed` or the failures.

- [ ] **Step 1: Write the failing check:** `loopback-gate.sh` with `MN_DXMT_ONLY=d3d12_null`. Expected: FAIL (no
  library).
- [ ] **Step 2: Implement** the runtime and the loopback build.
- [ ] **Step 3: Run** `MN_DXMT_ONLY=d3d12_null sh eac-vm/d3d/loopback-gate.sh`. Expected: `d3d12_null` passes (its
  readback through `Map` uses Task 6's arenas only from the next task: until then the host maps CPU-placed memory in
  host memory, which in one process the guest side can read).
- [ ] **Step 4: Commit.**

### Task 6: Memory

**Files:**
- Create: `eac-vm/d3d/host/memory.cpp` (arenas: the hooks, host pointer to `(blob, offset)`)
- Modify: `eac-vm/d3d/guest/proxy.c`, `unix.c` (galloc arenas handed to the host, `Map`'s guest address, the retry),
  `eac-vm/d3d/rules.txt` (`Map`, `Unmap`, `WriteToSubresource`, `ReadFromSubresource`)
- Test: `eac-vm/d3d/tests/dispatch-test.cpp` (`arena` cases), the loopback gate

**Interfaces:**
- Produces: op `MN_OP_D3D + 1` (add arena: `struct { uint64_t blob_id, offset, len; }`, one `galloc` region the guest
  holds for the host). The hook carves 16 KiB-aligned pieces first fit; none free: `NULL`, and the creation's reply
  status is `MN_D3D_NEED_ARENA` (`0x20000001`) with the bytes wanted, on which the guest adds an arena of
  `max(64 MiB, wanted)` and sends the creation again, once. `Map`'s reply is `(blob_id, offset)`; the guest returns its
  own mapping of it, cached in the proxy (a persistent map costs one round trip).
- [ ] **Step 1: Write the failing tests:** `dispatch-test` `arena`: an upload buffer bigger than the first arena gets
  `MN_D3D_NEED_ARENA`, then succeeds after an arena is added; with the guest refusing to add one, the creation
  answers `E_OUTOFMEMORY`; a mapped pointer is the arena's memory at the reported offset; freeing the buffer returns
  its piece. Loopback: `d3d12_copy` and `d3d12_api` (its `map` line).
- [ ] **Step 2: Run them.** Expected: fail.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run them**, then the whole loopback gate. Expected: pass; the rows Task 7 to 9 own may still fail.
- [ ] **Step 5: Commit.**

### Task 7: Waiting

**Files:**
- Create: `eac-vm/d3d/host/events.cpp` (the mirror page, relay events, notify posts)
- Modify: `eac-vm/d3d/guest/proxy.c`, `unix.c` (the mirror read, the listener thread), `rules.txt` (`ID3D12Fence`,
  `ID3D12Fence1`, `ID3D12CommandQueue::Signal`/`Wait`, `ID3D12Device1::SetEventOnMultipleFenceCompletion`)
- Test: `dispatch-test` (`events` cases), the loopback gate

**Interfaces:**
- Consumes: Task 1's `mn_win32_relay_event`; winemetal's notify queue (`wm/notify.h`, `mn_notifyq_post`, exported
  from `memory.m` if it is static).
- Produces: op `MN_OP_D3D + 2` (attach the context's notify queue blob, and the mirror blob: one `uint64_t` a fence,
  its slot named in the fence's creation reply). `GetCompletedValue` reads the slot in the guest. Every write to a slot
  is `max(slot, value)` (relays fire in any order), and the proxy's `ID3D12Fence::Signal(v)`, a CPU signal that
  D3D12 completes at once, raises the guest's slot to `v` before it sends the record. A `NULL` event in
  `SetEventOnCompletion` is a synchronous wait on the host.

- [ ] **Step 1: Write the failing tests:** `dispatch-test` `events`: a `Signal(5)` on a queue raises the fence's mirror
  slot to 5 without a record; a CPU `ID3D12Fence::Signal(9)` reads back 9 from `GetCompletedValue` at once; a relay
  for 3 that fires after one for 5 leaves 5; `SetEventOnCompletion(7, e)` posts `MN_NOTIFY_EVENT` with `e`'s value once the fence
  reaches 7; an event closed by the guest before then is still posted, and the guest's set fails without harm;
  `SetEventOnMultipleFenceCompletion` with all and any. Loopback: `d3d12_api`'s `multifence`, `d3d12_hazards`,
  `d3d12_query`, `d3d12_timestamp`.
- [ ] **Step 2: Run them.** Expected: fail.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run them.** Expected: pass.
- [ ] **Step 5: Commit.**

### Task 8: Threads

**Files:**
- Modify: `eac-vm/d3d/guest/proxy.c` (a command list's `Close` records its ring and sequence), `eac-vm/d3d/host/
  dispatch.cpp` (`ExecuteCommandLists` waits for them), `eac-vm/metal/host/executor.c` (`mn_executor_wait_seq`)
- Create: `eac-vm/d3d/tests/threads.cpp` (a native program for the loopback: two threads record, a third executes;
  a last `Release` on one thread while another records with the object)

**Interfaces:**
- Produces: `int mn_executor_wait_seq(uint32_t ctx_id, uint64_t ring_blob, uint64_t seq, unsigned timeout_ms)`: 0 once
  that ring's `done_seq` reaches `seq`, -1 on the timeout (5000 ms, `MN_H_WAIT_MS`'s). `ExecuteCommandLists`' record
  carries `(ring_blob, close_seq)` a list.

- [ ] **Step 1: Write the failing test** `threads.cpp` (its lines printed as `threads ...`, compared with the native
  run of the same program, M1's way). Expected: FAIL (a list executed before its recording finished, or a crash).
- [ ] **Step 2: Implement.**
- [ ] **Step 3: Run** it and the loopback gate. Expected: pass.
- [ ] **Step 4: Commit.**

### Task 9: The remaining methods

**Files:**
- Modify: `eac-vm/d3d/rules.txt`, `eac-vm/d3d/host/specials.cpp` (new), `eac-vm/d3d/guest/proxy.c`
- Test: `eac-vm/d3d/tests/wire-test.cpp` (new: cases the 19 programs don't reach), the loopback gate

**Interfaces:**
- Produces specials: `ID3D12Device2::CreatePipelineState` (the subobject stream, walked per its types, pointers in it
  marshaled), `CheckFeatureSupport` (per feature: its struct's size and in-out fields), DXGI's adapter and output
  descriptions (`WCHAR` arrays converted), `D3D12SerializeRootSignature` and its versioned form and every method that
  returns an `ID3DBlob` (the bytes copied into a guest-local blob), `ID3D12PipelineLibrary` (`Serialize` and
  `GetSerializedSize`, the stored pipelines), `GetCachedBlob`, `ExecuteIndirect`, PIX markers (`void *` and size),
  DXMT's private interfaces.

- [ ] **Step 1: Write the failing tests:** `wire-test`: 1 MiB of bytecode through `CreateComputePipelineState`
  answers what natively it does; a graphics pipeline with 32 input elements is created as natively; a `SetName` with non-ASCII text reads back through `GetPrivateData
  (WKPDID_D3DDebugObjectNameW)`; `IDXGIAdapter1::GetDesc1`'s `Description` equals the native one. The loopback gate,
  all 19.
- [ ] **Step 2: Run them.** Expected: fail where the specials are missing.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** `wire-test` and `loopback-gate.sh`. Expected: pass, `d3d-loopback gate: all 19 programs passed`.
- [ ] **Step 5: Commit.**

### Task 10: The guest stand-ins

**Files:**
- Create: `eac-vm/d3d/guest/mnd3d.def`, `d3d12.def`, `dxgi.def` (the two forward their exports to `mnd3d.dll`),
  `eac-vm/d3d/build-guest.sh` (plain PE DLLs on the Mac with llvm-mingw, importing `winemetal.dll` from the guest
  Wine SDK as `eac-vm/dxmt/build.sh` does; the guest `winemetal.so` gains `unix.c` in `eac-vm/metal/build-guest.sh`)
- Modify (fork 0076, `guest_vm` only): `src/winemetal/winemetal_thunks.c`, `winemetal.def` (`NTSTATUS WINAPI
  WMTForward(UINT32 call, void *params)`: `UNIX_CALL(call, params)` for `call >= 155`, `STATUS_INVALID_PARAMETER`
  below)
- Modify: `eac-vm/image/build.sh` (`/opt/mn-wine/lib/wine/mn-d3d/x86_64-windows/`), `eac-vm/image/guest-stages.sh`
  (the `d3d` stage), `eac-vm/image/stamps.sh`, `eac-vm/guest/run-game.sh` and `eac-vm/tests/dxmt.sh` (the three DLLs
  linked into the prefix's `system32`; `WINEDLLOVERRIDES` takes `d3d12,dxgi=n` with `MACNEUTRON_VM_D3D=forward`,
  `=b` otherwise), `Sources/MacNeutronCore/MicroVM.swift` (`passed` gains `MACNEUTRON_VM_D3D`,
  `MACNEUTRON_VM_D3D_TRACE`)
- Test: `eac-vm/tests/d3d-smoke.sh` (new)

**Interfaces:**
- Produces: `mnd3d.dll` initialises its unix lib as DXMT's `winemetal.dll` does (`__wine_init_unix_call`); a
  `MACNEUTRON_VM_D3D_TRACE=1` run writes `traces/d3d-guest.txt` (each method id and its count) and the host
  `traces/d3d-host.txt`. Forwarding stays off by default outside the gates (Global Constraints).

- [ ] **Step 1: Write the failing test** `d3d-smoke.sh`: in the VM, `d3d12_null` with `MACNEUTRON_VM_D3D=forward`
  prints its `null` lines, the trace shows `D3D12CreateDevice` reached the host, and with `MACNEUTRON_VM_D3D=guest`
  the same program runs on guest DXMT (the trace absent). It prints `macneutron-vm`'s resident size in both runs:
  forwarding loads a second LLVM (native `winemetal.dylib`) beside the Wine build's. Expected: FAIL.
- [ ] **Step 2: Implement**, rebuild the stages and the images.
- [ ] **Step 3: Run** `d3d-smoke.sh`, `eac-vm/tests/wine.sh`, `vm-setup.sh`. Expected: `d3d-smoke: ok`; the rest as
  before.
- [ ] **Step 4: Commit.**

### Task 11: The gate

**Files:**
- Create: `eac-vm/d3d/gate.sh`, `eac-vm/d3d/tests/coverage.py`
- Modify: `eac-vm/dxmt/compare.py` (`host` fails on `mn-d3d: error`)

**Interfaces:**
- Consumes: the 4b-2 gate (`share.sh`, `ref.sh`, `tests/dxmt.sh`, `compare.py`); M1's 19 rows; Task 10's trace.

- [ ] **Step 1: Write `gate.sh`:** the loopback gate, then the 19 rows in the VM through forwarding
  (`MACNEUTRON_VM_D3D=forward`, the trace on) against the macOS references, `compare.py host` on the VM's output,
  and `coverage.py`: every method id in the guest trace has a rule and a host count. Native DXMT's shader cache is
  one per VM run (`DXMT_SHADER_CACHE_PATH` in the VM's environment): a row whose lines need a cold cache runs in a VM
  of its own.
- [ ] **Step 2: Run it.** Expected: `d3d gate: all 19 programs passed` (loopback and VM), `coverage: ok`. A row that
  differs is a defect to find with superpowers:systematic-debugging. The existing VM gates keep passing (spec §7):
  `vm-setup.sh`, `turn.sh`, `modes.sh`, `tpm-host.sh`, `audio-host.sh`, and the 4b-2 gate on today's path.
- [ ] **Step 3: Commit.**

### Task 12: Record

**Files:**
- Modify: `docs/research/2026-10-15-vm-route/README.md` (§18 "Direct3D forwarding, M2: D3D12 and DXGI"), the spec
  where M2 settled what it left open (the generator's input, the op family, the swapchain to M3)

- [ ] **Step 1: Write §18:** the generator's numbers (interfaces, methods, rules written and inferred, specials), the
  loopback and VM gates' rows with run times, the 4b-2 gate's guest times beside them, what's left for M3 and M4.
- [ ] **Step 2: Privacy scan, commit.**

# Direct3D forwarding M1: native DXMT Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Our DXMT fork's `d3d12`, `dxgi`, `d3d11`, `d3d10core` and `winemetal` built as native arm64 macOS dylibs,
and DXMT's headless D3D12 test programs, built natively against them, printing what they print under the macOS Wine
runtime.

**Architecture:** DXMT's own native meson path (upstream `build: add native build targets`, VirtualBox's: Oracle
forwards guest D3D11 to native DXMT on the host, the architecture M2 builds; read their consumer when M2 is planned,
licence permitting) already builds everything but our D3D12. Two fork patches close the gap: the D3D12 headers and
ABI macro our PE build uses, and real Win32 handle shims (events, semaphores, files, time, modules) shared by native
DXMT and the native test programs. The 4b-2 gate's comparison (`compare.py`) judges the native runs against the macOS
runtime's.

**Tech Stack:** C++20 and C (DXMT), meson and ninja, clang (Xcode), LLVM 15 (airconv, the runtime's
`build/wine-arm64-src/llvm-arm64`), POSIX sh, Python 3 (`compare.py`, unchanged).

**Spec:** `docs/superpowers/specs/2026-10-17-macneutron-vm-d3d-forwarding-design.md` (§4.1, §7, §8 M1).

**Go/no-go (spec §8, settled before this plan, 2026-10-17):** go. A spike configured and built the fork natively
(`-Denable_d3d12=true`): everything but `d3d12` and `replay` built; 259 errors, all in our D3D12. With the PE build's
own D3D12 headers and `WIDL_EXPLICIT_AGGREGATE_RETURNS`, 123 remain: about 50 Win32 names (files, time, events and
semaphores, threads, modules, `DllMain`), one GUID (`kWKPDID_D3DDebugObjectNameW`) and two type errors. None is in a
rendering path: they sit in the shader cache, the DXIL dump, the stats, the pipeline recorder and the command queue's
and swapchain's waits. The native dylibs export their entry points (`CreateDXGIFactory2`, `D3D11CreateDevice`).
Spec §4.1's "eight `CloseHandle` and three event calls" undercounted; this plan's Task 1 list is the measured one.

## Global Constraints

- Native DXMT is our fork (`build/wine-arm64-src/dxmt`, branch `macneutron`), built through DXMT's native (non-cross)
  meson path: `d3d12`, `dxgi`, `d3d11` and `d3d10core` as macOS dylibs, `winemetal` linked in natively
  (`src/nativemetal`), Windows types from `include/native`. arm64, with the runtime's CPU tuning (`MACNEUTRON_CPU`,
  default `apple-m5`, as `wine-arm64/build.sh`).
- Built by `eac-vm/dxmt-native/build.sh` from `build/wine-arm64-src/dxmt`. The macOS runtime's PE build of DXMT is
  unchanged: every fork change is native-only (`#ifndef _WIN32`, or the native include list).
- Fork changes are commits on `macneutron`, exported as patches 0072 onward with `make wine-arm64-export` (run
  `wine-arm64/export.sh`'s dry check first: the tree was swapped during the release rehearsal). No PRs upstream.
- The gate (spec §8 M1): DXMT's test programs natively on the Mac match the macOS Wine run, by `eac-vm/dxmt/compare.py`.
- Shipping native DXMT in the app (`Contents/Helpers/vm/dxmt/`, `VMToolsInstaller.items`) is not M1's: nothing loads
  it until forwarding does. It goes in the first milestone whose VM loads it.
- Never open MacNeutron.app, never start or quit Steam. Downloads need the maintainer's yes (none is expected: the
  headers come from our llvm-mingw, the LLVM is the runtime's).
- Use `/usr/bin/grep` in scripts; commits end with the session's trailer lines; a privacy scan before committing docs.

## Rulings made writing this plan

- **The gate's programs:** the 4b-2 gate's 19 headless D3D12 rows (`eac-vm/dxmt/invocations`: `d3d12_api` to
  `d3d12_vsread`, without the XeSS and DLSS rows). Out: the 9 `d3d12_xess_*` and `d3d12_dlss_*` rows (§4.11's upscalers,
  M3: natively there's no Wine builtin `libxess` and `nvngx` is a native build of its own), and `d3d11_vsia` (it
  compiles HLSL with Wine's `d3dcompiler`, which has no native build; D3D11 is M4's). The spec's "DXMT's test
  programs" is broader: the rest are gated in M3 and M4.
- **The headers:** `include/native/directx` is a submodule (mingw-directx-headers, WIDL 7.7, older than our D3D12), so
  the newer files go in a new folder of the fork, `include/native/d3d12`, ahead of it in the native include list:
  `d3d12.h`, `d3d12sdklayers.h`, `d3d12shader.h`, copied from
  `build/dxmt-src/llvm-mingw-own/generic-w64-mingw32/include` (WIDL 11.12), the files the PE build compiles against.
- **`WIDL_EXPLICIT_AGGREGATE_RETURNS`** is a native project argument in the fork's `meson.build`, not a build-script
  flag: M2's dispatcher includes these headers to call DXMT and must see the same signatures. The test programs
  define it already (`dxmt/tests/d3d12_common.hpp`).

## Review Focus

- A fence's `SetEventOnCompletion` with an event the test program made: DXMT's `SetEvent` and the program's
  `WaitForSingleObject` must agree on one handle layout across dylibs. Pinned in Task 1's shared-header test and Task
  4's `d3d12_api` and `d3d12_hazards` rows.
- Paths: DXMT builds UTF-16 paths (`CreateFileW`, `CreateDirectoryW`) for its shader cache and dumps; a non-ASCII
  folder (the maintainer's home is ASCII, a user's may not be) must round-trip. Pinned in Task 1's test.
- A wait with a timeout (`WaitForSingleObject(h, ms)`) returns `WAIT_TIMEOUT` on time, and `INFINITE` waits until
  signalled; a semaphore past its maximum count fails. Pinned in Task 1's test.
- The shader cache across two runs (`d3d12_cache`'s second run reads what the first wrote): the native cache must
  write and read back through the shims. Pinned in Task 4 (each row runs twice, as `ref.sh` does).
- A program that crashes natively must fail its row, not hang the gate: each run has `ref.sh`'s timeout. Pinned in
  Task 4's script.

---

### Task 1: Win32 handles for the native build

**Files:**
- Create (fork): `src/util/win32_native.h` (C and C++, no `dxmt::Logger`: the test programs include it too)
- Modify (fork): `src/util/util_win32_compat.h` (includes it; its warn-and-return stubs for `CloseHandle`,
  `DuplicateHandle`, `CreateSemaphore`, `ReleaseSemaphore`, `GetCurrentThread`, `GetCurrentProcess` give way to the
  real ones)
- Create: `eac-vm/dxmt-native/tests/win32-test.cpp`, `eac-vm/dxmt-native/tests/win32.sh`

**Interfaces:**
- Produces: `HANDLE` is a pointer to `struct mn_win32_handle { uint32_t kind; ... }` (kinds: event, semaphore, file,
  thread pseudo-handle, process pseudo-handle). `CloseHandle` frees by kind; `DuplicateHandle` of an event or
  semaphore adds a reference. Every function below is `static inline` in `win32_native.h`, so any binary that includes
  it acts on another's handles. Declared there, with Win32's signatures and return conventions:
  - events and semaphores: `CreateEventW`, `CreateEventA`, `SetEvent`, `ResetEvent`, `CreateSemaphore` (as upstream
    spells it), `CreateSemaphoreW`, `ReleaseSemaphore`, `WaitForSingleObject` (`INFINITE`, `WAIT_OBJECT_0`,
    `WAIT_TIMEOUT`), `CloseHandle`, `DuplicateHandle`;
  - files: `CreateFileW` (`GENERIC_READ`, `GENERIC_WRITE`, `FILE_APPEND_DATA`, `FILE_SHARE_*`, `CREATE_ALWAYS`,
    `OPEN_ALWAYS`, `OPEN_EXISTING`, `FILE_ATTRIBUTE_NORMAL`), `ReadFile`, `WriteFile`, `SetFilePointer`/`FILE_BEGIN`,
    `SetEndOfFile`, `GetFileSizeEx`, `GetFileAttributesW`/`INVALID_FILE_ATTRIBUTES`, `CreateDirectoryW`,
    `MoveFileW`/`MOVEFILE_REPLACE_EXISTING`, `DeleteFileW`, `GetLastError` (`ERROR_ALREADY_EXISTS` and the codes these
    set), `LocalFree`; UTF-16 paths converted to UTF-8;
  - time: `QueryPerformanceCounter`, `QueryPerformanceFrequency` (`mach_absolute_time`, in nanoseconds),
    `GetTickCount`, `GetTickCount64`, `Sleep`, `GetLocalTime`/`SYSTEMTIME`;
  - threads and processes: `GetCurrentThread`, `GetCurrentThreadId`, `GetCurrentProcess`, `SetThreadPriority`
    (`THREAD_PRIORITY_TIME_CRITICAL`: no-op, TRUE), `GetThreadDescription` (`pthread_getname_np`, `LocalFree` frees
    it), `THREAD_QUERY_LIMITED_INFORMATION`;
  - modules and environment: `LoadLibraryW`/`LoadLibraryA` (`dlopen`), `GetProcAddress` (`dlsym`),
    `SetEnvironmentVariableW` (`setenv`/`unsetenv`), `DisableThreadLibraryCalls`, `DLL_PROCESS_ATTACH`,
    `DLL_PROCESS_DETACH`;
  - `GetAsyncKeyState(VK_F9)` returns 0 (the F9 pass dump has no key natively; its environment trigger stays).

- [ ] **Step 1: Write the failing test** `win32-test.cpp`, built natively by `win32.sh` with `clang++ -std=c++20`
  against the fork's `include/native/windows` and `src/util`, two translation units (`win32-test.cpp` and a second
  file that only calls `SetEvent`, to prove the layout crosses objects). Cases, each printing `ok <case>` or
  `FAIL <case>`:
  - `event`: an auto-reset event set from another thread wakes `WaitForSingleObject(h, INFINITE)` with
    `WAIT_OBJECT_0`, and resets; a manual-reset one stays set until `ResetEvent`.
  - `timeout`: `WaitForSingleObject(unset, 50)` returns `WAIT_TIMEOUT` after 40 to 200 ms.
  - `semaphore`: initial 1, max 2: two waits with 0 ms give `WAIT_OBJECT_0` then `WAIT_TIMEOUT`;
    `ReleaseSemaphore(h, 2, &prev)` fails (past the maximum) and `ReleaseSemaphore(h, 1, &prev)` gives `prev == 0`.
  - `cross`: the second file's `SetEvent` wakes the first's wait.
  - `file`: in a temporary folder named `caché-é`, `CreateDirectoryW` twice (the second fails with
    `ERROR_ALREADY_EXISTS`), `CreateFileW(CREATE_ALWAYS)` + `WriteFile` 5 bytes + `GetFileSizeEx == 5`,
    `SetFilePointer(0, FILE_BEGIN)` + `ReadFile` back the same bytes, `SetEndOfFile` at 2 gives size 2, `MoveFileW`
    with `MOVEFILE_REPLACE_EXISTING` onto an existing file, `GetFileAttributesW` of a missing path is
    `INVALID_FILE_ATTRIBUTES`, `DeleteFileW`.
  - `clock`: `QueryPerformanceFrequency` is 1000000000 and two counters 10 ms apart (`Sleep(10)`) differ by 8 to 50 ms.
  - `module`: `LoadLibraryA("/usr/lib/libSystem.B.dylib")` and `GetProcAddress(h, "getpid")` return non-null.
  `win32.sh` prints `win32: ok` or `win32: FAIL <cases>`.

- [ ] **Step 2: Run it:** `sh eac-vm/dxmt-native/tests/win32.sh`. Expected: it fails to compile (`win32_native.h`
  doesn't exist).

- [ ] **Step 3: Implement** `win32_native.h` (events and semaphores on a mutex and condition variable each, a
  reference count, `pthread_cond_timedwait` for timeouts; files on file descriptors; the error code in a
  `thread_local`) and include it from `util_win32_compat.h`, removing the stubs it replaces.

- [ ] **Step 4: Run it:** `sh eac-vm/dxmt-native/tests/win32.sh`. Expected: `win32: ok`.

- [ ] **Step 5: Commit** in the fork (`util: Real Win32 handles for the native build.`), export it (0072), and commit
  the export and the test in this repository.

### Task 2: The native build

**Files:**
- Create (fork): `include/native/d3d12/d3d12.h`, `d3d12sdklayers.h`, `d3d12shader.h` (the Rulings' copies, with a
  `README` line naming their source and that they follow `dxmt/pins`' llvm-mingw)
- Modify (fork): `meson.build` (`./include/native/d3d12` first in `dxmt_native_include_dirs`; native
  `-DWIDL_EXPLICIT_AGGREGATE_RETURNS`), the D3D12 sources that use Win32 without the compat header (they include
  `util_win32_compat.h` under `#ifndef _WIN32`), `kWKPDID_D3DDebugObjectNameW` where the native build lacks it,
  `D3D12_ERROR_DRIVER_VERSION_MISMATCH` if the header still lacks it, `src/d3d12/d3d12_device.cpp:1496` (a `nullptr`
  for a `UINT64`) and `src/d3d12/d3d12_resource_helper.cpp:272` (`kExclusiveWrite` not a constant expression natively)
- Create: `eac-vm/dxmt-native/build.sh`, `eac-vm/dxmt-native/tests/build.sh` (the check)

**Interfaces:**
- Consumes: Task 1's `win32_native.h`.
- Produces: `build/dxmt-native/install/lib/` (meson's install prefix) holding `d3d12.dylib`, `dxgi.dylib`,
  `d3d11.dylib`, `d3d10core.dylib` and `winemetal.dylib`, and `build/dxmt-native/install/include-native` (a copy of the
  fork's native include folders, for the test programs and M2's dispatcher). `build.sh` prints
  `dxmt-native: built <dylibs>` and exits 0.

- [ ] **Step 1: Write the failing check** `tests/build.sh`: runs `eac-vm/dxmt-native/build.sh`, then checks each of
  the five dylibs exists, is `arm64` (`lipo -archs`), and exports its entry points (`nm -gU`): `_D3D12CreateDevice`,
  `_D3D12GetDebugInterface`, `_D3D12SerializeVersionedRootSignature` (`d3d12`), `_CreateDXGIFactory2` (`dxgi`),
  `_D3D11CreateDevice` (`d3d11`), `_D3D10CoreCreateDevice` (`d3d10core`). Prints `build: ok` or `build: FAIL <what>`.

- [ ] **Step 2: Write `build.sh`** (meson setup into `build/dxmt-native/build` when its inputs change, as
  `wine-arm64/build.sh` does with `.setup-inputs`; `--buildtype release`, `-Denable_d3d12=true`,
  `-Denable_nvapi=false`, `-Denable_nvngx=false`, `-Dwine_builtin_dll=false`,
  `-Dnative_llvm_path=build/wine-arm64-src/llvm-arm64`, `-Dtranslator_key_file=build/wine-arm64-src/dxmt-translator-key`,
  and `-mcpu=$MACNEUTRON_CPU` in `c_args`/`cpp_args` unless it's `generic`; compile, install). Run `tests/build.sh`.
  Expected: FAIL, `d3d12.dylib` missing (the D3D12 sources don't compile yet).

- [ ] **Step 3: Implement** the fork changes listed under Files.

- [ ] **Step 4: Run** `sh eac-vm/dxmt-native/tests/build.sh`. Expected: `build: ok`. Link errors the compile spike
  couldn't show are this step's to fix: a Win32 name a link needs goes into Task 1's header with a test case.

- [ ] **Step 5: The PE build is unchanged:** `sh wine-arm64/build.sh` (the runtime's DXMT step, PE and
  `winemetal.so`) and `sh dxmt/check.sh` pass as before.

- [ ] **Step 6: Commit** in the fork (`build: Native D3D12 with the PE build's headers.`), export it (0073), and
  commit the export, the scripts and a `wine-arm64/README.md` row for `include/native/d3d12`.

### Task 3: The test programs, natively

**Files:**
- Create: `eac-vm/dxmt-native/build-tests.sh`, `eac-vm/dxmt-native/tests/native-prelude.h`

**Interfaces:**
- Consumes: Task 2's `install/lib` and `install/include-native`; Task 1's `win32_native.h`.
- Produces: `build/dxmt-native/tests/<program>` (no extension) for the 19 rows' programs, each linked to
  `install/lib`'s `d3d12.dylib` and `dxgi.dylib` with an rpath to it.

- [ ] **Step 1: Write the failing check:** `build-tests.sh` ends by running `build/dxmt-native/tests/d3d12_null` with
  `dxmt/tests/shaders/null.cs.dxil`; its `null` lines must equal the `null` lines of a fresh macOS reference run of
  the same row (`MN_DXMT_ONLY=d3d12_null sh eac-vm/dxmt/share.sh && MN_REF_RUNS=1 sh eac-vm/dxmt/ref.sh`), printing
  `tests: ok` or `tests: FAIL <diff>`. The reference needs what `ref.sh` needs: `make dxmt-tests`,
  `.build/release/macneutron` and the runtime's `build/wine-arm64/wine.app`. Run it. Expected: FAIL (no programs).

- [ ] **Step 2: Implement** `build-tests.sh`: each of the 19 programs' `dxmt/tests/<name>.cpp` compiled with
  `clang++ -std=c++17 -include native-prelude.h`, the native include folders, `-L install/lib -ld3d12 -ldxgi`,
  `-Wl,-rpath,<install/lib>`. `native-prelude.h` brings what the PE build takes from mingw and user32/psapi:
  `win32_native.h`, `INITGUID` for the program's IIDs, `GetProcessMemoryInfo` (`task_info`, `d3d12_timestamp`'s
  memory line).

- [ ] **Step 3: Run** `sh eac-vm/dxmt-native/build-tests.sh`. Expected: all 19 build, `tests: ok`.

- [ ] **Step 4: Commit.**

### Task 4: The gate

**Files:**
- Create: `eac-vm/dxmt-native/gate.sh`

**Interfaces:**
- Consumes: Task 3's programs; `eac-vm/dxmt/share.sh`, `ref.sh` and `compare.py` unchanged.

- [ ] **Step 1: Write `gate.sh`:** `compare.py self-test`; the macOS reference for the 19 rows
  (`MN_DXMT_ONLY="<the 19 names>" sh eac-vm/dxmt/share.sh && sh eac-vm/dxmt/ref.sh`: two runs each); then each row
  twice natively, as `ref.sh` runs it: arguments with `@S` and `@T` as plain paths (`dxmt/tests/shaders`,
  `dxmt/tests`), `DXMT_SHADER_CACHE_PATH` a fresh folder per run under `build/dxmt-native/runs/cache/<name>.<i>`,
  `DXMT_MAX_ANISOTROPY=0`, `ref.sh`'s timeout as a watchdog, output to `build/dxmt-native/runs/<name>.<i>.txt` with
  `.status` and `.time`; each row's second native run compared with `compare.py <name> <ref 1> <ref 2> <native 2>
  <prefixes>` (the second: it reads the first run's cache, as the macOS second run does). Prints a line a row and
  `dxmt-native gate: all 19 programs passed` or `dxmt-native gate: FAIL`.

- [ ] **Step 2: Run it:** `sh eac-vm/dxmt-native/gate.sh`. Expected: all 19 pass. A row that differs is a defect in
  the native build (a shim's semantics, a path, a missing feature): find its cause with
  superpowers:systematic-debugging, fix it with a Task 1 test case where it is a shim, and run the gate again.

- [ ] **Step 3: Commit** the script (and any fixes, each exported with the fork's patches).

### Task 5: Record

**Files:**
- Modify: `docs/research/2026-10-15-vm-route/README.md` (a new section, "§17 Direct3D forwarding, M1: native DXMT")
- Modify: `docs/superpowers/specs/2026-10-17-macneutron-vm-d3d-forwarding-design.md` §4.1 (the measured Win32 list;
  the headers' folder; app packaging moved to the first milestone that loads native DXMT)

- [ ] **Step 1: Write §17:** the go/no-go spike's numbers, the two patches, the gate's 19 rows with each row's native
  and macOS run times, the rows left for M3 and M4 and why.
- [ ] **Step 2: Amend the spec's §4.1** as listed.
- [ ] **Step 3: Privacy scan, commit.**

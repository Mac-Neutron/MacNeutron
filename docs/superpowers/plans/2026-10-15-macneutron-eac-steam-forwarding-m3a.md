# EAC microVM, milestone 3a: Steam forwarding Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A program in the guest gets Steam initialised and a 234-byte auth ticket from the Mac's running Steam,
through lsteamclient's Unix-call table forwarded over vsock to MacNeutron's macOS lsteamclient Unix half.

**Architecture:** One descriptor table, generated from lsteamclient's `unixlib_generated.h`, tells a shared C
marshaller how to copy each call's params (strings, fixed-size values, buffers with their size field, opaque host
values). The guest's forwarding Unix half (x86-64 Linux `.so`) serialises a call and sends it over a vsock
connection per thread. `mn-steam-host`, a small macOS process the VM host starts, rebuilds the params with host
buffers, calls the same entry of the macOS lsteamclient Unix half (which loads Mac Steam's `steamclient.dylib`), and
sends the results back. It stands in for the 13 Wine symbols that Unix half imports; its registry writes are
recorded and replayed in the guest's prefix.

**Tech Stack:** C (marshaller, host process, forwarder, tests), Python 3 (generator), Swift (VM host), libkrun vsock,
clang cross-compiling to `x86_64-linux-gnu` in the Fedora builder.

**Spec:** `docs/superpowers/specs/2026-10-15-macneutron-eac-vm-route-design.md` §5.9 and §8 milestone 3a.

## Global Constraints

- Worktree `.claude/worktrees/upstream-master`, branch `exp/upstream-master`; commit locally, never push.
- lsteamclient is the tree in `build/wine-arm64-src/lsteamclient/lsteamclient` (Proton db9e6ff plus
  `wine-arm64/patches/lsteamclient`). Don't modify it: the forwarder and the host process are built beside it.
- The macOS Unix half is `build/wine-arm64/wine.app/Contents/Resources/lib/wine/aarch64-unix/lsteamclient.so`.
- Steam tests use app 480 (Spacewar), as `bridge/probe.sh` does, and print the SteamID and persona name only as
  "ok"/"FAIL" (the probe's redaction). Never print account names, SteamIDs or ticket bytes.
- Mac Steam must already be running and signed in; never start or quit it. A test that finds it not running says so
  and fails.
- 64-bit table only (`__wine_unix_call_funcs`); the WoW64 table fails every call with a logged error.
- A call the marshaller can't carry returns `STATUS_NOT_IMPLEMENTED` (0xC0000002) and logs its name once. It is
  never answered in the guest.
- The builder's `/usr/local/bin/FEX` is our build (binfmt-registered); keep it so after any FEX rebuild.
- Gates assert and exit non-zero on a miss. Privacy scan before committing docs.
- Commit messages end with the two trailer lines this session's system reminder gives.

## Review Focus

1. **A buffer whose size field the naming rules pick wrongly.** Copying too little truncates data; too much reads
   past the caller's buffer. Every rule-matched pair is listed in the generator's report, and SMITE 2's calls are
   checked by hand (Task 6).
2. **Two game threads calling at once.** Each guest thread has its own connection and the host serves each on its
   own thread; replies never cross. Test in Task 4 (eight threads, each with its own ticket).
3. **Mac Steam not running or the host process gone.** A forwarded call returns an error status, and the guest
   logs it, instead of hanging. Test in Task 5 (kill `mn-steam-host` mid-run).
4. **A host path outside the shared library.** The path stand-ins refuse it rather than exposing a Mac path to the
   guest. Test in Task 3.
5. **A malformed or truncated message** (guest bug, dropped connection). The host rejects it and closes that
   connection without touching memory outside the message. Test in Task 3.

---

### Task 1: SMITE 2's Steam calls (the maintainer's run)

**Files:** Create `eac-vm/steam-forward/smite2-calls.txt` (one Unix-call name per line, sorted, from the trace).

- [ ] **Step 1:** Hand off: the maintainer launches SMITE 2 from Mac Steam with today's MacNeutron, launch options
  `/usr/bin/env WINEDEBUG=+steamclient %command%`, reaches the main menu, quits. Ask where the launcher wrote Wine's
  log first and give that path in the hand-off.
- [ ] **Step 2:** Extract the method names lsteamclient's Windows half traced (`winISteam…_…`), map each to its
  Unix-call name (`ISteam…_…`), write the file. Expected: a few dozen names. Privacy scan: names only.
- [ ] **Step 3:** Commit. (The rest of the plan does not wait for this task; Task 6 consumes it.)

### Task 2: x86-64 Linux sysroot in the builder

**Files:** Create `eac-vm/sysroot/build.sh`, `eac-vm/sysroot/packages.txt` (`glibc-devel`, `kernel-headers`,
`libgcc`), `eac-vm/sysroot/packages.lock`.

**Interfaces:** Produces `/var/cache/mn-sysroot-x86_64` in the builder (cached, signature-checked RPMs, installed
with `--forcearch=x86_64`, scriptlets run by FEX through binfmt) and `eac-vm/sysroot/cc-x86_64.sh` (clang
`--target=x86_64-linux-gnu --sysroot=… -fuse-ld=lld`). The same pinning rules as `eac-vm/image/build.sh`.

- [ ] **Step 1 (RED):** `cc-x86_64.sh -o /tmp/hello hello.c` fails (no sysroot).
- [ ] **Step 2:** Write the build; run it twice. Expected: the second run matches `packages.lock`.
- [ ] **Step 3 (GREEN):** `hello` builds, `file` says x86-64, and it prints under FEX. Commit.

### Task 3: Generator and marshaller

**Files:** Create `eac-vm/steam-forward/gen.py`, `marshal.h`, `marshal.c`, `wire.h`, `tests/marshal-test.c`,
`Makefile` (host builds: `marshal-test`).

**Interfaces:**
- `gen.py <lsteamclient dir> <out dir>` writes `descriptors.c` (one `struct mn_call_desc` per 64-bit table entry, in
  table order, compiled against lsteamclient's and Wine's headers so `sizeof` is the compiler's) and
  `report.txt` (per shape counts, every buffer→size pairing, every unsupported call with its reason).
- `struct mn_field { uint16_t offset; uint8_t kind; uint8_t flags; uint32_t size; uint16_t size_offset; uint8_t
  size_width; }`, kinds `MN_STR_IN`, `MN_FIXED` (copied both ways; `MN_IN_ONLY` when `const`), `MN_BUF` (size from
  the field at `size_offset`, times `size` per element), `MN_OPAQUE` (host value carried as is: `u_iface`,
  `u_buffer`, function pointers), `MN_UNSUPPORTED`.
- `struct mn_call_desc { const char *name; uint32_t params_size; uint16_t nfields; const struct mn_field *fields; }`.
- Wire (`wire.h`): request `{u32 magic 'MNSF', u32 index, u32 len}` + params bytes + each pointer field's bytes in
  field order; reply `{u32 magic, u32 status, u32 len}` + params bytes + out-field bytes + a registry log
  (`{u16 kind, u16 type, u32 key_len, u32 name_len, u32 data_len}` + bytes), possibly empty.
- `int mn_pack_request(uint32_t index, const void *params, struct mn_buf *out)`,
  `int mn_unpack_request(const void *msg, size_t len, uint32_t *index, void **host_params, struct mn_arena *arena)`,
  `int mn_pack_reply(uint32_t index, uint32_t status, const void *host_params, struct mn_buf *out)`,
  `int mn_unpack_reply(uint32_t index, const void *msg, size_t len, void *params, uint32_t *status)`. Each returns 0 or
  a negative error, and never reads outside `len`.

Naming rules for buffer sizes (the generator's, listed in its report): the nearest following integer field whose name
starts with `cb`, `cch`, `cub`, `n`/`un` and contains `Size`, `Max`, `Len` or `Count`; for `char *` outputs also
`nMaxLength`-style names. An `overrides` table in `gen.py` corrects or adds pairings by call name.

- [ ] **Step 1 (RED):** `marshal-test` with cases, each run through pack → unpack → fake call → pack reply → unpack
  reply against a hand-made descriptor: string in; `uint32_t *` out; buffer + `cb` out (the call writes 234 bytes into
  a 1024-byte buffer and sets the count); `const` struct in; `u_iface` carried unchanged; an unsupported field →
  `STATUS_NOT_IMPLEMENTED` before any send; a request truncated at every byte → error, no crash (run under
  `-fsanitize=address`); a reply whose buffer is longer than the caller's size field → error. Fails to build.
- [ ] **Step 2:** Write `marshal.c`; `make marshal-test`. Expected GREEN, no sanitizer report.
- [ ] **Step 3:** Write `gen.py`; generate. Expected: 6,539 descriptors; `report.txt` shows the unsupported count
  and every rule-matched pairing. Build `descriptors.c` for macOS arm64 (no errors) and check its `sizeof` values
  against the x86-64 Linux build in Task 5 (`gen.py --check` prints a table checksum both builds must agree on).
- [ ] **Step 4:** Commit (generated files go to `build/eac-vm-work/steam-forward/`, not the repo).

### Task 4: `mn-steam-host`

**Files:** Create `eac-vm/steam-forward/host.c` (server), `host_wine.c` (the 13 stand-ins), `tests/host-client.c`
(macOS client speaking the wire to the socket), `Makefile` targets.

**Interfaces:**
- `mn-steam-host --socket <path> --appid <id> --map <host dir>=<guest dir> [--client-dir <Mac Steam folder>]`:
  listens on a Unix socket; one thread per connection; dlopens the macOS `lsteamclient.so` with `RTLD_GLOBAL` after its
  own stand-ins are loaded; sets `SteamAppId`/`SteamGameId` and `STEAM_COMPAT_CLIENT_INSTALL_PATH` (default
  `~/Library/Application Support/Steam/Steam.AppBundle/Steam/Contents/MacOS`). Exits 3 if that folder has no
  `steamclient.dylib`.
- Stand-ins: `__wine_dbg_header/output/strdup` (log to stderr when `MN_STEAM_DEBUG=1`), `ntdll_umbstowcs` /
  `ntdll_wcstoumbs` (UTF-8 ↔ UTF-16), `ntdll_get_dos_file_name` / `ntdll_get_unix_file_name` (through `--map`: a host
  path under `<host dir>` becomes `<guest dir>` in Wine's DOS form; anything else fails with
  `STATUS_OBJECT_PATH_NOT_FOUND`), `NtQueryInformationToken` (a fixed user SID), `NtCreateKey` / `NtSetValueKey` /
  `NtClose` (record writes to the reply's registry log; keys under `\Registry\User\<sid>\` become `HKCU\…`).
- `host-client` replays a call script against the socket: `steamclient_init`, `CreateInterface("SteamClient017")`,
  `CreateSteamPipe`, `ConnectToGlobalUser`, `GetISteamUser(..., "SteamUser023")`, `GetSteamID` (prints "steamid
  ok"), `GetAuthSessionTicket` (prints "auth ticket <n> bytes"), plus `--threads 8`.

- [ ] **Step 1 (RED):** `host-client` against no server: "connect failed", exit 2.
- [ ] **Step 2:** Write the host; run it with app 480 and the client. Expected: `init ok`, `steamid ok`,
  `auth ticket 234 bytes`, and a registry log with `HKCU\Software\Valve\Steam` values.
- [ ] **Step 3:** `--threads 8`: eight tickets of 234 bytes, no crossed replies. Path stand-in: a path outside
  `--map` fails (Review Focus 4). Commit.

### Task 5: The guest forwarder over vsock

**Files:** Create `eac-vm/steam-forward/guest.c` (the forwarding Unix half: `__wine_unix_call_funcs` of generated
stubs, a connection per thread to vsock CID 2 port 5101, registry-log replay through a callback),
`tests/guest-client.c` (the Task 4 script, linked against the forwarder, x86-64 Linux), `eac-vm/steam-forward/build-guest.sh`
(builder, with Task 2's sysroot). Modify `eac-vm/host/macneutron-vm.swift` (`--steam-appid <id> --steam-map
<host>=<guest>`: start `mn-steam-host` on a socket in the share's parent, `krun_add_vsock` plus
`krun_add_vsock_port2(5101, socket, false)`, stop it on exit), `eac-vm/image/packages.txt` only if the test needs a
library the image lacks.

- [ ] **Step 1 (RED):** In the microVM, `guest-client` (x86-64, under FEX) with no `--steam-appid`: "connect failed".
- [ ] **Step 2:** Implement; gate script `tests/steam-forward.sh` (asserting): `init ok`, `steamid ok`, `auth ticket 234
  bytes`, eight threads, and the median round trip of 1,000 `GetSteamID` calls printed (recorded, no limit yet).
- [ ] **Step 3:** Kill `mn-steam-host` while `guest-client` loops: calls return an error status within 1 s, no hang
  (Review Focus 3). Commit.

### Task 6: SMITE 2's calls are covered

**Files:** Modify `gen.py` overrides as the review finds; create `eac-vm/steam-forward/tests/coverage.sh`.

- [ ] **Step 1 (RED):** `coverage.sh` lists every name in `smite2-calls.txt` whose descriptor has an unsupported
  field or a rule-matched buffer not yet checked by hand (`checked` list in `gen.py`). Expected: non-empty.
- [ ] **Step 2:** For each, read lsteamclient's Windows half and the call's meaning; fix or confirm the pairing in
  `gen.py`. Rerun until `coverage.sh` exits 0.
- [ ] **Step 3:** Record results (`docs/research/2026-10-15-vm-route/README.md` §5: round trip, coverage, sizes),
  commit. Whole-plan review by one fresh reviewer.

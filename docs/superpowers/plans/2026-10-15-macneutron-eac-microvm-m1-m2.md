# EAC microVM, milestones 1-2: FEX fidelity and the microVM Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Our FEX passes every EAC fidelity test, and a libkrun microVM in our own host process boots Fedora's signed
kernel from our shared read-only images with hardware TSO on every vCPU, which FEX then uses.

**Architecture:** FEX work happens in a Linux clone of our FEX, built and tested in the existing Fedora builder VM
(Virtualization.framework). The microVM is libkrun v1.19.6 plus our patches, driven by a small Swift host through
libkrun's C API. The guest is Fedora's signed `vmlinuz` with a dracut initramfs, two EROFS images and a state disk,
and a runner service that executes a script from a virtiofs share and powers off, so every gate is a script.

**Tech Stack:** C/C++ (FEX, tests), Rust (libkrun), Swift 6 (host, `swiftc`), shell, Fedora 44 aarch64 (dnf, dracut,
erofs-utils), Hypervisor.framework.

**Spec:** `docs/superpowers/specs/2026-10-15-macneutron-eac-vm-route-design.md` (approved 2026-10-15). This plan covers
its milestones 1 and 2 (§8); milestones 3-6 get their own plans.

## Global Constraints

- Work in the worktree `.claude/worktrees/upstream-master` (branch `exp/upstream-master`). Commit locally; never push.
- Never check out another branch in `build/wine-arm64-src/fex`: it feeds the macOS build. Linux FEX work happens in
  `build/eac-vm-work/fex-linux`, a clone on branch `eac-fidelity`.
- After every FEX commit, re-export the series: `git -C build/eac-vm-work/fex-linux format-patch --no-signature -o
  eac-vm/fex-patches macneutron..eac-fidelity` (delete the old `.patch` files first), and commit it.
- libkrun is pinned to tag `v1.19.6`; our changes are `eac-vm/libkrun/patches/000N-*.patch`, exported the same way.
- Kernel: Fedora `7.2.10-200.fc44.aarch64`, unmodified, from Fedora's signed RPMs. Command line always carries
  `lockdown=integrity module.sig_enforce=1`.
- Never start or quit Steam on the Mac, never open MacNeutron.app. The builder VM is booted headless (no `VM_GUI`), so
  its sway session and the guest's Steam don't start.
- Downloads (rustup and the Rust toolchain, libkrun's source and crates, Fedora packages in the builder) are approved
  once, with this plan, before Task 4.
- Per task: that task's gate only. The full gate (Task 9) runs once at the end.
- Privacy scan before committing docs: no `/Users/<name>`, SteamIDs or account names.
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`.

## Review Focus

1. **The host fails to set TSO** (an older macOS, a vCPU path we missed). FEX must stay on software TSO, never
   assume. Test in Task 8 (`tso-check` on the unpatched libkrun leaves `HostTSO` unset).
2. **Two hosts on the same `state.img`.** The second must refuse to start, not corrupt the disk. Test in Task 6
   (`a second host on the same images exits with "state.img is in use"`).
3. **A missing image or a kernel in a format we don't load.** The host names the file and exits non-zero within
   5 s instead of hanging. Test in Task 6.
4. **A guest kernel panic.** The host process exits instead of hanging with a dead guest. Test in Task 6 (sysrq crash
   with `panic=5`).
5. **Freed guest memory that never returns to macOS** (`MADV_FREE` is reclaimed lazily on macOS). Test in Task 7.

---

## Milestone 1: FEX fidelity on our FEX

### Task 1: Builder access and the fidelity tests in the repo

**Files:**
- Create: `eac-vm/builder.sh`
- Create: `eac-vm/fex-tests/` (copied from `build/eac-tests-from-poc/`, minus `build/`), with
  `eac-vm/fex-tests/LICENSE` (the PoC's MIT licence)
- Modify: `eac-vm/host/vmhost.swift` (bring in the `VM_SHARES` block from `build/eac-vm-work/eacbench/vmhost/main.swift`)

**Interfaces:**
- Produces: `eac-vm/builder.sh start|stop|ssh <cmd>|sync <host dir> <guest dir>`. Defaults:
  `BUILDER_DIR=build/eac-vm-work/eacvm/fedora/vm`, `BUILDER_KEY=build/eac-vm-work/eacbench/key`,
  `BUILDER_MAC=52:54:00:ea:c0:02`, user `bench`. `start` compiles `eac-vm/host/vmhost.swift` to
  `build/eac-vm-work/vmhost` if older than the source, signs it with `eac-vm/host/ent.plist`, boots headless with
  `VM_CPUS=12 VM_MEM_GB=16`, and waits until ssh answers (timeout 120 s). The guest IP comes from
  `/var/db/dhcpd_leases` by MAC (macOS writes it without leading zeros: `52:54:0:ea:c0:2`).

- [ ] **Step 1:** Write `builder.sh`; run `eac-vm/builder.sh start && eac-vm/builder.sh ssh 'uname -r'`.
  Expected: `7.2.10-200.fc44.aarch64`.
- [ ] **Step 2:** Copy the tests; `eac-vm/builder.sh sync eac-vm/fex-tests '~/fex-tests'`, then
  `eac-vm/builder.sh ssh 'cd ~/fex-tests && ./run-all.sh FEX'` with the PoC's FEX (`/usr/local/bin/FEX`, eac-2610.2).
  Expected: all 19 runs give their expected result (the reference run on this machine).
- [ ] **Step 3:** Privacy scan, commit (`eac-vm: builder VM helper and the PoC's fidelity tests`).

### Task 2: ptrace register writes at syscall stops work on our FEX

**Files:**
- Modify (in `build/eac-vm-work/fex-linux`): `Source/Tools/LinuxEmulation/LinuxSyscalls/PtraceEmu.cpp`,
  and whatever the root cause touches in `Source/Tools/LinuxEmulation/LinuxSyscalls/Syscalls.cpp` or FEXCore's
  syscall dispatch
- Modify: `eac-vm/fex-patches/` (re-export)

**Interfaces:**
- Consumes: Task 1's `builder.sh`, `eac-vm/fex-tests/run-all.sh`.
- Produces: FEX binaries in the builder at `~/fex-linux/build/Bin/` (`FEX`, `FEXServer`, `FEXGetConfig`), and patch
  `0028-*.patch`.

- [ ] **Step 1:** Create the clone: `git clone --branch eac-fidelity build/wine-arm64-src/fex build/eac-vm-work/fex-linux`
  (submodules from the same tree with `git submodule update --init --reference`). Sync it to `~/fex-linux` in the
  builder (excluding `build/`) and build with the cmake line from `eac-vm/guest/provision.sh`.
- [ ] **Step 2 (RED):** `~/fex-tests/run-all.sh ~/fex-linux/build/Bin/FEX`. Expected: `ptrace-inject` UNEXPECTED, and
  `ptrace_inject_test` prints exactly these FAIL lines:
  `unmodified GETREGSET/SETREGSET and GETREGS/SETREGS round trips at entry accepted`,
  `syscall exit stop returns the real result (getpid == child pid) after an entry round trip`,
  `SETREGSET at entry accepted (getpid -> getppid)`, `rewriting orig_rax/rax at syscall entry changes the syscall that runs`,
  `SETREGSET at exit stop accepted (forged rax; getuid still ran after the GETREGS/SETREGS round trip)`,
  `payload parked itself with SIGSTOP`.
- [ ] **Step 3:** Find the root cause with superpowers:systematic-debugging. The same patch passes on the PoC's FEX
  (eac-2610.2), so the cause is in what our base (upstream FEX master, `20bcf3d`) changed under it. The first
  hypothesis to test: the syscall path no longer spills every guest GPR to `Frame->State` and reloads it afterwards,
  so the tracer's writes through the shared block (`ApplyTracerWrites`) land in a state FEX doesn't read back. Confirm
  or reject it with the in-process trace FEX already has (`FEX_PTRACEEMUTRACE`-style logging added to
  `PtraceEmu.cpp` temporarily), not with an external debugger.
- [ ] **Step 4 (GREEN):** Fix it where all syscall stops route through. `run-all.sh` gives all 19 expected results.
- [ ] **Step 5:** Commit in `fex-linux` with a message that states the cause, re-export the series, commit the
  patches in the worktree.

### Task 3: FEX option `HostTSO`

**Files:**
- Modify (in `fex-linux`): `FEXCore/Source/Interface/Config/Config.json.in` (new bool `HostTSO`, default `false`,
  env `FEX_HOSTTSO`, description: "The host already runs this process with TSO memory ordering (a VM whose vCPUs have
  Apple's EnTSO set). FEX then emits no ordering barriers."), `Source/Tools/FEXInterpreter/FEXInterpreter.cpp`
  (`FEX::Kernel::TSO::SetupTSOEmulation`: when `HostTSO` is set, call `CTX->SetHardwareTSOSupport(true)` and return
  before the `PR_GET_MEM_MODEL` probe)
- Modify: `eac-vm/fex-patches/` (re-export, `0029-*.patch`)

**Interfaces:**
- Produces: `FEX_HOSTTSO=1` / `{"Config":{"HostTSO":"1"}}` switches FEX's TSO emulation off. Task 8 sets it.

- [ ] **Step 1 (RED):** In the builder, build `docs/research/2026-10-15-vm-route/harness/` x64-bench for Linux as
  `run-guest.sh` did, and time the `mem_seq_read` row three ways: plain, `FEX_TSOENABLED=0`, `FEX_HOSTTSO=1`.
  Expected before the change: `FEX_HOSTTSO=1` equals plain (about 0.90 s), `FEX_TSOENABLED=0` about 0.46 s.
- [ ] **Step 2:** Implement the option.
- [ ] **Step 3 (GREEN):** `FEX_HOSTTSO=1` is within 10% of `FEX_TSOENABLED=0` on `mem_seq_read`, and
  `run-all.sh` still gives 19 expected results with `FEX_HOSTTSO=1` exported.
- [ ] **Step 4:** Commit, re-export, commit the patches.

## Milestone 2: the microVM

### Task 4: libkrun fork builds

**Files:**
- Create: `eac-vm/libkrun/PIN` (`v1.19.6 <commit sha>`), `eac-vm/libkrun/build.sh`, `eac-vm/libkrun/patches/`
  (empty for now), `eac-vm/libkrun/README.md` (what each patch does; three lines per patch at most)

**Interfaces:**
- Produces: `eac-vm/libkrun/build.sh` → clones to `build/eac-vm-work/libkrun-src` at the pinned commit the first time,
  resets branch `macneutron` to the pin, `git am` every patch, runs `make INIT_BLOB=0 BLK=1`, and installs
  `libkrun.1.dylib` (install name `@rpath/libkrun.1.dylib`) and `include/libkrun.h` into
  `build/eac-vm-work/libkrun/`. Exits non-zero if a patch fails to apply.

- [ ] **Step 1:** Install rustup and the stable toolchain (approved download), write `build.sh`, run it.
  Expected: `nm -gU build/eac-vm-work/libkrun/libkrun.1.dylib | grep -c '_krun_set_kernel$'` prints `1`.
- [ ] **Step 2:** Commit (`eac-vm: libkrun v1.19.6 build for the microVM host`).

### Task 5: Guest images

**Files:**
- Create: `eac-vm/image/build.sh` (runs in the builder), `eac-vm/image/packages.txt` (the package names for
  `base.erofs`), `eac-vm/image/rootfs/` (files copied into `base.erofs`: `etc/fstab`,
  `etc/systemd/system/mn-runner.service`, `usr/libexec/mn-runner`, `usr/share/fex-emu/Config.json`)
- Create (output, ignored): `build/eac-vm-work/images/{vmlinuz,initramfs.img,base.erofs,x86-64-rootfs.erofs,state.img,cmdline.txt,packages.lock}`

**Interfaces:**
- Consumes: Task 2/3's FEX binaries in the builder.
- Produces, for Task 6:
  - `vmlinuz`: `/lib/modules/7.2.10-200.fc44.aarch64/vmlinuz` from the `kernel-core` RPM (dnf verifies Fedora's
    signature on download). `build.sh` prints `file vmlinuz`; Task 6 picks the libkrun format from it (`PE_GZ` for a
    gzip EFI zboot image).
  - `initramfs.img`: `dracut --no-hostonly --kver 7.2.10-200.fc44.aarch64 --add systemd-volatile-root
    --add-drivers "virtio_blk virtio_mmio erofs overlay virtiofs ext4"`.
  - `base.erofs` (label `mn-base`): `dnf --installroot --releasever=44 --setopt=install_weak_deps=False` with
    `packages.txt` (systemd, systemd-udev, util-linux, e2fsprogs, kernel-modules-core at the pinned kernel,
    fex-emu and fex-emu-filesystem for the binfmt units), then our FEX binaries over Fedora's in `/usr/bin`, then
    `rootfs/`. `mkfs.erofs -L mn-base -zlz4hc`.
  - `x86-64-rootfs.erofs`: the `default.erofs` of the pinned `fex-emu-rootfs-fedora`, copied as is, mounted at
    `/usr/share/fex-emu/RootFS/fedora`.
  - `state.img`: 16 GiB sparse ext4, label `mn-state`, mounted at `/home`.
  - Milestone 2's `base.erofs` holds only what the gates need; milestone 3's plan adds Steam's packages, cage and
    Xwayland.
  - `cmdline.txt`: `root=LABEL=mn-base rootfstype=erofs ro systemd.volatile=overlay lockdown=integrity
    module.sig_enforce=1 panic=5 console=hvc0`.
  - `packages.lock`: `rpm -qa --root` of the installroot, sorted, plus the kernel NVR.
  - The runner contract: at boot `mn-runner.service` mounts virtiofs tag `share` at `/mnt/share`; if
    `/mnt/share/run.sh` exists it runs it as root with output to `/mnt/share/run.log`, writes its exit status to
    `/mnt/share/run.status`, and powers off.

- [ ] **Step 1:** Write the files. Run `eac-vm/image/build.sh` in the builder with the share `out` mapped to
  `build/eac-vm-work/images` (`VM_SHARES=out=...` on `builder.sh start`). Expected: all seven files exist,
  `file vmlinuz` names a PE image, and the script prints each file's size.
- [ ] **Step 2:** Run it a second time. Expected: the same `packages.lock`.
- [ ] **Step 3:** Record the sizes in the commit message; commit (`eac-vm: guest images from Fedora's signed kernel`).

### Task 6: The MacNeutronVM host boots the guest

**Files:**
- Create: `eac-vm/host/macneutron-vm.swift`, `eac-vm/host/hv.plist` (`com.apple.security.hypervisor` only),
  `eac-vm/host/build.sh`, `eac-vm/run-guest.sh`

**Interfaces:**
- Consumes: Task 4's dylib and header, Task 5's images.
- Produces:
  - `build/eac-vm-work/macneutron-vm --images <dir> --share <dir> [--cpus N] [--ram-mib M] [--console <file>]`:
    `krun_create_ctx`, `krun_set_vm_config`, `krun_set_kernel` (format from `file vmlinuz`), `krun_add_disk2` for the
    three images (base and x86-64 read-only, raw), `krun_add_virtiofs` tag `share`, `krun_disable_implicit_init`,
    `krun_disable_implicit_vsock`, console to `--console` (default `<share>/console.log`), then `krun_start_enter`.
    Defaults: `--cpus` = performance cores + half the efficiency cores (`hw.perflevel0.physicalcpu`,
    `hw.perflevel1.physicalcpu`), `--ram-mib 8192`.
    Before any libkrun call it checks every image is readable (else prints `missing image: <path>` and exits 2), and
    takes `flock(LOCK_EX|LOCK_NB)` on `state.img` (else prints `state.img is in use` and exits 3).
  - `eac-vm/run-guest.sh <script> [host args…]`: copies the script to `build/eac-vm-work/share/run.sh`, removes old
    `run.log`/`run.status`, runs the host, then prints `run.log` and exits with `run.status`.

- [ ] **Step 1:** Write the host and `build.sh` (`swiftc -O -import-objc-header <libkrun.h>`, rpath to the dylib,
  `codesign -s - --entitlements hv.plist`).
- [ ] **Step 2:** Gate script `uname -r; cat /sys/kernel/security/lockdown; cat /sys/module/module/parameters/sig_enforce;
  getconf PAGESIZE; findmnt -n -o FSTYPE /usr/share/fex-emu/RootFS/fedora; findmnt -n -o FSTYPE /home`.
  Expected: `7.2.10-200.fc44.aarch64`, a line containing `[integrity]`, `Y`, `4096`, `erofs`, `ext4`.
- [ ] **Step 3:** Review Focus tests:
  - a missing `base.erofs` → `missing image:` and exit 2 within 5 s;
  - a second host started while the first runs on the same images → `state.img is in use`, exit 3;
  - a script `echo c > /proc/sysrq-trigger` → the host process exits within 15 s.
- [ ] **Step 4:** Commit (`eac-vm: MacNeutronVM, a libkrun host that boots Fedora's signed kernel`).

### Task 7: Freed guest memory returns to macOS

**Files:**
- Create: `eac-vm/libkrun/patches/0001-*.patch` (in `libkrun-src`: `src/devices/src/virtio/balloon/device.rs`, use
  `MADV_FREE_REUSABLE` on macOS for reported ranges)
- Create: `eac-vm/tests/memhog.c` (`memhog <MiB> <hold s>`: mmap, touch every page, hold, munmap, sleep 60)

**Interfaces:**
- Consumes: Task 6's host. The balloon device must be present with free-page reporting; if v1.19.6 does not add it
  implicitly, the host adds it in this task.

- [ ] **Step 1 (RED):** Run `memhog 4096 10` in the guest (`--ram-mib 8192`). Sample the host's
  `footprint -p <pid>` phys_footprint before, at the hold, and 30 s after the munmap. Expected now: the drop after
  munmap is under 3 GB.
- [ ] **Step 2:** Patch libkrun, rebuild with `build.sh`.
- [ ] **Step 3 (GREEN):** Same run: the footprint drops by at least 3 GB within 30 s of the munmap.
- [ ] **Step 4:** Export the patch, commit.

### Task 8: TSO on every vCPU, and FEX uses it

**Files:**
- Create: `eac-vm/libkrun/patches/0002-*.patch` (in `libkrun-src`: `src/hvf/src/lib.rs`, `HvfVcpu::new`: after the
  `MPIDR_EL1` write, `hv_vcpu_set_sys_reg(vcpuid, HV_SYS_REG_ACTLR_EL1, actlr | 1 << 1)`, read it back, and fail vCPU
  creation if bit 1 isn't set)
- Create: `eac-vm/tests/tso-litmus.c` (the message-passing test of `harness/mp2.c` with a time budget:
  `tso-litmus <ms>`, prints `violations <n> of <m>`, exit 0 only when `n == 0` and `m >= 1000000`)
- Create: `eac-vm/image/rootfs/etc/systemd/system/mn-tso-check.service`, `.../usr/libexec/mn-tso-check`
  (runs `tso-litmus 200`; on success merges `"HostTSO":"1"` into `/usr/share/fex-emu/Config.json`; on failure logs
  `mn-tso-check: <n> violations, FEX keeps software TSO` to the console)
- Modify: `eac-vm/image/build.sh` (builds `tso-litmus` for arm64 into `base.erofs`)

**Interfaces:**
- Consumes: Task 3's `HostTSO`; Task 6's runner.
- Produces: a guest where `/usr/share/fex-emu/Config.json` has `HostTSO` set exactly when the litmus passed.

- [ ] **Step 1 (RED):** Rebuild the images; with the current libkrun run a script printing `tso-litmus 2000` and the
  FEX config. Expected: violations > 0, and no `HostTSO` in the config (Review Focus 1).
- [ ] **Step 2:** Write the libkrun patch; rebuild.
- [ ] **Step 3 (GREEN):** Same script: `violations 0 of` at least 300 M, `HostTSO` is `1`, and `mem_seq_read`
  under FEX (no env) is within 10% of the `FEX_TSOENABLED=0` time.
- [ ] **Step 4:** Export the patch, commit both.

### Task 9: Milestone 2 gate and record

**Files:**
- Modify: `docs/research/2026-10-15-vm-route/README.md` (new section "4. The microVM"), the spec's status line,
  memory `eac-microvm-route`

- [ ] **Step 1:** Full gate, `--cpus 16 --ram-mib 16384` (the benchmarks' configuration):
  - Task 6's identity checks;
  - `tso-litmus 2000`: 0 violations;
  - `gameframe` x86-64 under FEX, median of 3: at most 2.25 ms (5% over 2.14);
    if it misses, the first fix is spec §5.1's QoS: libkrun patch `0003` runs vCPU threads at
    `QOS_CLASS_USER_INTERACTIVE`, then re-measure;
  - Task 7's footprint drop;
  - `fex-tests/run-all.sh`: 19 expected results inside the microVM;
  - cold boot to the runner's first line: recorded (target set from this number in the next plan);
  - image sizes: recorded.
- [ ] **Step 2:** Write the results with the commands that produced them; privacy scan; commit.
- [ ] **Step 3:** Whole-branch review by one fresh reviewer (Opus) over the milestone's commits; fix Critical and
  Important findings, batch the rest.

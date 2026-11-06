# Swift → Rust migration: session handoff

State captured mid-session so work can resume after a restart. This file is disposable; delete it once the
migration lands.

## Goal

Port MacNeutron from Swift to pure Rust. Three threads:

1. **Milestone 1 (done):** the pre-existing untracked Rust workspace is green — `cargo test --workspace` → 126 passed, 0 failed.
2. **Milestone 2 (in progress):** port the CLI — `crates/macneutron-cli`.
3. **Milestone 2 prerequisites (done):** four behavioral modules the CLI needs did not exist in Rust
   (`Launcher` is still missing); the rest are ported.

## Todo list (continue tracking with the `todos` tool)

- [completed] Fix `posix::open_exclusive_lock` `O_CLOEXEC` and confirm green suite
- [in_progress] Port CLI: create `crates/macneutron-cli`
- [pending] Port installer to Rust
- [pending] Port app to Rust
- [pending] Cut Makefile/bundle over to cargo

## Environment

```bash
export PATH="$HOME/.rustup/toolchains/stable-aarch64-apple-darwin/bin:$PATH"
export CARGO_HOME="$HOME/.cargo"
cd ~/Documents/MacProton
cargo check -p macneutron-core           # clean, zero warnings
cargo check --workspace                  # clean, zero warnings
cargo test -p macneutron-core --lib        # 126 passed / 0 failed (re-verified at handoff time)
cargo clippy --workspace                 # not yet run this session
```

## Baseline verified at handoff

Re-run just before writing this file, so the recorded state is real:

- `cargo check -p macneutron-core -p macneutron-cli` → clean, **zero warnings**.
- `cargo test -p macneutron-core --lib` → **126 passed; 0 failed; 0 ignored**.
- `test_support.rs` compiles under `--all-targets`; its only warnings are dead-code, and they disappear as the
  ported tests below start consuming the fixtures.

`@Test` counts, counted from the Swift files (these supersede any count in the prior summary):

| Swift test file | `@Test`s to port |
| --- | --- |
| `RuntimeInstallerTests.swift` | 24 |
| `VMToolsInstallerTests.swift` | 15 |
| `CommandLineToolTests.swift` | 13 |

- `cargo` is not on `PATH`; always prepend the rustup toolchain bin dir.
- Toolchain: 1.99.1 (5f94df478 2026-08-27), target `aarch64-apple-darwin`.
- Swift oracle: `swift test` → 537/537 pass.
- Branch `dxmtrs`, HEAD `3c14bbf`.

## Created / modified this session

### `crates/macneutron-cli` (new crate)

Three files, complete except for tests. `cargo check --workspace` passes with zero warnings.

- `Cargo.toml` — lib `macneutron_cli`, bin `macneutron`, deps `macneutron-core`, `libc`, `plist`.
- `src/main.rs` — gathers `args`/`environment`/`current_exe`, calls `macneutron_cli::run`, exits with its code.
- `src/lib.rs` — `USAGE`, `run`, `install`, `passthrough`, `vm_setup`, `option`, `install_exit_code`,
  `install_message`, `passthrough_target`, `read_bundle_executable`, the `CPU_TYPE_*`/`CPU_SUBTYPE_*` constants
  (libc does not export them), `PASSTHROUGH_ARCHITECTURES`, `spawn` (the `posix_spawn` port),
  `install_setup_handlers`, `stop_setup`, `usage_error`, `failure`.
  - `// TODO: launch (Launcher + notifier) and the toast helper are unported.` marks the only gap.

### `crates/macneutron-core/src/test_support.rs` (new, written this session)

The Rust counterpart of `Tests/MacNeutronCoreTests/Support.swift`. Compiles clean (`cargo check -p macneutron-core
--all-targets`); its only warnings are dead-code, which resolve as the ported tests consume the fixtures.

Contents: `make_temp_dir` (delegates to `crate::rpm_fixture::temp`), `write`, `FakeRunner` + `Call` (implements
`ProcessRunner`, both `run` and `run_with_started`), `wineboot_creating_prefix`, `make_tool_layout`,
`set_runtime_version`, `write_info_plist`, `TEST_IDENTITY`, `steam_environment`, `install_fake_steam_bridge`,
`pe_bytes(machine, certificate)`, `make_signed_wine_app`, `make_log`.

Not ported from `Support.swift` (only port if a test needs it): the `pe_bytes(icons:)` icon builder,
`makeSteamLocation`, `testPreflight`.

### `crates/macneutron-core/src/lib.rs`

Now declares (line ~48):

```rust
#[cfg(test)]
pub(crate) mod rpm_fixture;
#[cfg(test)]
pub(crate) mod test_support;
```

`test_support` is `cfg(test)` only, so the CLI crate cannot see it. The CLI's own tests must define the few
helpers they need locally.

### Earlier this session (already done, do not redo)

- `vm_tools_installer.rs`: `_tools()` helper and the `Command`/`Stdio` import removed; `link_path` → `Self::link_path` ×4;
  `digest` de-closured (it double-borrowed `hash`).
- `vm_tools_installer` wired into `lib.rs` (module + `pub use`).
- `posix::open_flock(path) -> Option<File>` added (inserted before `user_name`): `O_RDWR | O_CREAT | O_CLOEXEC`,
  mode `0o600`, plus `flock(fd, LOCK_EX | LOCK_NB)`; closes the fd and returns `None` when the lock is held.
  Deliberately distinct from `open_exclusive_lock` (`O_EXLOCK`), because `setup.lock` must be opened even when its
  lock is held.
- `VMSetup::run` and `VMSetup::head` ported into `vm_setup.rs`; all five pre-existing dead-code warnings are now
  consumed. New `pub fn run` (~line 115), new `pub fn head` (~line 200), new private `fn build` (~line 230).
- `vm_downloads::standard_fetch()` added (curl-based `Fetch` into `temp_dir()/macneutron-download-<pid>/…`) plus a
  private `unique_stamp()`.
- Root `Cargo.toml` members: `["vm/host", "crates/macneutron-core", "crates/macneutron-cli"]`.

## Exact next steps

1. **Port `Tests/MacNeutronCoreTests/RuntimeInstallerTests.swift` (24 `@Test`s)** into
   `crates/macneutron-core/src/runtime_installer.rs` as `#[cfg(test)] mod tests`, using `crate::test_support`.
   The file's own private helpers must be ported too: `makeEchoLauncher`, `realPath`, `makeFakeWineApp`,
   `fakeIdentity`, `installFake`, `makeInstalledLayout`, `exists`.
   - `installFake` builds a `FakeRunner` where `cp` copies with `std::fs::copy` (unless `cpStatus != 0`) and
     `codesign` answers `codesignStatus`; `scans` answers the two running-executable scans in turn (the last
     repeats when exhausted).
   - `fakeIdentity` reads `Contents/id` from the bundle and returns `None` when missing.
   - Asserts to preserve: `runner.calls.map(\.tool) == ["cp", "codesign"]`; the exact argument vectors
     `["-c","-R",source,new]` and `["--verify","--strict",new]`; `RuntimeInstallError::CopyFailed(1)`'s message
     `"Couldn't copy the runtime into the tool folder (cp exit 1); check free disk space."`.
2. **Port `Tests/MacNeutronCoreTests/VMToolsInstallerTests.swift` (15 `@Test`s)** into
   `crates/macneutron-core/src/vm_tools_installer.rs` as `#[cfg(test)] mod tests`. Private helpers: `makeTools`,
   `text`. The `aToolWithABrokenSignatureIsRefused` test copies `/usr/bin/true` over `macneutron-vm`, appends a byte
   with `open`/`write`/`close`, and expects `RuntimeInstallError`. `gamesWaitWhileTheToolsSwap` uses
   `MicroVM::find(root, environment, Verb::WaitForExitAndRun)`.
3. **Port `Tests/MacNeutronCoreTests/CommandLineToolTests.swift` (13 `@Test`s)** into
   `#[cfg(test)] mod tests` in `crates/macneutron-cli/src/lib.rs`. The CLI crate cannot see `cfg(test)` items of
   `macneutron-core`, so define the few helpers it needs (a temp-dir helper, `write`, `make_signed_wine_app`) in the
   CLI crate itself. Notable tests:
   - `unknownCommandPrintsUsage`: `run(&["frobnicate"],…) == 2`, `run(&[],…) == 2`, plus `USAGE.contains`.
   - `optionParsingRemovesTheFlagAndValue`.
   - `spawnRunsTheTargetAndReturnsItsPid`: spawn `/usr/bin/arch` with `standard_output` to a file, `libc::waitpid`,
     assert status 0 and that the file is exactly `"arm64"`.
   - `vmSetupNeedsAManifestAndAFolder`: the developer-folder symlink case must exit 0. The manifest is `"{}"`, which
     fails `VMSetupManifest::parse`, so the developer-folder check must run before any parsing — it does
     (`is_developer_folder` is the first statement in `VMSetup::run`).
4. **Add an `open_flock` regression test** next to `exclusive_lock_conflicts_and_closes_on_exec` at
   `crates/macneutron-core/src/posix.rs:384-398`: take the lock; a second `open_flock` on the same path is `None`;
   drop the first; it succeeds; and the fd is not inherited across `fork`+`exec`.
5. **Verify:** `cargo test --workspace` (expect ≥126 + new, 0 failures), `cargo clippy --workspace`, and confirm
   `cargo build -p macneutron-cli` produces `target/debug/macneutron` that prints the usage text and exits 2.
6. Then the installer (`crates/macneutron-installer`) — see `Sources/MacNeutronInstaller/main.swift`,
   `Sources/MacNeutronCore/AppInstaller.swift`, `docs/superpowers/specs/2026-10-25-macneutron-installer-design.md`.
   `AppInstaller.swift`, `SteamPlayMode.swift`, `Updater.swift`, `PrefixManager.swift`, `InstallScripts.swift`
   are all absent from Rust.
7. Then the app (`crates/macneutron-app`) — AppKit/SwiftUI across
   `Sources/MacNeutronApp/{AppModel,GamesView,MacNeutronApp,MenuContent,MenuHotKey,SettingsView,SetupView,UpdateModel}.swift`.
8. Cutover last: point `Makefile` `build`/`test`/`bundle`/`clean` at `cargo build --release`/`cargo test`,
   replicate the `.app` bundle assembly from `target/release/`, replicate the codesign entitlements, and only then
   remove `Package.swift`, `Sources/`, `Tests/`.
9. Before declaring the migration complete: re-run the Swift suite as a parity oracle and do a final advisor consult.

## Decisions made (do not relitigate)

1. CLI's first-cut verb set: `install`, `passthrough`, `vm-setup`. `launch` and `toast` deferred.
2. No HTTP crate in `Cargo.lock`, so `VMSetup::head` shells out to `/usr/bin/curl -sI` and
   `vm_downloads::standard_fetch` shells out to `/usr/bin/curl -fsSL --retry 2 -o`. Zero new dependencies.
3. `runtime_installer::install` returns the recorded child calls so ported tests can assert on them.
4. `RuntimeInstallError::Io(String)` for paths Swift satisfied with Foundation errors.
5. `VMToolsInstaller::digest` takes `&[(String, PathBuf)]`.
6. `posix::open_flock` added rather than reusing `open_exclusive_lock` (different lock primitive).
7. CPU constants defined locally in the CLI crate — libc 0.2.191 does not export `CPU_TYPE_*`/`CPU_SUBTYPE_*`.
8. `progress` is `&'static (dyn Fn(&str) + Sync)` — forced by `vm_downloads::Progress`'s implicit `'static`.

## Assumptions

- `O_CLOEXEC` is the correct and sufficient fix for both lock helpers.
- `build.rs` in `macneutron-core` (links `dylib=zstd` and `dylib=lzma`) is load-bearing. Do not touch.
- The `install` verb's exit codes and messages are a stable interface: `Installed`/`Unchanged` → 0, `Deferred` → 3;
  messages `"installed <label>"`, `"unchanged <label>"`, `"deferred: <path> is running"`.
- `USAGE` must contain `"macneutron install --tool-dir"` and `"macneutron passthrough <verb>"`.

## Open risks

- `toast` and `launch` are AppKit/SwiftUI programs. Their Rust equivalents need a GUI story (`objc2-app-kit` for a
  faithful `NSPanel` port, or a full GUI crate). `Cargo.lock` has none.
- The 537 Swift tests vs 126 Rust tests gap is un-audited. Swift test files still needing Rust counterparts:
  `AppModelTests`, `LauncherTests`, `MicroVMTests`, `VMDownloadsTests`, `VMSetupTests`, `VMToolsInstallerTests`,
  `GameDriveTests`, `UninstalledDrivesTests`, `CommandLineToolTests`, `RuntimeInstallerTests`, `CodeIdentityTests`,
  `UpdaterTests`, `SteamPlayModeTests`, `GPTKDiskImageTests`, `GPTKImporterTests`, `DXMTInstallerTests`,
  `PathsTests`, `PreflightTests`, `ShaderPrecacheTests`, `SteamBridgeTests`, `LaunchEnvironmentTests`,
  `PEImageTests`, `RunningProcessesTests`, `GraphicsBackendTests`.
- **Untested new code:** `runtime_installer.rs`, `vm_tools_installer.rs`, `VMSetup::run`, `VMSetup::head`,
  `standard_fetch`, `open_flock`, and the whole CLI have zero tests.
- Bundle packaging + codesign entitlements replication (`com.apple.security.virtualization`) is the highest-risk
  part of the cutover. `Makefile:17` `swift build -c release`, `:20` `swift test`, `:137` copies
  `build/wine-arm64/wine.app`, runs `vm/host/stage.sh`, codesigns `vm/host/hv.plist`.

## Shell gotchas hit repeatedly (the bash tool is fragile)

- The bash tool rejects bare `>` and some `;`: `could not parse command: … '>' must be followed by a word`.
- **Stray XML-ish fragments get injected into commands and `edit` calls.** One became literal file content in
  `vm_setup.rs` this session. If a `sed`/`edit` behaves oddly, immediately `view` the file and check for injected text.
- `sed` replacement text containing `&` expands to the whole match. Use python heredocs instead.
- The `edit` tool's `old_string` must use literal quotes; backslash-escaped `\"` never matches. It errors with
  "file modified since last read" if a bash command touched the file in between — `view` it again first.
- `grep -rln` with no pattern errors. `--skip ""` silently filters everything.
- `view` on paths other than the current file needs the full absolute path.

// MacNeutronVM: boots the EAC guest (Fedora's signed kernel, our EROFS images) in a libkrun microVM, in this process.
// The guest's virtio-gpu has the dxmt context (spec §5.6), served by the Metal transport's executor in this process.
//   macneutron-vm --images <dir> --share <dir> [--cpus N] [--ram-mib M] [--console <file>] [--timeout <s>]
//                 [--steam-appid <id> [--steam-map <host dir>=<guest dir>]] [--metal-test-ops]
// --steam-appid starts mn-steam-host (steam-forward/host/ beside this binary) for the guest's Steam forwarding
// (spec §5.9) and maps guest vsock port 5101 to it; the map defaults to the Mac Steam library's common folder as
// /mnt/games. The map's host side is shared read-only (virtiofs tag `games`) whether or not Steam is forwarded.
// <dir> holds eac-vm/image/build.sh's output. The share is virtiofs tag `share` (/mnt/share in the guest). The process
// exits when the guest powers off.
import Foundation

func fail(_ code: Int32, _ s: String) -> Never {
  FileHandle.standardError.write((s + "\n").data(using: .utf8)!)
  exit(code)
}
func sysctlInt(_ name: String) -> Int {
  var v: Int32 = 0
  var n = MemoryLayout<Int32>.size
  return sysctlbyname(name, &v, &n, nil, 0) == 0 ? Int(v) : 0
}
func sysctlString(_ name: String) -> String {
  var n = 0
  guard sysctlbyname(name, nil, &n, nil, 0) == 0, n > 0 else { return "" }
  var buf = [CChar](repeating: 0, count: n)
  return sysctlbyname(name, &buf, &n, nil, 0) == 0 ? String(cString: buf) : ""
}
// Spec §5.1: every performance core plus half the efficiency cores. Chips name their levels (M5 Pro: "Super" and
// "Performance", no "Efficiency"), so only a level named Efficiency is halved.
func defaultCPUs() -> Int {
  (0..<sysctlInt("hw.nperflevels")).reduce(0) { total, level in
    let cores = sysctlInt("hw.perflevel\(level).physicalcpu")
    return total + (sysctlString("hw.perflevel\(level).name") == "Efficiency" ? cores / 2 : cores)
  }
}
// libkrun's v2 API: strings are (pointer, length) pairs, and a failing call leaves a KrunError in `err`.
var err: KrunError? = nil
func kstr(_ s: String) -> KrunStr {
  let p = strdup(s)!  // ponytail: never freed, a handful of strings for the life of the process
  return KrunStr(data: UnsafePointer(p), len: strlen(p))
}
func check(_ r: KrunResult, _ what: String) {
  if r != KrunResult(KRUN_RESULT_SUCCESS) { fail(1, "\(what): \(String(cString: krun_result_name_cstr(r)))") }
}
func made<T>(_ handle: T?, _ what: String) -> T {
  if let e = err { check(krun_error_result(e), what) }
  guard let h = handle else { fail(1, "\(what): no handle") }
  return h
}

var opts: [String: String] = [:]
// --metal-test-ops (a bare flag): the Metal transport's test ops (eac-vm/metal/gate.sh); a game's VM never has them.
let metalTestOps = CommandLine.arguments.contains("--metal-test-ops")
let argv = CommandLine.arguments.filter { $0 != "--metal-test-ops" }
for i in stride(from: 1, to: argv.count - 1, by: 2) { opts[argv[i]] = argv[i + 1] }
guard let images = opts["--images"], let share = opts["--share"] else {
  fail(64, "usage: macneutron-vm --images <dir> --share <dir> [--cpus N] [--ram-mib M] [--console <file>] [--timeout <s>]")
}
let cpus = Int(opts["--cpus"] ?? "") ?? defaultCPUs()
let ramMiB = UInt32(opts["--ram-mib"] ?? "") ?? 8192
let console = opts["--console"] ?? share + "/console.log"
let timeout = Int(opts["--timeout"] ?? "") ?? 0  // seconds; 0 = run until the guest stops
let steamAppID = opts["--steam-appid"]
let steamMap = opts["--steam-map"]
  ?? FileManager.default.homeDirectoryForCurrentUser.path + "/Library/Application Support/Steam/steamapps/common=/mnt/games"
func path(_ n: String) -> String { images + "/" + n }
let exeDir = URL(fileURLWithPath: CommandLine.arguments[0]).deletingLastPathComponent().path

for n in ["vmlinuz", "initramfs.img", "cmdline.txt", "base.erofs", "x86-64-rootfs.erofs", "wine.erofs", "state.img"]
where access(path(n), R_OK) != 0 {
  fail(2, "missing image: \(path(n))")
}
// One VM per state disk: two writers would corrupt its ext4. The lock lives as long as this process.
let stateLock = open(path("state.img"), O_RDWR)
if stateLock < 0 || flock(stateLock, LOCK_EX | LOCK_NB) != 0 { fail(3, "state.img is in use") }
let cmdline = (try? String(contentsOfFile: path("cmdline.txt"), encoding: .utf8))?
  .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""

// MN_KRUN_LOG: libkrun log level, 0 (off) to 5 (trace); warnings and errors by default.
let logLevel = UInt32(ProcessInfo.processInfo.environment["MN_KRUN_LOG"] ?? "") ?? UInt32(KRUN_LOG_LEVEL_WARN)
check(krun_init_log(-1, logLevel, UInt32(KRUN_LOG_STYLE_AUTO), 0, &err), "log level")
let devices = krun_mmio_device_manager_new()
func add(_ device: KrunAttachDevice?, _ what: String) { krun_mmio_device_manager_add(devices, made(device, what)) }
func disk(_ id: String, _ name: String, readOnly: Bool) {
  let d = made(krun_block_device_new(kstr(id), kstr(path(name)), UInt32(KRUN_DISK_FORMAT_RAW), &err), name)
  krun_block_device_set_read_only(d, readOnly)
  add(d, name)
}
// The guest console (hvc0) goes to the console file. libkrun makes port 0 hvc0 only for a terminal, so it gets a pty
// whose other side is copied into the file.
let consoleFD = open(console, O_WRONLY | O_CREAT | O_TRUNC, 0o644)
var ptyMaster: Int32 = -1, ptySlave: Int32 = -1
if consoleFD < 0 || openpty(&ptyMaster, &ptySlave, nil, nil, nil) != 0 {
  fail(1, "console: \(String(cString: strerror(errno)))")
}
Thread.detachNewThread {
  var buf = [UInt8](repeating: 0, count: 65536)
  while case let n = read(ptyMaster, &buf, buf.count), n > 0 { _ = write(consoleFD, buf, n) }
}
let consoles = krun_console_device_builder()
var port: UInt32 = 0
check(krun_console_builder_add_tty_port(consoles, kstr(""), ptySlave, &port, &err), "console port")
add(krun_console_builder_build(consoles, &err), "console")
disk("base", "base.erofs", readOnly: true)
disk("x86-64", "x86-64-rootfs.erofs", readOnly: true)
disk("wine", "wine.erofs", readOnly: true)
disk("state", "state.img", readOnly: false)
add(krun_fs_device_new(kstr("share"), kstr(share), &err), "share")
// The Mac's Steam library (the map's host side), read-only as tag `games` at /mnt/games: game files stay the Mac's.
let games = String(steamMap.prefix { $0 != "=" })
if access(games, R_OK) == 0 { add(krun_fs_device_new_read_only(kstr("games"), kstr(games), &err), "games") }
add(krun_rng_device_new(&err), "rng")
// Free page reporting gives the guest's freed memory back to macOS (our libkrun's balloon patch).
add(krun_balloon_device_new(&err), "balloon")
// The Metal transport (spec §5.6): virtio-gpu with the dxmt context and an 8 GiB host-visible window for its blobs.
if metalTestOps { mn_executor_start(mn_testops, nil) } else { mn_executor_start(nil, nil) }  // 4b: winemetal's handler
add(krun_gpu_device_without_display(8 << 30), "gpu")
// Steam forwarding: mn-steam-host serves the guest's lsteamclient calls against the Mac's Steam (spec §5.9).
var steamHostPID: pid_t = 0
if let appid = steamAppID {
  let sock = NSTemporaryDirectory() + "mn-steam-\(getpid()).sock"
  unlink(sock)
  let host = Process()
  host.executableURL = URL(fileURLWithPath: exeDir + "/steam-forward/host/mn-steam-host")
  host.arguments = ["--socket", sock, "--appid", appid, "--map", steamMap, "--parent", String(getpid())]
  do { try host.run() } catch { fail(5, "mn-steam-host: \(error)") }
  steamHostPID = host.processIdentifier
  atexit { if steamHostPID > 0 { kill(steamHostPID, SIGTERM) } }
  var waited = 0
  while access(sock, F_OK) != 0 {
    if waited >= 50 || !host.isRunning { fail(5, "mn-steam-host did not start") }
    usleep(100_000); waited += 1
  }
  let vsock = made(krun_vsock_device_new(3, 0, &err), "vsock device")
  krun_vsock_device_add_unix_port(vsock, 5101, kstr(sock), false)
  add(vsock, "vsock")
}
// Network (spec §5.7): gvproxy (eac-vm/net/fetch.sh, beside this binary) answers the guest's frames with the Mac's own
// sockets, over a unixgram socket in vfkit's framing. The guest takes gvproxy's static lease for this MAC
// (192.168.127.2, gateway and DNS 192.168.127.1). No gvproxy, no network.
if access(exeDir + "/gvproxy", X_OK) == 0 {
  let sock = NSTemporaryDirectory() + "mn-net-\(getpid()).sock"
  unlink(sock)
  let gvproxy = Process()
  gvproxy.executableURL = URL(fileURLWithPath: "/bin/sh")
  // libkrun ends this process with _exit, so a watchdog stops gvproxy once the VM host is gone.
  gvproxy.arguments = ["-c", """
    "$0" -listen-vfkit "unixgram://$1" -ssh-port -1 -log-file "$1.log" & g=$!
    while kill -0 "$2" 2>/dev/null && kill -0 $g 2>/dev/null; do sleep 1; done
    kill $g 2>/dev/null; rm -f "$1" "$1.log"
    """, exeDir + "/gvproxy", sock, String(getpid())]
  do { try gvproxy.run() } catch { fail(6, "gvproxy: \(error)") }
  var waited = 0
  while access(sock, F_OK) != 0 {
    if waited >= 50 || !gvproxy.isRunning { fail(6, "gvproxy did not start, see \(sock).log") }
    usleep(100_000); waited += 1
  }
  // No offloads: gvproxy takes ordinary MTU-sized frames.
  let mac: [UInt8] = [0x5a, 0x94, 0xef, 0xe4, 0x0c, 0xee]
  let net = mac.withUnsafeBufferPointer {
    krun_net_device_new_unixgram_path(kstr("net0"), kstr(sock), KrunBytes(data: $0.baseAddress, len: 6), 0,
                                      UInt32(KRUN_NET_FLAGS_VFKIT), &err)
  }
  add(net, "network")
}
// Fedora's vmlinuz is an EFI zboot image; our libkrun's PE loader reads its header (gzip or zstd payload).
let payload = made(krun_payload_load_external(kstr(path("vmlinuz")), UInt32(KRUN_KERNEL_FORMAT_PE_GZ),
  kstr(path("initramfs.img")), kstr(cmdline), &err), "kernel")
var builder = krun_vmm_builder_new()
check(krun_vmm_builder_vcpus(&builder, UInt8(cpus), &err), "\(cpus) vCPUs")
check(krun_vmm_builder_ram_mib(&builder, ramMiB, &err), "\(ramMiB) MiB")
krun_vmm_builder_payload(&builder, payload)
krun_vmm_builder_devices(&builder, devices)
FileHandle.standardError.write("macneutron-vm: \(cpus) vCPUs, \(ramMiB) MiB\n".data(using: .utf8)!)
if timeout > 0 {
  // A guest that never powers off (an initrd that cannot find its root, a failed mount) must not hold the host and
  // state.img's lock forever.
  DispatchQueue.global().asyncAfter(deadline: .now() + .seconds(timeout)) {
    FileHandle.standardError.write("timeout: the guest still runs after \(timeout) s, see \(console)\n".data(using: .utf8)!)
    _exit(4)
  }
}
let vmm = made(krun_vmm_builder_build(&builder, &err), "build the VM")
// The VM runs off the main thread: the executor needs the main queue for Metal's main-thread calls (spec §5.6).
Thread.detachNewThread {
  krun_vmm_run(vmm)
  exit(0)
}
dispatchMain()

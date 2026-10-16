// MacNeutronVM: boots the EAC guest (Fedora's signed kernel, our EROFS images) in a libkrun microVM, in this process.
//   macneutron-vm --images <dir> --share <dir> [--cpus N] [--ram-mib M] [--console <file>] [--timeout <s>]
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
func check(_ r: Int32, _ what: String) {
  if r < 0 { fail(1, "\(what): \(String(cString: strerror(-r)))") }
}

var opts: [String: String] = [:]
let argv = CommandLine.arguments
for i in stride(from: 1, to: argv.count - 1, by: 2) { opts[argv[i]] = argv[i + 1] }
guard let images = opts["--images"], let share = opts["--share"] else {
  fail(64, "usage: macneutron-vm --images <dir> --share <dir> [--cpus N] [--ram-mib M] [--console <file>] [--timeout <s>]")
}
let cpus = Int(opts["--cpus"] ?? "") ?? defaultCPUs()
let ramMiB = UInt32(opts["--ram-mib"] ?? "") ?? 8192
let console = opts["--console"] ?? share + "/console.log"
let timeout = Int(opts["--timeout"] ?? "") ?? 0  // seconds; 0 = run until the guest stops
func path(_ n: String) -> String { images + "/" + n }

for n in ["vmlinuz", "initramfs.img", "cmdline.txt", "base.erofs", "x86-64-rootfs.erofs", "state.img"]
where access(path(n), R_OK) != 0 {
  fail(2, "missing image: \(path(n))")
}
// One VM per state disk: two writers would corrupt its ext4. The lock lives as long as this process.
let stateLock = open(path("state.img"), O_RDWR)
if stateLock < 0 || flock(stateLock, LOCK_EX | LOCK_NB) != 0 { fail(3, "state.img is in use") }
let cmdline = (try? String(contentsOfFile: path("cmdline.txt"), encoding: .utf8))?
  .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""

// MN_KRUN_LOG: libkrun log level, 0 (off) to 5 (trace); warnings and errors by default.
check(krun_set_log_level(UInt32(ProcessInfo.processInfo.environment["MN_KRUN_LOG"] ?? "") ?? 2), "log level")
let ctx = krun_create_ctx()
check(ctx, "create context")
let id = UInt32(ctx)
check(krun_set_vm_config(id, UInt8(cpus), ramMiB), "vm config (\(cpus) vCPUs, \(ramMiB) MiB)")
// Fedora's vmlinuz is an EFI zboot image; our libkrun's PE loader reads its header (gzip or zstd payload).
check(krun_set_kernel(id, path("vmlinuz"), UInt32(KRUN_KERNEL_FORMAT_PE_GZ), path("initramfs.img"), cmdline), "kernel")
check(krun_add_disk2(id, "base", path("base.erofs"), UInt32(KRUN_DISK_FORMAT_RAW), true), "base.erofs")
check(krun_add_disk2(id, "x86-64", path("x86-64-rootfs.erofs"), UInt32(KRUN_DISK_FORMAT_RAW), true), "x86-64-rootfs.erofs")
check(krun_add_disk2(id, "state", path("state.img"), UInt32(KRUN_DISK_FORMAT_RAW), false), "state.img")
check(krun_add_virtiofs(id, "share", share), "share")
check(krun_disable_implicit_vsock(id), "vsock")
check(krun_set_console_output(id, console), "console")
FileHandle.standardError.write("macneutron-vm: \(cpus) vCPUs, \(ramMiB) MiB\n".data(using: .utf8)!)
if timeout > 0 {
  // A guest that never powers off (an initrd that cannot find its root, a failed mount) must not hold the host and
  // state.img's lock forever.
  DispatchQueue.global().asyncAfter(deadline: .now() + .seconds(timeout)) {
    FileHandle.standardError.write("timeout: the guest still runs after \(timeout) s, see \(console)\n".data(using: .utf8)!)
    _exit(4)
  }
}
check(krun_start_enter(id), "start")

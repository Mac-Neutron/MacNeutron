// MacNeutronVM: boots the EAC guest (Fedora's signed kernel, our EROFS images) in a libkrun microVM, in this process.
//   macneutron-vm --images <dir> --share <dir> [--cpus N] [--ram-mib M] [--console <file>]
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
func check(_ r: Int32, _ what: String) {
  if r < 0 { fail(1, "\(what): \(String(cString: strerror(-r)))") }
}

var opts: [String: String] = [:]
let argv = CommandLine.arguments
for i in stride(from: 1, to: argv.count - 1, by: 2) { opts[argv[i]] = argv[i + 1] }
guard let images = opts["--images"], let share = opts["--share"] else {
  fail(64, "usage: macneutron-vm --images <dir> --share <dir> [--cpus N] [--ram-mib M] [--console <file>]")
}
// Default vCPUs: the performance cores plus half the efficiency cores (spec §5.1).
let cpus = Int(opts["--cpus"] ?? "") ?? sysctlInt("hw.perflevel0.physicalcpu") + sysctlInt("hw.perflevel1.physicalcpu") / 2
let ramMiB = UInt32(opts["--ram-mib"] ?? "") ?? 8192
let console = opts["--console"] ?? share + "/console.log"
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

check(krun_set_log_level(2), "log level")  // warnings and errors
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
check(krun_start_enter(id), "start")

// Minimal Virtualization.framework host for the VM-route benchmarks: EFI-boots a raw disk image, NAT networking,
// serial console to a file, a virtiofs share and a vsock device. Usage:
//   vmhost <dir>   (dir holds disk.raw, seed.iso, efi.vars; console goes to dir/console.log)
import Foundation
import Virtualization
import AppKit
setvbuf(stdout, nil, _IONBF, 0)

let dir = URL(fileURLWithPath: CommandLine.arguments[1])
let cpus = Int(ProcessInfo.processInfo.environment["VM_CPUS"] ?? "8")!
let memGB = UInt64(ProcessInfo.processInfo.environment["VM_MEM_GB"] ?? "12")!

func fail(_ s: String) -> Never { FileHandle.standardError.write((s + "\n").data(using: .utf8)!); exit(1) }

let config = VZVirtualMachineConfiguration()
config.cpuCount = cpus
config.memorySize = memGB << 30
let platform = VZGenericPlatformConfiguration()
let idURL = dir.appendingPathComponent("machine.id")
if let data = try? Data(contentsOf: idURL), let id = VZGenericMachineIdentifier(dataRepresentation: data) {
  platform.machineIdentifier = id
} else {
  try! platform.machineIdentifier.dataRepresentation.write(to: idURL)
}
config.platform = platform

let varsURL = dir.appendingPathComponent("efi.vars")
let store: VZEFIVariableStore
if FileManager.default.fileExists(atPath: varsURL.path) {
  store = VZEFIVariableStore(url: varsURL)
} else {
  store = try! VZEFIVariableStore(creatingVariableStoreAt: varsURL)
}
// Secure Boot (VM_SECUREBOOT=1): Microsoft's KEK, UEFI CA and revocation list, and Apple's default platform key, so a
// distribution's Microsoft-signed shim boots its signed kernel with module signature enforcement and lockdown.
if ProcessInfo.processInfo.environment["VM_SECUREBOOT"] == "1" {
  do {
    try store.enrollDefaultSecureBootSignatures()
    do { try store.enableSecureBootUsingDefaultPlatformKey() } catch { print("secure boot: \(error) (already enabled?)") }
    print("secure boot: enrolled and enabled")
  } catch { fail("secure boot: \(error)") }
}
let boot = VZEFIBootLoader()
boot.variableStore = store
config.bootLoader = boot

var disks: [VZStorageDeviceConfiguration] = []
let disk = try! VZDiskImageStorageDeviceAttachment(url: dir.appendingPathComponent("disk.raw"), readOnly: false,
                                                   cachingMode: .cached, synchronizationMode: .fsync)
disks.append(VZVirtioBlockDeviceConfiguration(attachment: disk))
let seedURL = dir.appendingPathComponent("seed.iso")
if FileManager.default.fileExists(atPath: seedURL.path) {
  disks.append(VZVirtioBlockDeviceConfiguration(attachment: try! VZDiskImageStorageDeviceAttachment(url: seedURL, readOnly: true)))
}
config.storageDevices = disks

let net = VZVirtioNetworkDeviceConfiguration()
net.attachment = VZNATNetworkDeviceAttachment()
net.macAddress = VZMACAddress(string: ProcessInfo.processInfo.environment["VM_MAC"] ?? "52:54:00:ea:c0:01")!
config.networkDevices = [net]

let consoleURL = dir.appendingPathComponent("console.log")
FileManager.default.createFile(atPath: consoleURL.path, contents: nil)
let serial = VZVirtioConsoleDeviceSerialPortConfiguration()
serial.attachment = try! VZFileSerialPortAttachment(url: consoleURL, append: true)
config.serialPorts = [serial]

let share = VZVirtioFileSystemDeviceConfiguration(tag: "share")
share.share = VZSingleDirectoryShare(directory: VZSharedDirectory(url: dir.appendingPathComponent("share"), readOnly: false))
var shares: [VZDirectorySharingDeviceConfiguration] = [share]
if ProcessInfo.processInfo.environment["VM_ROSETTA"] == "1" {
  let rosetta = VZVirtioFileSystemDeviceConfiguration(tag: "rosetta")
  rosetta.share = try! VZLinuxRosettaDirectoryShare()
  shares.append(rosetta)
}
config.directorySharingDevices = shares
config.socketDevices = [VZVirtioSocketDeviceConfiguration()]
config.entropyDevices = [VZVirtioEntropyDeviceConfiguration()]
config.memoryBalloonDevices = [VZVirtioTraditionalMemoryBalloonDeviceConfiguration()]


// Benchmark transport (VM_VBENCH=1): a custom virtio device with one doorbell queue (ping: the element's 8 bytes come
// back plus one) and one 64 MB shared memory region over host memory, where a host thread consumes the guest's ring.
final class VBDevice: NSObject, VZCustomVirtioDeviceConfigurationDelegate, VZCustomVirtioDeviceDelegate {
  var device: VZCustomVirtioDevice?
  let size: UInt64 = 64 << 20
  let mem: UnsafeMutableRawPointer
  override init() {
    // A VM region of its own (mapMemory shares it through XPC shared memory, which refuses heap memory).
    mem = mmap(nil, Int(64 << 20), PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0)!
  }
  func customVirtioConfiguration(_ configuration: VZCustomVirtioDeviceConfiguration, didCreateDevice device: VZCustomVirtioDevice) {
    self.device = device
    device.delegate = self
  }
  func customVirtioDeviceDidAcceptDriverOk(_ device: VZCustomVirtioDevice) {
    device.sharedMemoryRegions[0].mapMemory(mem, atOffset: 0, size: size) { error in
      if let error { print("vbench: map failed: \(error)") } else { print("vbench: shared memory mapped"); vb_start_consumer(self.mem) }
    }
  }
  func customVirtioDevice(_ device: VZCustomVirtioDevice, didReceiveNotificationFor queue: VZVirtioQueue) {
    while let e = queue.nextElement() {
      var v: UInt64 = 0
      _ = try? e.readBytes(intoBuffer: &v, exactLength: 8)
      v += 1
      _ = try? e.writeBuffer(&v, exactLength: 8)
      e.returnToQueue()
    }
  }
}
let vbDevice = VBDevice()
if ProcessInfo.processInfo.environment["VM_VBENCH"] == "1" {
  let c = VZCustomVirtioDeviceConfiguration()
  c.deviceID = 63
  c.pciClassID = 0xff
  c.pciSubclassID = 0
  c.virtioQueueCount = 1
  c.sharedMemoryRegions = [VZVirtioSharedMemoryRegionConfiguration(regionID: 0, size: 64 << 20)]
  c.provider = VZCustomVirtioDeviceDelegateProvider(deviceQueue: DispatchQueue(label: "vbench", qos: .userInteractive), delegate: vbDevice)
  config.customVirtioDevices = [c]
}
if CommandLine.arguments.count > 2 && CommandLine.arguments[2] == "baseline" { vb_baseline(); exit(0) }
let gui = ProcessInfo.processInfo.environment["VM_GUI"] == "1"
if gui {
  let gpu = VZVirtioGraphicsDeviceConfiguration()
  gpu.scanouts = [VZVirtioGraphicsScanoutConfiguration(widthInPixels: 2560, heightInPixels: 1600)]
  config.graphicsDevices = [gpu]
  config.keyboards = [VZUSBKeyboardConfiguration()]
  config.pointingDevices = [VZUSBScreenCoordinatePointingDeviceConfiguration()]
  let sound = VZVirtioSoundDeviceConfiguration()
  let out = VZVirtioSoundDeviceOutputStreamConfiguration()
  out.sink = VZHostAudioOutputStreamSink()
  sound.streams = [out]
  config.audioDevices = [sound]
}
do { try config.validate() } catch { fail("invalid configuration: \(error)") }
let vm = VZVirtualMachine(configuration: config)
final class Watcher: NSObject, VZVirtualMachineDelegate {
  func guestDidStop(_ vm: VZVirtualMachine) { print("guest stopped"); exit(0) }
  func virtualMachine(_ vm: VZVirtualMachine, didStopWithError error: Error) { fail("stopped: \(error)") }
}
let watcher = Watcher()
vm.delegate = watcher
vm.start { result in
  if case .failure(let e) = result { fail("start failed: \(e)") }
  print("started")
}
signal(SIGTERM, SIG_IGN)
let term = DispatchSource.makeSignalSource(signal: SIGTERM, queue: .main)
term.setEventHandler { if vm.canRequestStop { try? vm.requestStop() } else { exit(0) } }
term.resume()
if gui {
  let app = NSApplication.shared
  app.setActivationPolicy(.regular)
  let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 1280, height: 800),
                        styleMask: [.titled, .closable, .miniaturizable, .resizable], backing: .buffered, defer: false)
  window.title = "MacNeutron EAC VM"
  let view = VZVirtualMachineView()
  view.virtualMachine = vm
  view.capturesSystemKeys = true
  view.automaticallyReconfiguresDisplay = true
  window.contentView = view
  window.center()
  window.makeKeyAndOrderFront(nil)
  app.activate(ignoringOtherApps: true)
  app.run()
} else {
  dispatchMain()
}

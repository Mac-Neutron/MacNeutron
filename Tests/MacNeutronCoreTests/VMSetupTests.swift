import CryptoKit
import Foundation
import Testing
@testable import MacNeutronCore

private func sha(_ data: Data) -> String { SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined() }
private func sha(_ url: URL) throws -> String { sha(try Data(contentsOf: url)) }

private func run(_ tool: String, _ arguments: [String]) throws {
    let p = Process()
    p.executableURL = URL(filePath: tool)
    p.arguments = arguments
    try p.run()
    p.waitUntilExit()
    #expect(p.terminationStatus == 0)
}

/// Counts the requests of the setup's real downloader (file: URLs here).
private final class Requests: @unchecked Sendable {
    private let lock = NSLock()
    private var count = 0
    var made: Int { lock.withLock { count } }
    var fetch: ValveClient.Fetch {
        { url in
            self.lock.withLock { self.count += 1 }
            return try await VMDownloads.download(url)
        }
    }
}

private struct SetupFixture {
    let manifest: URL
    let mkfs: URL
    let vm: URL
}

/// A kernel package (vmlinuz and /usr/bin/demo), FEX's root package, and parts whose recipes build a tiny base and
/// initramfs, all served from file: URLs; mkfs.erofs stands in as a script that keeps the tar it's given.
private func makeSetup() throws -> SetupFixture {
    let tmp = try makeTempDir(), rpms = tmp.appending(path: "rpms", directoryHint: .isDirectory)
    try FileManager.default.createDirectory(at: rpms, withIntermediateDirectories: true)
    let kver = "7.2.10-200.fc44.aarch64", demo = Data("demo\n".utf8), initBytes = Data("#!/bin/sh\n".utf8)
    let kernel = rpms.appending(path: "kernel-core-7.2.10-200.fc44.aarch64.rpm")
    try RPMFixture.write(to: kernel, name: "kernel-core", version: "7.2.10", release: "200.fc44",
                         files: [.init(path: "/lib/modules/\(kver)/vmlinuz", bytes: Data("kernel\n".utf8), inode: 1),
                                 .init(path: "/usr/bin/demo", bytes: demo, inode: 2, mode: 0o100755)])
    let fex = rpms.appending(path: "fex-emu-rootfs-fedora-1-1.fc44.noarch.rpm")
    let erofs = Data("an x86-64 root\n".utf8)
    try RPMFixture.write(to: fex, name: "fex-emu-rootfs-fedora", version: "1", release: "1.fc44", arch: "noarch",
                         files: [.init(path: "/usr/share/fex-emu/RootFS/default.erofs", bytes: erofs, inode: 1)])
    let parts = tmp.appending(path: "parts", directoryHint: .isDirectory)
    try write("d\t/\t755\t0\t0\t-\t-\t-\t-\t-\nd\t/usr\t755\t0\t0\t-\t-\t-\t-\t-\nd\t/usr/bin\t755\t0\t0\t-\t-\t-\t-\t-\n"
              + "f\t/usr/bin/demo\t755\t0\t0\t5\t\(sha(demo))\tpkg:\(kernel.lastPathComponent)\t-\t-\n",
              to: parts.appending(path: "recipe/base.tsv"))
    try write("d\t/\t755\t0\t0\t-\t-\t-\t-\t-\nf\t/init\t755\t0\t0\t10\t\(sha(initBytes))\toverlay\t-\t-\n",
              to: parts.appending(path: "recipe/initramfs.tsv"))
    try FileManager.default.createDirectory(at: parts.appending(path: "overlay"), withIntermediateDirectories: true)
    try initBytes.write(to: parts.appending(path: "overlay/\(sha(initBytes))"))
    try write("wine\n", to: tmp.appending(path: "wine/bin/wine"))
    try run("/usr/bin/tar", ["-C", tmp.appending(path: "wine").path, "-cf", parts.appending(path: "wine.tar").path, "."])
    for (name, text) in [("cmdline.txt", "root=LABEL=mn-base\n"), ("packages.lock", "kernel-core\n"),
                         ("state-template.img", "empty ext4\n"), ("share/bin/mn-input", "input\n"),
                         ("share/bin/lib/plugin.so", "a 32-bit plugin\n")] {
        try write(text, to: parts.appending(path: name))
    }
    let archive = tmp.appending(path: "parts.tar.xz")
    try run("/usr/bin/tar", ["-C", parts.path, "-cJf", archive.path, "."])
    func package(_ url: URL, _ name: String, _ version: String, _ release: String, _ arch: String) throws -> [String: Any] {
        ["file": url.lastPathComponent, "name": name, "version": version, "release": release, "arch": arch,
         "size": try FileManager.default.attributesOfItem(atPath: url.path)[.size] as! Int, "sha256": try sha(url)]
    }
    let manifest: [String: Any] = [
        "version": 1,
        "parts": ["url": archive.absoluteString, "size": try FileManager.default.attributesOfItem(atPath: archive.path)[.size] as! Int,
                  "sha256": try sha(archive)],
        "kernel": kver,
        "packages": [try package(kernel, "kernel-core", "7.2.10", "200.fc44", "aarch64")],
        "fexRootfs": ["package": try package(fex, "fex-emu-rootfs-fedora", "1", "1.fc44", "noarch"),
                      "image": "/usr/share/fex-emu/RootFS/default.erofs", "imageSha256": sha(erofs)],
        "koji": ["base": "https://kojipkgs.fedoraproject.org/packages/", "signedKey": "6d9f90a6"],
        "mirrors": [rpms.absoluteString],
    ]
    let manifestFile = tmp.appending(path: "vm-manifest.json")
    try JSONSerialization.data(withJSONObject: manifest).write(to: manifestFile)
    let mkfs = tmp.appending(path: "mkfs.erofs")
    try write("#!/bin/sh\nif [ -n \"$6\" ]; then cp \"$6\" \"$5\"; else cat > \"$5\"; fi\n", to: mkfs, executable: true)
    return SetupFixture(manifest: manifestFile, mkfs: mkfs, vm: tmp.appending(path: "vm", directoryHint: .isDirectory))
}

private func setUp(_ f: SetupFixture, requests: Requests = Requests(), minFreeGB: Int = 0,
                   running: @escaping () -> [String] = { [] }) async throws -> Int32 {
    try await VMSetup.run(manifest: f.manifest, vm: f.vm, mkfs: f.mkfs, minFreeGB: minFreeGB, fetch: requests.fetch,
                          runningExecutables: running, progress: { _ in })
}

@Test func aFullRunLeavesACompleteImageSet() async throws {
    let f = try makeSetup()
    try write("a game's disk\n", to: f.vm.appending(path: "state/1.img"))
    #expect(try await setUp(f) == 0)
    let images = f.vm.appending(path: "images")
    for name in ["vmlinuz", "initramfs.img", "base.erofs", "x86-64-rootfs.erofs", "wine.erofs", "state-template.img",
                 "cmdline.txt", "packages.lock"] {
        #expect(FileManager.default.fileExists(atPath: images.appending(path: name).path), "\(name)")
    }
    #expect(try String(contentsOf: images.appending(path: "manifest.sha256"), encoding: .utf8)
            == VMSetup.manifestHash(f.manifest)! + "\n")
    #expect(try Data(contentsOf: images.appending(path: "vmlinuz")) == Data("kernel\n".utf8))
    #expect(try Data(contentsOf: images.appending(path: "x86-64-rootfs.erofs")) == Data("an x86-64 root\n".utf8))
    #expect(try Data(contentsOf: f.vm.appending(path: "share/bin/mn-input")) == Data("input\n".utf8))
    // A tool in a folder of share/bin (the ALSA plugin per width, lib/ and lib64/): at the same path.
    #expect(try Data(contentsOf: f.vm.appending(path: "share/bin/lib/plugin.so")) == Data("a 32-bit plugin\n".utf8))
    #expect(try Data(contentsOf: f.vm.appending(path: "state/1.img")) == Data("a game's disk\n".utf8))
    #expect(!FileManager.default.fileExists(atPath: f.vm.appending(path: "setup").path))
    #expect(MicroVM.imagesComplete(f.vm))
}

@Test func aFailedSetupLeavesOnlyItsDownloads() async throws {
    // A failure keeps the cache for the next try and nothing else: parts, store and a half-built set are gigabytes.
    let f = try makeSetup()
    try write("#!/bin/sh\nexit 1\n", to: f.mkfs, executable: true)
    await #expect(throws: VMSetupError.tool("mkfs.erofs", 1)) { try await setUp(f) }
    #expect(try FileManager.default.contentsOfDirectory(atPath: f.vm.appending(path: "setup").path) == ["cache"])
}

@Test func tooLittleSpaceIsRefusedBeforeAnyDownload() async throws {
    let f = try makeSetup(), requests = Requests()
    await #expect(throws: VMSetupError.self) { try await setUp(f, requests: requests, minFreeGB: 100_000) }
    #expect(requests.made == 0)
}

@Test func aRunningVMDefersTheSwapAndTheNextRunFinishesIt() async throws {
    let f = try makeSetup()
    try write("old\n", to: f.vm.appending(path: "images/manifest.sha256"))
    let vmExecutable = f.vm.appending(path: "macneutron-vm").path
    #expect(try await setUp(f, running: { [vmExecutable] }) == 75)
    // The store goes once the images are built: the setup's peak on disk is downloads, parts and images.
    #expect(!FileManager.default.fileExists(atPath: f.vm.appending(path: "setup/store").path))
    #expect(try String(contentsOf: f.vm.appending(path: "images/manifest.sha256"), encoding: .utf8) == "old\n")
    let requests = Requests()
    #expect(try await setUp(f, requests: requests) == 0)
    #expect(requests.made == 0)
    #expect(try String(contentsOf: f.vm.appending(path: "images/manifest.sha256"), encoding: .utf8)
            == VMSetup.manifestHash(f.manifest)! + "\n")
}

@Test func theDevelopersFolderIsLeftAlone() async throws {
    let f = try makeSetup()
    let target = try makeTempDir().appending(path: "eac-vm-work", directoryHint: .isDirectory)
    try FileManager.default.createDirectory(at: target, withIntermediateDirectories: true)
    try FileManager.default.createSymbolicLink(at: f.vm, withDestinationURL: target)
    #expect(try await setUp(f) == 0)
    #expect(try FileManager.default.contentsOfDirectory(atPath: target.path).isEmpty)
}

@Test func theManifestsHashLeavesOutWhereThePartsAre() throws {
    // A release whose VM didn't change moves only the parts' URL: the image set is the same, nothing downloads again.
    let dir = try makeTempDir()
    let a = dir.appending(path: "a.json"), b = dir.appending(path: "b.json"), c = dir.appending(path: "c.json")
    try write(#"{"version": 1, "parts": {"url": "https://x/v0.6.0/p.tar.xz", "size": 3, "sha256": "aa"}, "kernel": "k"}"#, to: a)
    try write(#"{"kernel": "k", "parts": {"sha256": "aa", "size": 3, "url": "https://x/v0.6.1/p.tar.xz"}, "version": 1}"#, to: b)
    try write(#"{"version": 1, "parts": {"url": "https://x/v0.6.1/p.tar.xz", "size": 3, "sha256": "bb"}, "kernel": "k"}"#, to: c)
    #expect(VMSetup.manifestHash(a) == VMSetup.manifestHash(b))
    #expect(VMSetup.manifestHash(a) != VMSetup.manifestHash(c))
}

@Test func aSecondSetupInTheFolderWaits() async throws {
    // A crashed app's setup still running when the relaunch starts another: one at a time, the other waits (75).
    let f = try makeSetup(), requests = Requests()
    try FileManager.default.createDirectory(at: f.vm, withIntermediateDirectories: true)
    let held = open(f.vm.appending(path: "setup.lock").path, O_RDWR | O_CREAT, 0o600)
    #expect(held >= 0 && flock(held, LOCK_EX | LOCK_NB) == 0)
    defer { close(held) }
    #expect(try await setUp(f, requests: requests) == 75)
    #expect(requests.made == 0)
}

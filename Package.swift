// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "MacNeutron",
    platforms: [.macOS("27.0")],
    products: [
        .executable(name: "macneutron", targets: ["macneutron"]),
        .executable(name: "MacNeutronApp", targets: ["MacNeutronApp"]),
        .executable(name: "MacNeutronInstaller", targets: ["MacNeutronInstaller"]),
        .library(name: "MacNeutronCore", targets: ["MacNeutronCore"]),
    ],
    targets: [
        // zstd's decompressor (v1.5.7, BSD-3-Clause; Sources/CZstd/README), for the VM setup's RPM payloads.
        .target(name: "CZstd", exclude: ["LICENSE", "README"],
                cSettings: [.define("ZSTD_DISABLE_ASM"), .define("ZSTD_LEGACY_SUPPORT", to: "0")]),
        .target(name: "MacNeutronCore", dependencies: ["CZstd"]),
        .executableTarget(name: "macneutron", dependencies: ["MacNeutronCore"]),
        .executableTarget(name: "MacNeutronApp", dependencies: ["MacNeutronCore"]),
        // Install MacNeutron (spec 2026-10-25-macneutron-installer-design): AppInstaller's front, in its own bundle.
        .executableTarget(name: "MacNeutronInstaller", dependencies: ["MacNeutronCore"]),
        .testTarget(name: "MacNeutronCoreTests", dependencies: ["MacNeutronCore", "MacNeutronApp"]),
    ]
)

import Foundation

public enum PreflightError: Error, Equatable, CustomStringConvertible {
    case unsupportedSystem
    case runtimeMissing
    case thirtyTwoBit
    case unsupportedMachine(UInt16)
    case unsupportedCPU(String, [String])

    public var description: String {
        switch self {
        case .unsupportedSystem: "MacNeutron needs macOS 27 or later on an Apple Silicon Mac."
        case .runtimeMissing: "MacNeutron's runtime is missing or damaged. Open MacNeutron to repair it."
        case .thirtyTwoBit: "This game is 32-bit. MacNeutron 0.1 runs 64-bit games only; 32-bit support is planned."
        case .unsupportedMachine(let machine):
            "This game is built for \(Self.name(of: machine)), which MacNeutron can't run."
        case .unsupportedCPU(let cpu, let missing):
            "This MacNeutron runtime is built for \(Self.name(ofCPU: cpu)), and this Mac's processor lacks "
                + missing.joined(separator: ", ") + ". Install the MacNeutron build for this Mac."
        }
    }

    /// "apple-m5" → "Apple M5"; another name as it is.
    static func name(ofCPU cpu: String) -> String {
        guard cpu.hasPrefix("apple-") else { return cpu }
        return "Apple " + cpu.dropFirst("apple-".count).uppercased()
    }

    static func name(of machine: UInt16) -> String {
        switch machine {
        case 0x1c4: "ARM (32-bit)"
        case 0x200: "Itanium"
        default: String(format: "machine type 0x%04x", UInt32(machine))
        }
    }
}

/// Launch-time checks. `wine.app`'s signature is verified at install; here we only confirm the pieces a launch
/// needs are present and its identity reads, which costs a few `stat`s and one code-directory read.
public struct Preflight: Sendable {
    public let systemSupported: @Sendable () -> Bool
    public let identity: @Sendable (ToolLayout) -> String?
    public let cpuHas: @Sendable (String) -> Bool

    public init(systemSupported: @escaping @Sendable () -> Bool = Preflight.isSupportedSystem,
                identity: @escaping @Sendable (ToolLayout) -> String? = { $0.identity },
                cpuHas: @escaping @Sendable (String) -> Bool = Preflight.cpuHas) {
        self.systemSupported = systemSupported
        self.identity = identity
        self.cpuHas = cpuHas
    }

    /// `hw.optional.arm.<feature>` reads 1.
    public static func cpuHas(_ feature: String) -> Bool {
        var value: Int32 = 0
        var size = MemoryLayout<Int32>.size
        return sysctlbyname("hw.optional.arm." + feature, &value, &size, nil, 0) == 0 && value == 1
    }

    /// macOS 27 or later on Apple Silicon.
    public static func isSupportedSystem() -> Bool {
        var arm64: Int32 = 0
        var size = MemoryLayout<Int32>.size
        return ProcessInfo.processInfo.isOperatingSystemAtLeast(OperatingSystemVersion(majorVersion: 27, minorVersion: 0,
                                                                                       patchVersion: 0))
            && sysctlbyname("hw.optional.arm64", &arm64, &size, nil, 0) == 0 && arm64 == 1
    }

    /// Returns the runtime's identity. A missing or unreadable runtime writes the damaged marker, which makes the
    /// app reinstall it. The target's machine type is checked only for the game (`waitforexitandrun`); a target that
    /// isn't PE (a script, a missing file) isn't checked.
    public func check(_ layout: ToolLayout, request: LaunchRequest) throws(PreflightError) -> String {
        guard systemSupported() else { throw .unsupportedSystem }
        let fm = FileManager.default
        guard fm.isExecutableFile(atPath: layout.wine.path(percentEncoded: false)),
              fm.isExecutableFile(atPath: layout.wineserver.path(percentEncoded: false)),
              let identity = identity(layout)
        else {
            try? Data().write(to: layout.runtimeDamagedMarker)
            throw .runtimeMissing
        }
        // A runtime built for a newer CPU (wine-arm64/build.sh's MACNEUTRON_CPU) would die on an illegal instruction.
        if let text = try? String(contentsOf: layout.cpuFile, encoding: .utf8) {
            let lines = text.split(whereSeparator: \.isNewline).map(String.init).filter { !$0.isEmpty }
            if let cpu = lines.first {
                let missing = lines.dropFirst().filter { !cpuHas($0) }
                if !missing.isEmpty { throw .unsupportedCPU(cpu, Array(missing)) }
            }
        }
        if request.verb == .waitforexitandrun, let machine = PEImage.machine(of: URL(filePath: request.target)) {
            switch machine {
            case PEImage.amd64, PEImage.arm64: break
            case PEImage.i386: throw .thirtyTwoBit
            default: throw .unsupportedMachine(machine)
            }
        }
        return identity
    }
}

/// Tells the user about launch-blocking failures; Steam itself shows nothing useful.
public protocol Notifier: Sendable {
    func post(title: String, message: String)
    /// A toast while something long runs (a game's prefix prepared), until the returned closure runs or `until`
    /// exists.
    func progress(title: String, message: String, until: URL?) -> @Sendable () -> Void
}

/// Posts through `osascript`, which needs no app bundle or entitlement. Toasts are `toast`'s (the launcher binary's
/// `macneutron toast`, Sources/macneutron/Toast.swift); without one, none.
public struct AppleScriptNotifier: Notifier {
    let toast: URL?

    public init(toast: URL? = nil) { self.toast = toast }

    public func progress(title: String, message: String, until: URL?) -> @Sendable () -> Void {
        guard let toast else { return {} }
        let process = Process(), input = Pipe()
        process.executableURL = toast
        process.arguments = ["toast", title, message] + (until.map { [$0.path(percentEncoded: false)] } ?? [])
        process.standardInput = input
        process.standardOutput = FileHandle.nullDevice
        process.standardError = FileHandle.nullDevice
        guard (try? process.run()) != nil else { return {} }
        // The toast goes when its stdin closes: here, or when this process ends.
        let end = input.fileHandleForWriting
        return { try? end.close() }
    }

    public func post(title: String, message: String) {
        let script = "display notification \(Self.quoted(message)) with title \(Self.quoted(title))"
        _ = try? SystemProcessRunner().run(URL(filePath: "/usr/bin/osascript"), ["-e", script],
                                           environment: [:], output: nil)
    }

    /// An AppleScript string literal: backslashes and quotes escaped.
    static func quoted(_ text: String) -> String {
        "\"" + text.replacingOccurrences(of: "\\", with: "\\\\").replacingOccurrences(of: "\"", with: "\\\"") + "\""
    }
}

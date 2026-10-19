import Foundation

/// Starts child processes. A protocol so tests can record calls instead of running Wine.
public protocol ProcessRunner: Sendable {
    /// Runs `executable` to completion and returns its exit status (128 + signal if killed).
    /// Output is appended to `output` when given, otherwise inherited from this process.
    func run(_ executable: URL, _ arguments: [String], environment: [String: String], output: URL?) throws -> Int32
    /// As `run`, telling `started` the child's process ID once it runs: the leader of a process group of its own
    /// (Process makes one), which Steam's Stop signals for a microVM launch (Launcher.stopVM).
    func run(_ executable: URL, _ arguments: [String], environment: [String: String], output: URL?,
             started: @Sendable (pid_t) -> Void) throws -> Int32
}

extension ProcessRunner {
    public func run(_ executable: URL, _ arguments: [String], environment: [String: String], output: URL?,
                    started: @Sendable (pid_t) -> Void) throws -> Int32 {
        try run(executable, arguments, environment: environment, output: output)
    }
}

public struct SystemProcessRunner: ProcessRunner {
    public init() {}

    public func run(_ executable: URL, _ arguments: [String], environment: [String: String],
                    output: URL?) throws -> Int32 {
        try run(executable, arguments, environment: environment, output: output, started: { _ in })
    }

    public func run(_ executable: URL, _ arguments: [String], environment: [String: String], output: URL?,
                    started: @Sendable (pid_t) -> Void) throws -> Int32 {
        let process = Process()
        process.executableURL = executable
        process.arguments = arguments
        process.environment = environment
        var handle: FileHandle?
        if let output {
            let path = output.path(percentEncoded: false)
            if !FileManager.default.fileExists(atPath: path) {
                FileManager.default.createFile(atPath: path, contents: nil)
            }
            let file = try FileHandle(forWritingTo: output)
            try file.seekToEnd()
            process.standardOutput = file
            process.standardError = file
            handle = file
        }
        defer { try? handle?.close() }
        try process.run()
        started(process.processIdentifier)
        process.waitUntilExit()
        return process.terminationReason == .uncaughtSignal
            ? 128 + process.terminationStatus : process.terminationStatus
    }
}

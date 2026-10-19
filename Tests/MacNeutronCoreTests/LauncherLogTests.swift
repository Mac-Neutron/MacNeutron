import Foundation
import Testing
@testable import MacNeutronCore

@Test func appendsTimestampedLines() throws {
    let log = LauncherLog(directory: try makeTempDir().appending(path: "Logs"))
    log.append("verb=run exit=0")
    log.append("verb=run exit=1")
    let lines = try String(contentsOf: log.launcherLog, encoding: .utf8).split(separator: "\n")
    #expect(lines.count == 2)
    #expect(lines[1].hasSuffix(" verb=run exit=1"))
    #expect(lines[0].hasPrefix("20"))
}

@Test func rotatesPastOneMegabyte() throws {
    let log = LauncherLog(directory: try makeTempDir())
    try Data(count: LauncherLog.rotateBytes + 1).write(to: log.launcherLog)
    log.append("fresh")
    let rotated = log.directory.appending(path: "launcher.log.1")
    #expect(FileManager.default.fileExists(atPath: rotated.path(percentEncoded: false)))
    #expect(try String(contentsOf: log.launcherLog, encoding: .utf8).hasSuffix(" fresh\n"))
}

@Test func gameLogIsPerApp() throws {
    let log = LauncherLog(directory: try makeTempDir().appending(path: "Logs"))
    #expect(log.gameLog(appID: "42").lastPathComponent == "steam-42.log")
    #expect(FileManager.default.fileExists(atPath: log.directory.path(percentEncoded: false)))
}

@Test func gameLogRotatesPastFiftyMegabytesKeepingOne() throws {
    let log = LauncherLog(directory: try makeTempDir())
    let game = log.gameLog(appID: "42"), old = log.directory.appending(path: "steam-42.log.1")
    func size(_ url: URL) -> Int? {
        ((try? FileManager.default.attributesOfItem(atPath: url.path(percentEncoded: false)))?[.size] as? NSNumber)?.intValue
    }
    try write("older", to: old)
    try write("", to: game)
    let handle = try FileHandle(forWritingTo: game)
    try handle.truncate(atOffset: UInt64(LauncherLog.gameLogRotateBytes))  // at the limit (sparse): kept
    #expect(log.gameLog(appID: "42") == game)
    #expect(size(game) == LauncherLog.gameLogRotateBytes && size(old) == 5)
    try handle.truncate(atOffset: UInt64(LauncherLog.gameLogRotateBytes + 1))  // past it: rotated, the older one gone
    try handle.close()
    _ = log.gameLog(appID: "42")
    #expect(size(game) == nil)
    #expect(size(old) == LauncherLog.gameLogRotateBytes + 1)
}

@Test func concurrentAppendsKeepEveryLine() throws {
    // Launches overlap (Steam's setup run and the game, check.sh's lanes): no append may overwrite another.
    let log = LauncherLog(directory: try makeTempDir())
    DispatchQueue.concurrentPerform(iterations: 8) { thread in
        for i in 0..<50 { log.append("thread \(thread) line \(i)") }
    }
    let lines = try String(contentsOf: log.launcherLog, encoding: .utf8).split(separator: "\n")
    #expect(lines.count == 400)
}

@Test func aQuietLogWritesOnlySummaries() throws {
    // A performance build (X.Y.Zp) launched without MACNEUTRON_LOG: one line a launch, its summary.
    let log = LauncherLog(directory: try makeTempDir().appending(path: "Logs"))
    log.setQuiet(true)
    log.append("note: Steam client folder /x")
    #expect(!FileManager.default.fileExists(atPath: log.launcherLog.path(percentEncoded: false)))
    log.summary("verb=run appid=1 exit=0")
    let lines = try String(contentsOf: log.launcherLog, encoding: .utf8).split(separator: "\n")
    #expect(lines.count == 1)
    #expect(lines[0].hasSuffix(" verb=run appid=1 exit=0"))
}

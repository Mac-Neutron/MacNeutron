import Foundation
import Testing
@testable import MacNeutronCore

@Test func aCrashMarkerMapsItsGuestReportsBackToTheMac() throws {
    let share = try makeTempDir().appending(path: "share", directoryHint: .isDirectory)
    let prefix = try makeTempDir().appending(path: "compatdata/42/pfx", directoryHint: .isDirectory)
    let library = "/Users/someone/Library/Application Support/Steam/steamapps/common"
    try write("""
        when=2026-10-25T21:00:00Z
        reporter=CrashReportClie
        pid=1234
        command=C:\\Games\\Game\\Binaries\\CrashReportClient.exe -app="Game"
        report=/home/mn/pfx/drive_c/users/steamuser/AppData/Local/Game/Saved/Crashes/ABC
        report=/mnt/games/Some Game/Saved/Crashes/DEF
        report=/usr/share/something/else
        """, to: share.appending(path: VMCrash.marker))
    try write("ps\n", to: share.appending(path: VMCrash.details).appending(path: "ps.txt"))

    let crash = try #require(VMCrash.read(share: share, prefix: prefix, library: library))
    #expect(crash.when == "2026-10-25T21:00:00Z")
    #expect(crash.reporter == "CrashReportClie")
    #expect(crash.command.contains("-app=\"Game\""))
    #expect(crash.reports == [
        prefix.path(percentEncoded: false) + "/drive_c/users/steamuser/AppData/Local/Game/Saved/Crashes/ABC",
        library + "/Some Game/Saved/Crashes/DEF",
    ])
    #expect(crash.detailsURL == share.appending(path: VMCrash.details))
    #expect(crash.raw.contains("reporter=CrashReportClie"))
    #expect(crash.summary.contains("reports="))
}

@Test func aMarkerWithoutReportsStillSaysWhatRan() throws {
    let share = try makeTempDir()
    try write("when=2026-10-25T21:00:00Z\nreporter=BugSplat\n", to: share.appending(path: VMCrash.marker))
    let crash = try #require(VMCrash.read(share: share, prefix: try makeTempDir(), library: "/tmp/lib"))
    #expect(crash.reporter == "BugSplat")
    #expect(crash.reports.isEmpty)
    #expect(crash.command.isEmpty)
}

@Test func noMarkerIsNoCrash() throws {
    #expect(VMCrash.read(share: try makeTempDir(), prefix: try makeTempDir(), library: "/tmp/lib") == nil)
}

@Test func aGuestPathOutsideTheSharesIsNotOffered() throws {
    let prefix = try makeTempDir()
    #expect(VMCrash.macPath("/mnt/share/traces/x", prefix: prefix, library: "/tmp/lib") == nil)
    #expect(VMCrash.macPath("/home/mn/pfx/drive_c/windows/x", prefix: prefix, library: "/tmp/lib") == nil)
}

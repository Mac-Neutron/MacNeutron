import Foundation
import Testing
@testable import MacNeutronCore

// Steam's install scripts for a game in the VM (spec 2026-10-24-macneutron-install-scripts-design): the files are
// 3DMark Demo's (231350) and the Steamworks Common Redistributables' (228980), as Steam installed them.

private let sharedManifest = #"""
"AppState"
{
    "appid"        "228980"
    "installdir"        "Steamworks Shared"
    "InstallScripts"
    {
        "228984"        "_CommonRedist\\vcredist\\2012\\installscript.vdf"
        "228988"        "_CommonRedist\\vcredist\\2019\\installscript.vdf"
        "228990"        "_CommonRedist\\DirectX\\Jun2010\\installscript.vdf"
        "229004"        "_CommonRedist\\DotNet\\4.5.2\\installscript.vdf"
    }
}
"""#

private let vc2012 = #"""
"InstallScript"
{
    "Run Process"
    {
        "x86 Update 4"
        {
            "HasRunKey"        "HKEY_LOCAL_MACHINE\\Software\\Valve\\Steam\\Apps\\CommonRedist\\vcredist\\2012"
            "process 1"        "%INSTALLDIR%\\_CommonRedist\\vcredist\\2012\\vcredist_x86.exe"
            "command 1"        "/quiet /norestart"
            "NoCleanUp"        "1"
        }
        "x64 Update 4"
        {
            "HasRunKey"        "HKEY_LOCAL_MACHINE\\Software\\Valve\\Steam\\Apps\\CommonRedist\\vcredist\\2012"
            "process 1"        "%INSTALLDIR%\\_CommonRedist\\vcredist\\2012\\vcredist_x64.exe"
            "command 1"        "/quiet /norestart"
            "NoCleanUp"        "1"
            "Requirement_OS"
            {
                "Is64BitWindows"        "1"
            }
        }
    }
}
"kvsignatures"
{
    "InstallScript"        "579ba5cacfb9d7f10af81f98c13407ca"
}
"""#

private let vc2019 = #"""
"InstallScript"
{
    "Run Process"
    {
        "x64 14.28.29334.0"
        {
            "HasRunKey"        "HKEY_LOCAL_MACHINE\\Software\\Valve\\Steam\\Apps\\CommonRedist\\vcredist\\2019"
            "process 1"        "%INSTALLDIR%\\_CommonRedist\\vcredist\\2019\\Microsoft Visual C++ 2019 x64.cmd"
        }
    }
}
"""#

private let directX = #"""
"installscript"
{
    "Run Process"
    {
        "dxsetup"
        {
            "hasrunkey"        "HKEY_LOCAL_MACHINE\\Software\\Valve\\Steam\\Apps\\CommonRedist\\DirectX\\Jun2010"
            "process 1"        "%INSTALLDIR%\\_CommonRedist\\DirectX\\Jun2010\\DXSETUP.exe"
            "command 1"        "/silent"
            "nocleanup"        "1"
        }
    }
}
"""#

private let dotNet452 = #"""
"InstallScript"
{
    "Run Process"
    {
        "4.5.2"
        {
            "hasrunkey"        "HKEY_LOCAL_MACHINE\\Software\\Valve\\Steam\\Apps\\CommonRedist\\.NET\\4.5.2"
            "process 1"        "%INSTALLDIR%\\_CommonRedist\\DotNet\\4.5.2\\noop.cmd"
            "nocleanup"        "1"
            "requirement_os"
            {
                "OSType"        "Windows XP"
            }
        }
        "4.5.2"
        {
            "hasrunkey"        "HKEY_LOCAL_MACHINE\\Software\\Valve\\Steam\\Apps\\CommonRedist\\.NET\\4.5.2"
            "process 1"        "%INSTALLDIR%\\_CommonRedist\\DotNet\\4.5.2\\Microsoft .NET Framework 4.5.2.cmd"
            "nocleanup"        "1"
        }
    }
}
"""#

private let threeDMarkRegistry = #"""
"InstallScript"
{
    "Registry"
    {
        "HKEY_LOCAL_MACHINE\\SOFTWARE\\Futuremark\\3DMark"
        {
            "string"
            {
                "Install_Path"        "%INSTALLDIR%"
            }
        }
    }
}
"kvsignatures"
{
    "InstallScript"        "579ba5cacfb9d7f10af81f98c13407ca"
}
"""#

private let systemInfo = #"""
"InstallScript"
{
    "Run Process"
    {
        "FmSysInfo"
        {
            "HasRunKey"        "HKEY_CURRENT_USER\\SOFTWARE\\Valve\\Steam\\Apps\\231350"
            "Process 1"        "%INSTALLDIR%\\SystemInfoSetupAssistant.exe"
            "Command 1"        "Futuremark_SystemInfo_5_93_1517.msi"
            "MinimumHasRunValue"        "1517"
        }
    }
}
"""#

/// A game's appmanifest: its own scripts (depot → path) and the shared depots it uses (depot → 228980).
private func gameManifest(_ appID: String, installdir: String, scripts: [(String, String)],
                          shared: [String] = []) -> String {
    "\"AppState\"\n{\n\t\"appid\"\t\t\"\(appID)\"\n\t\"installdir\"\t\t\"\(installdir)\"\n"
        + "\t\"InstallScripts\"\n\t{\n" + scripts.map { "\t\t\"\($0.0)\"\t\t\"\($0.1)\"\n" }.joined() + "\t}\n"
        + "\t\"SharedDepots\"\n\t{\n" + shared.map { "\t\t\"\($0)\"\t\t\"228980\"\n" }.joined() + "\t}\n}\n"
}

/// 3DMark Demo's library: its two scripts, and the four shared ones (VC 2019's belongs to another game).
private func threeDMarkLibrary(shared: Bool = true) throws -> URL {
    let steamapps = try makeTempDir().appending(path: "steamapps", directoryHint: .isDirectory)
    try write(gameManifest("231350", installdir: "3DMark Demo",
                           scripts: [("223851", "installscript.vdf"), ("231351", "installscript_si.vdf")],
                           shared: ["228984", "228990", "229004"]),
              to: steamapps.appending(path: "appmanifest_231350.acf"))
    try write(threeDMarkRegistry, to: steamapps.appending(path: "common/3DMark Demo/installscript.vdf"))
    try write(systemInfo, to: steamapps.appending(path: "common/3DMark Demo/installscript_si.vdf"))
    if shared {
        try write(sharedManifest, to: steamapps.appending(path: "appmanifest_228980.acf"))
        let redist = steamapps.appending(path: "common/Steamworks Shared/_CommonRedist")
        try write(vc2012, to: redist.appending(path: "vcredist/2012/installscript.vdf"))
        try write(vc2019, to: redist.appending(path: "vcredist/2019/installscript.vdf"))
        try write(directX, to: redist.appending(path: "DirectX/Jun2010/installscript.vdf"))
        try write(dotNet452, to: redist.appending(path: "DotNet/4.5.2/installscript.vdf"))
    }
    return steamapps
}

/// A game "Game" (app 42) whose only script is `script`.
private func plan(script: String) throws -> InstallScripts {
    let steamapps = try makeTempDir().appending(path: "steamapps", directoryHint: .isDirectory)
    try write(gameManifest("42", installdir: "Game", scripts: [("43", "installscript.vdf")]),
              to: steamapps.appending(path: "appmanifest_42.acf"))
    try write(script, to: steamapps.appending(path: "common/Game/installscript.vdf"))
    return InstallScripts.plan(steamapps: steamapps, appID: "42")
}

/// One Run Process entry of app 42's script.
private func runScript(_ entries: String) -> String {
    "\"InstallScript\"\n{\n\"Run Process\"\n{\n\(entries)\n}\n}\n"
}

private let redist = #"Z:\mnt\games\Steamworks Shared\_CommonRedist"#
private let commonRedistKey = #"HKEY_LOCAL_MACHINE\Software\Wow6432Node\Valve\Steam\Apps\CommonRedist"#

@Test func threeDMarksScriptsBecomeItsPlan() throws {
    let result = InstallScripts.plan(steamapps: try threeDMarkLibrary(), appID: "231350")
    let vc: String = "/mnt/games/Steamworks Shared/_CommonRedist/vcredist/2012"
    let vcKey: String = commonRedistKey + #"\vcredist\2012"#
    var expected: [InstallScripts.Run] = []
    expected.append(.init(key: vcKey, name: "x86 Update 4", minimum: 1, folder: vc,
                          process: redist + #"\vcredist\2012\vcredist_x86.exe"#, arguments: ["/quiet", "/norestart"]))
    expected.append(.init(key: vcKey, name: "x64 Update 4", minimum: 1, folder: vc,
                          process: redist + #"\vcredist\2012\vcredist_x64.exe"#, arguments: ["/quiet", "/norestart"]))
    expected.append(.init(key: commonRedistKey + #"\DirectX\Jun2010"#, name: "dxsetup", minimum: 1,
                          folder: "/mnt/games/Steamworks Shared/_CommonRedist/DirectX/Jun2010",
                          process: redist + #"\DirectX\Jun2010\DXSETUP.exe"#, arguments: ["/silent"]))
    expected.append(.init(key: commonRedistKey + #"\.NET\4.5.2"#, name: "4.5.2", minimum: 1,
                          folder: "/mnt/games/Steamworks Shared/_CommonRedist/DotNet/4.5.2",
                          process: redist + #"\DotNet\4.5.2\Microsoft .NET Framework 4.5.2.cmd"#, arguments: []))
    expected.append(.init(key: #"HKEY_CURRENT_USER\SOFTWARE\Valve\Steam\Apps\231350"#, name: "FmSysInfo",
                          minimum: 1517, folder: "/mnt/games/3DMark Demo",
                          process: #"Z:\mnt\games\3DMark Demo\SystemInfoSetupAssistant.exe"#,
                          arguments: ["Futuremark_SystemInfo_5_93_1517.msi"]))
    #expect(result.runs == expected)
    #expect(result.registry == #"""
        REGEDIT4

        [HKEY_LOCAL_MACHINE\Software\Wow6432Node\Futuremark\3DMark]
        "Install_Path"="Z:\\mnt\\games\\3DMark Demo"

        """#)
    #expect(result.notes == [])
}

@Test func keysMatchInAnyCase() throws {
    let entry = #"""
        "x64"
        {
            "%@"        "HKEY_LOCAL_MACHINE\\Software\\T"
            "%@"        "%INSTALLDIR%\\setup.exe"
            "%@"        "/q"
            "%@"
            {
                "Is64BitWindows"        "1"
            }
        }
        """#
    func script(_ keys: [String]) -> String {
        var body = entry
        for key in keys.dropFirst(2) {
            if let slot = body.range(of: "%@") { body.replaceSubrange(slot, with: key) }
        }
        return "\"\(keys[0])\"\n{\n\"\(keys[1])\"\n{\n\(body)\n}\n}\n"
    }
    let lower = try plan(script: script(["installscript", "run process", "hasrunkey", "process 1", "command 1", "requirement_os"]))
    let upper = try plan(script: script(["InstallScript", "Run Process", "HasRunKey", "Process 1", "Command 1", "Requirement_OS"]))
    #expect(lower.runs.count == 1)
    #expect(lower.runs == upper.runs)
    #expect(lower.runs.first?.process == #"Z:\mnt\games\Game\setup.exe"#)
}

@Test func anEntryForAnotherWindowsIsDropped() throws {
    func entry(_ name: String, _ requirement: String) -> String {
        "\"\(name)\"\n{\n\"HasRunKey\" \"HKEY_LOCAL_MACHINE\\\\Software\\\\T\"\n"
            + "\"process 1\" \"%INSTALLDIR%\\\\\(name).exe\"\n\"Requirement_OS\"\n{\n\(requirement)\n}\n}\n"
    }
    let result = try plan(script: runScript(entry("bits32", #""Is64BitWindows" "0""#)
                                            + entry("xp", #""OSType" "Windows XP""#)
                                            + entry("server", #""IsServer" "1""#)
                                            + entry("ten", #""OSType" "windows 10""#)))
    #expect(result.runs.map(\.name) == ["ten"])
    #expect(result.notes.count == 1)
    #expect(result.notes.first?.contains("IsServer") == true)
}

@Test func theFirstEntryPerHasRunValueWins() throws {
    let entry = { (exe: String) in
        "\"4.5.2\"\n{\n\"hasrunkey\" \"HKEY_LOCAL_MACHINE\\\\Software\\\\T\"\n\"process 1\" \"%INSTALLDIR%\\\\\(exe)\"\n}\n"
    }
    let result = try plan(script: runScript(entry("first.exe") + entry("second.exe")))
    #expect(result.runs.map(\.process) == [#"Z:\mnt\games\Game\first.exe"#])
}

@Test func whatIsntSupportedIsSkippedWithANote() throws {
    let script = #"""
        "InstallScript"
        {
            "Firewall"
            {
                "Game"        "%INSTALLDIR%\\Game.exe"
            }
            "Run Process On Uninstall"
            {
                "x"
                {
                    "process 1"        "%INSTALLDIR%\\uninstall.exe"
                }
            }
            "Run Process"
            {
                "nokey"
                {
                    "process 1"        "%INSTALLDIR%\\a.exe"
                }
                "two"
                {
                    "HasRunKey"        "HKEY_LOCAL_MACHINE\\Software\\T"
                    "process 1"        "%INSTALLDIR%\\a.exe"
                    "process 2"        "%INSTALLDIR%\\b.exe"
                }
                "drive"
                {
                    "HasRunKey"        "HKEY_LOCAL_MACHINE\\Software\\T"
                    "process 1"        "%ROOTDRIVE%\\a.exe"
                }
                "powershell"
                {
                    "HasRunKey"        "HKEY_LOCAL_MACHINE\\Software\\T"
                    "process 1"        "%INSTALLDIR%\\setup.ps1"
                }
            }
            "Registry"
            {
                "HKEY_LOCAL_MACHINE\\Software\\Game"
                {
                    "string"
                    {
                        "english"
                        {
                            "Language"        "en"
                        }
                    }
                }
            }
        }
        """#
    let result = try plan(script: script)
    #expect(result.runs == [])
    #expect(result.registry == "")
    for word in ["Firewall", "Run Process On Uninstall", "HasRunKey", "process 2", "%ROOTDRIVE%", ".ps1", "language"] {
        #expect(result.notes.contains { $0.contains(word) }, "no note for \(word): \(result.notes)")
    }
}

@Test func missingManifestsAndScriptsAreSkipped() throws {
    // A launch from outside Steam: no manifest, nothing to say.
    #expect(InstallScripts.plan(steamapps: try makeTempDir(), appID: "231350") == InstallScripts())
    // The shared app isn't in this library: the game's own scripts still run.
    let alone = InstallScripts.plan(steamapps: try threeDMarkLibrary(shared: false), appID: "231350")
    #expect(alone.runs.map(\.name) == ["FmSysInfo"])
    #expect(alone.registry.contains("Install_Path"))
    #expect(alone.notes.count == 3)
    #expect(alone.notes.allSatisfy { $0.contains("228980") })
    // A listed script that isn't there.
    let steamapps = try threeDMarkLibrary()
    try FileManager.default.removeItem(at: steamapps.appending(path: "common/3DMark Demo/installscript_si.vdf"))
    let missing = InstallScripts.plan(steamapps: steamapps, appID: "231350")
    #expect(!missing.runs.contains { $0.name == "FmSysInfo" })
    #expect(missing.notes.count == 1)
    #expect(missing.notes.first?.contains("installscript_si.vdf") == true)
}

@Test func aBOMAndCRLFScriptIsRead() throws {
    let steamapps = try threeDMarkLibrary()
    try write("\u{FEFF}" + systemInfo.replacingOccurrences(of: "\n", with: "\r\n"),
              to: steamapps.appending(path: "common/3DMark Demo/installscript_si.vdf"))
    let result = InstallScripts.plan(steamapps: steamapps, appID: "231350")
    #expect(result.runs.last?.name == "FmSysInfo")
    #expect(result.notes == [])
}

@Test func registryTextEscapesAsRegFilesDo() throws {
    let result = try plan(script: #"""
        "InstallScript"
        {
            "Registry"
            {
                "HKEY_CURRENT_USER\\Software\\Game"
                {
                    "string"
                    {
                        "v"        "a\"b\\c"
                        "(Default)"        "x"
                        "broken"        "two\#nlines"
                    }
                    "dword"
                    {
                        "d"        "1517"
                        "bad"        "x12"
                    }
                }
            }
        }
        """#)
    #expect(result.registry == #"""
        REGEDIT4

        [HKEY_CURRENT_USER\Software\Game]
        "v"="a\"b\\c"
        @="x"
        "d"=dword:000005ed

        """#)
    #expect(result.notes.count == 2)
    #expect(result.notes.contains { $0.contains("broken") })
    #expect(result.notes.contains { $0.contains("bad") })
}

@Test func windowsCommandLinesSplitAsWindowsDoes() {
    #expect(InstallScripts.arguments("/quiet /norestart") == ["/quiet", "/norestart"])
    #expect(InstallScripts.arguments(#"install "a b" c"#) == ["install", "a b", "c"])
    #expect(InstallScripts.arguments(#"a\"b"#) == [#"a"b"#])
    #expect(InstallScripts.arguments(#"a\\"b c""#) == [#"a\b c"#])
    #expect(InstallScripts.arguments(#""""#) == [""])
    #expect(InstallScripts.arguments("  ") == [])
}

@Test func hklmSoftwareIsSteamsThirtyTwoBitView() {
    #expect(InstallScripts.registryKey(#"HKEY_LOCAL_MACHINE\SOFTWARE\Futuremark"#)
            == #"HKEY_LOCAL_MACHINE\Software\Wow6432Node\Futuremark"#)
    #expect(InstallScripts.registryKey(#"HKLM\Software\X"#) == #"HKEY_LOCAL_MACHINE\Software\Wow6432Node\X"#)
    #expect(InstallScripts.registryKey(#"HKEY_LOCAL_MACHINE\Software\Wow6432Node\X"#)
            == #"HKEY_LOCAL_MACHINE\Software\Wow6432Node\X"#)
    #expect(InstallScripts.registryKey(#"HKEY_LOCAL_MACHINE\SYSTEM\X"#) == #"HKEY_LOCAL_MACHINE\SYSTEM\X"#)
    #expect(InstallScripts.registryKey(#"HKCU\Software\X"#) == #"HKEY_CURRENT_USER\Software\X"#)
    #expect(InstallScripts.registryKey(#"HKEY_CLASSES_ROOT\X"#) == nil)
}

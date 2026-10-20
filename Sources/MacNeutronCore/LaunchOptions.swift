import Foundation

/// A game's launch options as one line: the Games window's field, or Steam's own Launch Options.
public enum LaunchOptions {
    /// The leading `NAME=value` words are variables (a later one of a name wins); every word from the first other one
    /// on is a game argument. A first `/usr/bin/env` and every `%command%` are dropped, so a line pasted from Steam or
    /// the README reads the same.
    // ponytail: no quoting; add it when someone needs a value with a space.
    public static func parse(_ line: String) -> (environment: [String: String], arguments: [String]) {
        var words = line.split(whereSeparator: \.isWhitespace)
        if words.first == "/usr/bin/env" { words.removeFirst() }
        words.removeAll { $0 == "%command%" }
        var environment: [String: String] = [:]
        var index = words.startIndex
        while index < words.endIndex, isAssignment(words[index]) {
            let word = words[index]
            let equals = word.firstIndex(of: "=")!
            environment[String(word[..<equals])] = String(word[word.index(after: equals)...])
            index += 1
        }
        return (environment, words[index...].map(String.init))
    }

    /// Each app's non-empty Launch Options in Steam's `localconfig.vdf`, read only (Steam saves the file when it quits,
    /// so an edit made in Steam can show late). An unreadable file gives none.
    public static func steamLines(localConfig: URL) -> [UInt32: String] {
        guard let text = try? String(contentsOf: localConfig, encoding: .utf8),
              let apps = (try? KeyValues.parse(text))?.node(at: ["UserLocalConfigStore", "Software", "Valve", "Steam", "apps"])
        else { return [:] }
        var lines: [UInt32: String] = [:]
        for app in apps.children {
            if let id = UInt32(KeyValues.unescape(app.key)), let line = app.children.node(at: ["LaunchOptions"])?.stringValue,
               !line.isEmpty {
                lines[id] = line
            }
        }
        return lines
    }

    /// What's wrong with a game's Steam launch line, for the Games window: macOS Steam runs it without a shell, so a
    /// first `NAME=value` word is run as the program ("OS Error 260"); and its variables win over the app's.
    public static func problems(steamLine: String, appEnvironment: [String: String]) -> [String] {
        var problems: [String] = []
        if let first = steamLine.split(whereSeparator: \.isWhitespace).first, isAssignment(first) {
            problems.append("Steam can't start the game with this line: begin it with /usr/bin/env")
        }
        let overridden = Set(parse(steamLine).environment.keys).intersection(appEnvironment.keys).sorted()
        if !overridden.isEmpty {
            problems.append("Steam's launch options override: \(overridden.joined(separator: ", "))")
        }
        return problems
    }

    /// `NAME=value`, `NAME` a shell name.
    static func isAssignment(_ word: Substring) -> Bool {
        guard let equals = word.firstIndex(of: "="), let first = word.first, first == "_" || first.isASCIILetter else {
            return false
        }
        return word[..<equals].allSatisfy { $0 == "_" || $0.isASCIILetter || ($0.isASCII && $0.isNumber) }
    }
}

private extension Character {
    var isASCIILetter: Bool { isASCII && isLetter }
}

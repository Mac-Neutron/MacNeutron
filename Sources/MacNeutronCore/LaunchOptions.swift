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

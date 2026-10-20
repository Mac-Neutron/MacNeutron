import Foundation
import Security

/// A signed bundle's identity: its main executable's CDHash, as `codesign -dvvv` prints it (`CDHash=`).
public enum CodeIdentity {
    /// 40 lowercase hex digits (`kSecCodeInfoUnique`), or nil when `bundle` is missing or unsigned.
    /// Reads the code directory only; it doesn't validate the bundle's resources.
    public static func of(_ bundle: URL) -> String? {
        guard let unique = signingInformation(bundle)?[kSecCodeInfoUnique as String] as? Data else { return nil }
        return unique.map { String(format: "%02x", $0) }.joined()
    }

    /// The Developer ID team that signed `bundle` (`kSecCodeInfoTeamIdentifier`); nil when it is ad hoc signed,
    /// unsigned or missing. Like `of`, it doesn't validate the signature.
    public static func team(of bundle: URL) -> String? {
        signingInformation(bundle)?[kSecCodeInfoTeamIdentifier as String] as? String
    }

    private static func signingInformation(_ bundle: URL) -> [String: Any]? {
        var code: SecStaticCode?
        var info: CFDictionary?
        guard SecStaticCodeCreateWithPath(bundle as CFURL, [], &code) == errSecSuccess, let code,
              SecCodeCopySigningInformation(code, SecCSFlags(rawValue: kSecCSSigningInformation), &info) == errSecSuccess
        else { return nil }
        return info as? [String: Any]
    }
}

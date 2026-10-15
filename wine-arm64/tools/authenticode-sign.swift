// Authenticode-signs Windows PE files with a code-signing identity from the keychain (the Developer ID MacNeutron's
// bundle is signed with), in place: authenticode-sign <identity> <file>...
//
// Why: NVIDIA's NGX SDK, linked into every DLSS game, loads nvngx.dll only when WinVerifyTrust accepts its Authenticode
// signature (any trusted signer: Apple's own nvngx-on-metalfx.dll is signed by Apple). DXMT's nvngx.dll was unsigned,
// so games never saw DLSS. Developer ID Application certificates carry the Code Signing EKU and chain to Apple's root,
// which Wine trusts through macOS's root store.
//
// What it writes: PKCS#7 SignedData over an SpcIndirectDataContent holding the file's SHA-256 Authenticode digest, with
// the signer's chain, in a WIN_CERTIFICATE appended to the file (security directory and checksum updated). A file that
// is already signed is refused rather than re-signed. No timestamp: the signature verifies while the certificate is
// valid.
import CryptoKit
import Foundation
import Security

func fail(_ message: String) -> Never {
  FileHandle.standardError.write(("authenticode-sign: " + message + "\n").data(using: .utf8)!)
  exit(1)
}

// DER.
func derLength(_ n: Int) -> [UInt8] {
  if n < 0x80 { return [UInt8(n)] }
  var bytes: [UInt8] = []
  var v = n
  while v > 0 { bytes.insert(UInt8(v & 0xff), at: 0); v >>= 8 }
  return [0x80 | UInt8(bytes.count)] + bytes
}
func tlv(_ tag: UInt8, _ content: [UInt8]) -> [UInt8] { [tag] + derLength(content.count) + content }
func seq(_ parts: [UInt8]...) -> [UInt8] { tlv(0x30, parts.flatMap { $0 }) }
func setOf(_ items: [[UInt8]]) -> [UInt8] {
  // DER sorts a SET OF by its elements' encodings.
  tlv(0x31, items.sorted { $0.lexicographicallyPrecedes($1) }.flatMap { $0 })
}
func oid(_ dotted: String) -> [UInt8] {
  let arcs = dotted.split(separator: ".").map { UInt64($0)! }
  var out: [UInt8] = [UInt8(arcs[0] * 40 + arcs[1])]
  for arc in arcs.dropFirst(2) {
    var chunk: [UInt8] = [UInt8(arc & 0x7f)]
    var v = arc >> 7
    while v > 0 { chunk.insert(UInt8(v & 0x7f) | 0x80, at: 0); v >>= 7 }
    out += chunk
  }
  return tlv(0x06, out)
}
func octets(_ bytes: [UInt8]) -> [UInt8] { tlv(0x04, bytes) }
let null: [UInt8] = [0x05, 0x00]

// One DER element at `at`: its tag, where its content starts and its total length.
func element(_ d: [UInt8], _ at: Int) -> (tag: UInt8, content: Int, end: Int) {
  let tag = d[at]
  var len = Int(d[at + 1])
  var p = at + 2
  if len & 0x80 != 0 {
    let n = len & 0x7f
    len = 0
    for i in 0..<n { len = len << 8 | Int(d[p + i]) }
    p += n
  }
  return (tag, p, p + len)
}

// The certificate's issuer and serial number, as its TBSCertificate encodes them.
func issuerAndSerial(_ cert: [UInt8]) -> [UInt8] {
  let certificate = element(cert, 0)
  let tbs = element(cert, certificate.content)
  var p = tbs.content
  if cert[p] == 0xa0 { p = element(cert, p).end }  // [0] version
  let serial = element(cert, p)
  let sigAlg = element(cert, serial.end)
  let issuer = element(cert, sigAlg.end)
  return seq(Array(cert[sigAlg.end..<issuer.end]), Array(cert[p..<serial.end]))
}

// The signing identity and its chain (leaf first, root left out).
func identity(named name: String) -> (SecIdentity, [[UInt8]]) {
  let query: [String: Any] = [kSecClass as String: kSecClassIdentity, kSecMatchLimit as String: kSecMatchLimitAll,
                              kSecReturnRef as String: true]
  var result: CFTypeRef?
  guard SecItemCopyMatching(query as CFDictionary, &result) == errSecSuccess, let all = result as? [SecIdentity] else {
    fail("no signing identities in the keychain")
  }
  for id in all {
    var cert: SecCertificate?
    guard SecIdentityCopyCertificate(id, &cert) == errSecSuccess, let cert,
      (SecCertificateCopySubjectSummary(cert) as String?) == name
    else { continue }
    var trust: SecTrust?
    SecTrustCreateWithCertificates(cert, SecPolicyCreateBasicX509(), &trust)
    guard let trust else { fail("can't build \(name)'s chain") }
    _ = SecTrustEvaluateWithError(trust, nil)
    let chain = (SecTrustCopyCertificateChain(trust) as? [SecCertificate]) ?? [cert]
    let ders = chain.map { [UInt8](SecCertificateCopyData($0) as Data) }
    return (id, ders.count > 1 ? Array(ders.dropLast()) : ders)  // the root stays in the verifier's store
  }
  fail("no identity named \(name) in the keychain")
}

func u16(_ d: [UInt8], _ at: Int) -> Int { Int(d[at]) | Int(d[at + 1]) << 8 }
func u32(_ d: [UInt8], _ at: Int) -> Int { u16(d, at) | u16(d, at + 2) << 16 }
func put32(_ d: inout [UInt8], _ at: Int, _ v: Int) {
  for i in 0..<4 { d[at + i] = UInt8((v >> (8 * i)) & 0xff) }
}

// The PE checksum: 16-bit one's-complement sum of the file with its checksum field as 0, plus the file length.
func peChecksum(_ d: [UInt8], skipping at: Int) -> Int {
  var sum = 0
  var i = 0
  while i < d.count {
    var word = Int(d[i])
    if i + 1 < d.count { word |= Int(d[i + 1]) << 8 }
    if i != at && i != at + 2 { sum += word; sum = (sum & 0xffff) + (sum >> 16) }
    i += 2
  }
  sum = (sum & 0xffff) + (sum >> 16)
  return (sum & 0xffff) + d.count
}

func sign(_ path: String, _ id: SecIdentity, _ chain: [[UInt8]]) {
  guard var d = FileManager.default.contents(atPath: path).map({ [UInt8]($0) }), d.count > 0x40,
    d[0] == 0x4d, d[1] == 0x5a
  else { fail("\(path) is not a PE file") }
  let pe = u32(d, 0x3c)
  guard d.count > pe + 24, d[pe] == 0x50, d[pe + 1] == 0x45 else { fail("\(path) is not a PE file") }
  let opt = pe + 24
  let magic = u16(d, opt)
  guard magic == 0x10b || magic == 0x20b else { fail("\(path): unknown optional header") }
  let checksumAt = opt + 64
  let securityAt = opt + (magic == 0x20b ? 112 : 96) + 4 * 8
  guard u32(d, securityAt) == 0, u32(d, securityAt + 4) == 0 else { fail("\(path) is already signed") }

  // The certificate table starts 8-aligned, after the padding, which the digest covers.
  while d.count % 8 != 0 { d.append(0) }
  var hasher = SHA256()
  hasher.update(data: d[0..<checksumAt])
  hasher.update(data: d[(checksumAt + 4)..<securityAt])
  hasher.update(data: d[(securityAt + 8)..<d.count])
  let digest = [UInt8](hasher.finalize())

  // SpcIndirectDataContent: SpcPeImageData (no flags, the "<<<Obsolete>>>" file link) and the digest.
  let obsolete = Array("<<<Obsolete>>>".utf16.flatMap { [UInt8($0 >> 8), UInt8($0 & 0xff)] })
  let peImageData = seq([0x03, 0x01, 0x00], tlv(0xa0, tlv(0xa2, tlv(0x80, obsolete))))
  let indirect = seq(
    seq(oid("1.3.6.1.4.1.311.2.1.15"), peImageData),
    seq(seq(oid("2.16.840.1.101.3.4.2.1"), null), octets(digest)))
  // The signed attributes' message digest covers the content's value, without its SEQUENCE header.
  let indirectValue = Array(indirect[element(indirect, 0).content...])
  let contentDigest = [UInt8](SHA256.hash(data: indirectValue))
  let attributes = [
    seq(oid("1.2.840.113549.1.9.3"), setOf([oid("1.3.6.1.4.1.311.2.1.4")])),  // contentType: SPC_INDIRECT_DATA
    seq(oid("1.3.6.1.4.1.311.2.1.11"), setOf([seq(oid("1.3.6.1.4.1.311.2.1.21"))])),  // statement type: individual
    seq(oid("1.3.6.1.4.1.311.2.1.12"), setOf([seq()])),  // SpcSpOpusInfo, empty
    seq(oid("1.2.840.113549.1.9.4"), setOf([octets(contentDigest)])),  // messageDigest
  ]
  let signedAttributes = setOf(attributes)

  var key: SecKey?
  guard SecIdentityCopyPrivateKey(id, &key) == errSecSuccess, let key else { fail("no private key for the identity") }
  var error: Unmanaged<CFError>?
  guard let signature = SecKeyCreateSignature(key, .rsaSignatureMessagePKCS1v15SHA256, Data(signedAttributes) as CFData,
                                              &error) as Data?
  else { fail("signing failed: \(error!.takeRetainedValue())") }

  let signerInfo = seq(
    [0x02, 0x01, 0x01],  // version 1
    issuerAndSerial(chain[0]),
    seq(oid("2.16.840.1.101.3.4.2.1"), null),
    [0xa0] + Array(signedAttributes.dropFirst()),  // the same SET, [0] IMPLICIT
    seq(oid("1.2.840.113549.1.1.1"), null),
    octets([UInt8](signature)))
  let signedData = seq(
    [0x02, 0x01, 0x01],  // version 1
    setOf([seq(oid("2.16.840.1.101.3.4.2.1"), null)]),
    seq(oid("1.3.6.1.4.1.311.2.1.4"), tlv(0xa0, indirect)),
    tlv(0xa0, chain.flatMap { $0 }),
    setOf([signerInfo]))
  var pkcs7 = seq(oid("1.2.840.113549.1.7.2"), tlv(0xa0, signedData))
  while (pkcs7.count + 8) % 8 != 0 { pkcs7.append(0) }

  // WIN_CERTIFICATE: length, revision 2.0, PKCS#7 signed data.
  let tableAt = d.count
  var entry: [UInt8] = [0, 0, 0, 0, 0x00, 0x02, 0x02, 0x00]
  put32(&entry, 0, 8 + pkcs7.count)
  d += entry + pkcs7
  put32(&d, securityAt, tableAt)
  put32(&d, securityAt + 4, 8 + pkcs7.count)
  put32(&d, checksumAt, peChecksum(d, skipping: checksumAt))
  guard FileManager.default.createFile(atPath: path, contents: Data(d)) else { fail("can't write \(path)") }
}

let args = CommandLine.arguments
guard args.count >= 3 else { fail("usage: authenticode-sign <identity> <file>...") }
let (id, chain) = identity(named: args[1])
for path in args.dropFirst(2) { sign(path, id, chain) }

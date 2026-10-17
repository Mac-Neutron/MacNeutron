import Foundation
import Testing
@testable import MacNeutronCore

/// 2000 lines of "MacNeutron payload\n" (38000 bytes), compressed in the builder by `zstd -19` and `xz -9`.
private let zstdVector = hex("28b52ffd647093dd0000984d61634e657574726f6e207061796c6f61640a0100b528a6cf30e69040ca")
private let xzVector = hex("""
    fd377a585a000004e6d6b44604c061f0a80221011c000000000000003698dc0de0946f00595d0026984864e9308ceb3ed2bb60fa6dd46e7462\
    bd75a0570c8ddc0f4ee37bbaa3d643381926765b3ba3843b72421a082ab75ee15ef1464f3a182fa2848a2a63a597fedb4e00f271db0d6515a4\
    1246bab2161fae216c0e2eeeb70000000000fa6dac45f21e94f600017df0a80200001b7f86b7b1c467fb020000000004595a
    """)
private let plain = Data(String(repeating: "MacNeutron payload\n", count: 2000).utf8)

private func hex(_ text: String) -> Data {
    let digits = Array(text.utf8)
    return Data(stride(from: 0, to: digits.count, by: 2).map { UInt8(String(decoding: digits[$0..<$0 + 2], as: UTF8.self), radix: 16)! })
}

/// The whole stream, fed in 7-byte pieces.
private func decode(_ compressor: String, _ input: Data) throws -> Data {
    var decoder = try PayloadDecoder(compressor: compressor)
    var out = Data()
    for start in stride(from: 0, to: input.count, by: 7) {
        out += try decoder.decode(input.subdata(in: start..<min(start + 7, input.count)))
    }
    return out + (try decoder.finish())
}

@Test func zstdAndXzDecodeToTheSameBytes() throws {
    #expect(try decode("zstd", zstdVector) == plain)
    #expect(try decode("xz", xzVector) == plain)
}

@Test func aCutStreamIsTruncated() throws {
    #expect(throws: VMSetupError.truncated) { try decode("zstd", zstdVector.dropLast(4)) }
    #expect(throws: VMSetupError.truncated) { try decode("xz", xzVector.dropLast(4)) }
}

@Test func anUnknownCompressorIsUnsupported() throws {
    #expect(throws: VMSetupError.unsupported("lzip")) { try PayloadDecoder(compressor: "lzip") }
}

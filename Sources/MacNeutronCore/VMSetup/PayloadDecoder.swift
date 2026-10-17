import Compression
import CZstd
import Foundation

/// One RPM payload's decompressor, fed as the payload streams in: zstd (most of Fedora's packages, through the vendored
/// CZstd) or xz (some, kernel-core among them, through Compression's LZMA, which reads the xz container).
struct PayloadDecoder {
    private enum Stream { case zstd(Zstd), xz(Xz) }
    private let stream: Stream

    init(compressor: String) throws {
        switch compressor {
        case "zstd": stream = .zstd(Zstd())
        case "xz": stream = .xz(try Xz())
        default: throw VMSetupError.unsupported(compressor)
        }
    }

    /// As much output as this input gives.
    mutating func decode(_ input: Data) throws -> Data {
        switch stream {
        case .zstd(let z): try z.decode(input)
        case .xz(let x): try x.process(input, finalize: false)
        }
    }

    /// The rest of the output; `.truncated` when the stream didn't end.
    mutating func finish() throws -> Data {
        switch stream {
        case .zstd(let z): return try z.finish()
        case .xz(let x):
            do {
                let tail = try x.process(Data(), finalize: true)
                guard x.ended else { throw VMSetupError.truncated }
                return tail
            } catch VMSetupError.corrupt { throw VMSetupError.truncated }  // the decoder's word for "ran out"
        }
    }
}

private let chunk = 1 << 18

private final class Zstd {
    let stream = ZSTD_createDStream()
    /// ZSTD_decompressStream's last hint: 0 once a frame has ended.
    var pending = 1
    deinit { ZSTD_freeDStream(stream) }

    func decode(_ data: Data) throws -> Data {
        guard !data.isEmpty else { return Data() }
        var out = Data(), buffer = [UInt8](repeating: 0, count: chunk)
        try data.withUnsafeBytes { raw in
            var input = ZSTD_inBuffer(src: raw.baseAddress, size: raw.count, pos: 0)
            var produced = 0
            repeat {
                produced = try buffer.withUnsafeMutableBytes { b in
                    var output = ZSTD_outBuffer(dst: b.baseAddress, size: b.count, pos: 0)
                    let hint = ZSTD_decompressStream(stream, &output, &input)
                    if ZSTD_isError(hint) != 0 { throw VMSetupError.corrupt }
                    pending = hint
                    return output.pos
                }
                out.append(contentsOf: buffer[..<produced])
            } while input.pos < input.size || produced == chunk
        }
        return out
    }

    func finish() throws -> Data {
        guard pending == 0 else { throw VMSetupError.truncated }
        return Data()
    }
}

private final class Xz {
    let stream = UnsafeMutablePointer<compression_stream>.allocate(capacity: 1)
    private(set) var ended = false

    init() throws {
        guard compression_stream_init(stream, COMPRESSION_STREAM_DECODE, COMPRESSION_LZMA) == COMPRESSION_STATUS_OK else {
            stream.deallocate()
            throw VMSetupError.unsupported("xz")
        }
    }

    deinit {
        compression_stream_destroy(stream)
        stream.deallocate()
    }

    func process(_ data: Data, finalize: Bool) throws -> Data {
        guard !ended else { return Data() }
        var out = Data()
        let buffer = UnsafeMutablePointer<UInt8>.allocate(capacity: chunk)
        defer { buffer.deallocate() }
        let flags = finalize ? Int32(COMPRESSION_STREAM_FINALIZE.rawValue) : 0
        try data.withUnsafeBytes { raw in
            stream.pointee.src_ptr = raw.bindMemory(to: UInt8.self).baseAddress ?? UnsafePointer(buffer)
            stream.pointee.src_size = raw.count
            repeat {
                stream.pointee.dst_ptr = buffer
                stream.pointee.dst_size = chunk
                let status = compression_stream_process(stream, flags)
                out.append(buffer, count: chunk - stream.pointee.dst_size)
                if status == COMPRESSION_STATUS_END { ended = true; return }
                guard status == COMPRESSION_STATUS_OK else { throw VMSetupError.corrupt }
            } while stream.pointee.src_size > 0 || stream.pointee.dst_size == 0
        }
        return out
    }
}

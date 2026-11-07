//! One RPM payload's decompressor, fed as the payload streams in: zstd (most of Fedora's packages) or xz (some,
//! kernel-core among them). zstd is compiled into the binary (zstd-sys, zstd 1.5.7, as Swift vendored its decoder) and
//! xz goes through Apple's Compression framework (COMPRESSION_LZMA reads the xz container), so nothing outside the
//! system is loaded at run time.

use crate::vm_setup_error::VMSetupError;

#[derive(Debug)]
pub struct PayloadDecoder {
    stream: Stream,
}

#[derive(Debug)]
enum Stream {
    Zstd(Zstd),
    Xz(Xz),
}

impl PayloadDecoder {
    pub fn new(compressor: &str) -> Result<Self, VMSetupError> {
        let stream = match compressor {
            "zstd" => Stream::Zstd(Zstd::new()?),
            "xz" => Stream::Xz(Xz::new()?),
            other => return Err(VMSetupError::Unsupported(other.to_string())),
        };
        Ok(PayloadDecoder { stream })
    }

    /// As much output as this input gives.
    pub fn decode(&mut self, input: &[u8]) -> Result<Vec<u8>, VMSetupError> {
        match &mut self.stream {
            Stream::Zstd(z) => z.decode(input),
            Stream::Xz(x) => x.process(input, false),
        }
    }

    /// The rest of the output; `.Truncated` when the stream didn't end.
    pub fn finish(&mut self) -> Result<Vec<u8>, VMSetupError> {
        match &mut self.stream {
            Stream::Zstd(z) => z.finish(),
            Stream::Xz(x) => match x.process(&[], true) {
                Ok(tail) => {
                    if x.ended {
                        Ok(tail)
                    } else {
                        Err(VMSetupError::Truncated)
                    }
                }
                Err(VMSetupError::Corrupt) => Err(VMSetupError::Truncated),
                Err(other) => Err(other),
            },
        }
    }
}

// zstd's functions below come from zstd-sys's static build.
use zstd_sys as _;

const CHUNK: usize = 1 << 18;

#[repr(C)]
struct ZstdInBuffer {
    src: *const u8,
    size: usize,
    pos: usize,
}

#[repr(C)]
struct ZstdOutBuffer {
    dst: *mut u8,
    size: usize,
    pos: usize,
}

extern "C" {
    fn ZSTD_createDStream() -> *mut std::ffi::c_void;
    fn ZSTD_freeDStream(stream: *mut std::ffi::c_void) -> usize;
    fn ZSTD_isError(code: usize) -> u32;
    fn ZSTD_decompressStream(
        stream: *mut std::ffi::c_void,
        output: *mut ZstdOutBuffer,
        input: *mut ZstdInBuffer,
    ) -> usize;
}

#[derive(Debug)]
struct Zstd {
    stream: *mut std::ffi::c_void,
    /// `ZSTD_decompressStream`'s last hint: 0 once a frame has ended.
    pending: usize,
}

impl Zstd {
    fn new() -> Result<Self, VMSetupError> {
        let stream = unsafe { ZSTD_createDStream() };
        if stream.is_null() {
            return Err(VMSetupError::Unsupported("zstd".into()));
        }
        Ok(Zstd { stream, pending: 1 })
    }

    fn decode(&mut self, data: &[u8]) -> Result<Vec<u8>, VMSetupError> {
        if data.is_empty() {
            return Ok(Vec::new());
        }
        let mut out = Vec::new();
        let mut buffer = vec![0u8; CHUNK];
        let mut input = ZstdInBuffer { src: data.as_ptr(), size: data.len(), pos: 0 };
        loop {
            let mut output = ZstdOutBuffer { dst: buffer.as_mut_ptr(), size: buffer.len(), pos: 0 };
            let hint = unsafe { ZSTD_decompressStream(self.stream, &mut output, &mut input) };
            if unsafe { ZSTD_isError(hint) } != 0 {
                return Err(VMSetupError::Corrupt);
            }
            self.pending = hint;
            out.extend_from_slice(&buffer[..output.pos]);
            if !(input.pos < input.size || output.pos == CHUNK) {
                break;
            }
        }
        Ok(out)
    }

    fn finish(&self) -> Result<Vec<u8>, VMSetupError> {
        if self.pending == 0 {
            Ok(Vec::new())
        } else {
            Err(VMSetupError::Truncated)
        }
    }
}

impl Drop for Zstd {
    fn drop(&mut self) {
        unsafe { ZSTD_freeDStream(self.stream) };
    }
}

// Apple's Compression framework (libcompression), the decoder Swift used: COMPRESSION_LZMA is the xz container.

#[repr(C)]
struct CompressionStream {
    dst_ptr: *mut u8,
    dst_size: usize,
    src_ptr: *const u8,
    src_size: usize,
    state: *mut std::ffi::c_void,
}

const COMPRESSION_STREAM_DECODE: i32 = 1;
const COMPRESSION_STREAM_FINALIZE: i32 = 0x0001;
const COMPRESSION_LZMA: i32 = 0x306;
const COMPRESSION_STATUS_OK: i32 = 0;
const COMPRESSION_STATUS_END: i32 = 1;

#[link(name = "compression")]
extern "C" {
    fn compression_stream_init(stream: *mut CompressionStream, operation: i32, algorithm: i32) -> i32;
    fn compression_stream_process(stream: *mut CompressionStream, flags: i32) -> i32;
    fn compression_stream_destroy(stream: *mut CompressionStream) -> i32;
}

struct Xz {
    stream: Box<CompressionStream>,
    ended: bool,
}

impl std::fmt::Debug for Xz {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Xz").field("ended", &self.ended).finish()
    }
}

impl Xz {
    fn new() -> Result<Self, VMSetupError> {
        let mut stream: Box<CompressionStream> = Box::new(unsafe { std::mem::zeroed() });
        let status = unsafe { compression_stream_init(&mut *stream, COMPRESSION_STREAM_DECODE, COMPRESSION_LZMA) };
        if status != COMPRESSION_STATUS_OK {
            return Err(VMSetupError::Unsupported("xz".into()));
        }
        Ok(Xz { stream, ended: false })
    }

    fn process(&mut self, data: &[u8], finalize: bool) -> Result<Vec<u8>, VMSetupError> {
        if self.ended {
            return Ok(Vec::new());
        }
        let mut out = Vec::new();
        let mut buffer = vec![0u8; CHUNK];
        self.stream.src_ptr = if data.is_empty() { buffer.as_ptr() } else { data.as_ptr() };
        self.stream.src_size = data.len();
        let flags = if finalize { COMPRESSION_STREAM_FINALIZE } else { 0 };
        loop {
            self.stream.dst_ptr = buffer.as_mut_ptr();
            self.stream.dst_size = CHUNK;
            let status = unsafe { compression_stream_process(&mut *self.stream, flags) };
            out.extend_from_slice(&buffer[..CHUNK - self.stream.dst_size]);
            if status == COMPRESSION_STATUS_END {
                self.ended = true;
                return Ok(out);
            }
            if status != COMPRESSION_STATUS_OK {
                return Err(VMSetupError::Corrupt);
            }
            if !(self.stream.src_size > 0 || self.stream.dst_size == 0) {
                break;
            }
        }
        Ok(out)
    }
}

impl Drop for Xz {
    fn drop(&mut self) {
        unsafe { compression_stream_destroy(&mut *self.stream) };
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn hex(text: &str) -> Vec<u8> {
        let digits: Vec<u8> = text.bytes().filter(|b| !b.is_ascii_whitespace()).collect();
        digits.chunks(2).map(|p| u8::from_str_radix(std::str::from_utf8(p).unwrap(), 16).unwrap()).collect()
    }

    /// 2000 lines of "MacNeutron payload\n" (38000 bytes), compressed by `zstd -19` and `xz -9`.
    fn zstd_vector() -> Vec<u8> {
        hex("28b52ffd647093dd0000984d61634e657574726f6e207061796c6f61640a0100b528a6cf30e69040ca")
    }
    fn xz_vector() -> Vec<u8> {
        hex(concat!(
            "fd377a585a000004e6d6b44604c061f0a80221011c000000000000003698dc0de0946f00595d0026984864e9308ceb3ed2bb60fa6dd46e7462",
            "bd75a0570c8ddc0f4ee37bbaa3d643381926765b3ba3843b72421a082ab75ee15ef1464f3a182fa2848a2a63a597fedb4e00f271db0d6515a4",
            "1246bab2161fae216c0e2eeeb70000000000fa6dac45f21e94f600017df0a80200001b7f86b7b1c467fb020000000004595a"
        ))
    }
    fn plain() -> Vec<u8> {
        "MacNeutron payload\n".repeat(2000).into_bytes()
    }

    fn decode(compressor: &str, input: &[u8]) -> Result<Vec<u8>, VMSetupError> {
        let mut decoder = PayloadDecoder::new(compressor)?;
        let mut out = Vec::new();
        let mut start = 0;
        while start < input.len() {
            let end = (start + 7).min(input.len());
            out.extend(decoder.decode(&input[start..end])?);
            start = end;
        }
        out.extend(decoder.finish()?);
        Ok(out)
    }

    #[test]
    fn zstd_and_xz_decode_to_the_same_bytes() {
        assert_eq!(decode("zstd", &zstd_vector()).unwrap(), plain());
        assert_eq!(decode("xz", &xz_vector()).unwrap(), plain());
    }

    #[test]
    fn a_cut_stream_is_truncated() {
        let z = zstd_vector();
        assert_eq!(decode("zstd", &z[..z.len() - 4]).unwrap_err(), VMSetupError::Truncated);
        let x = xz_vector();
        assert_eq!(decode("xz", &x[..x.len() - 4]).unwrap_err(), VMSetupError::Truncated);
    }

    #[test]
    fn an_unknown_compressor_is_unsupported() {
        assert_eq!(PayloadDecoder::new("lzip").unwrap_err(), VMSetupError::Unsupported("lzip".into()));
    }
}

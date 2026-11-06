// The VM setup reads RPM payloads, which are zstd- or xz-compressed. Rather than hand-port the C decoders, link
// the system libzstd and liblzma (their headers live where Homebrew, and on Apple's build machines, put them).

use std::path::PathBuf;

fn main() {
    println!("cargo:rerun-if-changed=build.rs");

    for (name, header, lib) in [
        ("zstd", "zstd.h", "zstd"),
        ("xz", "lzma.h", "lzma"),
    ] {
        if let Some(include) = header_dir(header) {
            println!("cargo:include={}", include.display());
        }
        if let Some(libdir) = library_dir(lib) {
            println!("cargo:rustc-link-search=native={}", libdir.display());
        }
        let _ = name;
    }

    println!("cargo:rustc-link-lib=dylib=zstd");
    println!("cargo:rustc-link-lib=dylib=lzma");
}

fn header_dir(header: &str) -> Option<PathBuf> {
    for root in ["/opt/homebrew/include", "/usr/local/include", "/opt/homebrew/opt/zstd/include"] {
        let candidate = PathBuf::from(root).join(header);
        if candidate.exists() {
            return Some(PathBuf::from(root));
        }
    }
    None
}

fn library_dir(lib: &str) -> Option<PathBuf> {
    for root in ["/opt/homebrew/lib", "/usr/local/lib"] {
        let candidate = PathBuf::from(root).join(format!("lib{lib}.dylib"));
        if candidate.exists() {
            return Some(PathBuf::from(root));
        }
    }
    None
}

//! The setup's downloads, each into cache/<sha256> and checked for its size and SHA-256 before it's kept.

use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};

use crate::posix;
use crate::valve_client::{Fetch, ValveClientError};
use crate::vm_setup_error::VMSetupError;

/// One item to fetch: its sources in turn, its size and SHA-256, and the name to report it under.
#[derive(Clone)]
pub struct DownloadItem {
    pub sources: Vec<String>,
    pub size: i64,
    pub sha256: String,
    pub name: String,
}

/// A `@Sendable (Int) async -> Void` in Swift: between rounds, a short network drop shouldn't use up every try.
pub type Pause = Arc<dyn Fn(i64) + Send + Sync>;

/// A `(Int64, Int64) -> Void` progress callback: bytes done, then the total.
pub type Progress = Arc<dyn Fn(i64, i64) + Send + Sync>;

pub struct VMDownloads;

impl VMDownloads {
    /// Between rounds: 1 s, then 4 s.
    pub fn backoff(seconds: i64) {
        std::thread::sleep(std::time::Duration::from_secs(seconds as u64));
    }

    /// The cached copy if it's right, else each source in turn, the whole list up to three times.
    pub fn fetch(
        sources: &[String],
        size: i64,
        sha256: &str,
        name: &str,
        cache: &Path,
        fetch: &Fetch,
        pause: &Pause,
    ) -> Result<PathBuf, VMSetupError> {
        let kept = cache.join(sha256);
        if Self::matches(&kept, size, sha256) {
            return Ok(kept);
        }
        std::fs::create_dir_all(cache).map_err(|_| VMSetupError::Download { name: name.to_string() })?;
        for round in 0..3 {
            if round > 0 {
                pause(if round == 1 { 1 } else { 4 });
            }
            for url in sources {
                let file = match fetch(url) {
                    Ok(file) => file,
                    Err(ValveClientError::Cancelled) => return Err(VMSetupError::Cancelled),
                    Err(_) => continue,
                };
                if Self::matches(&file, size, sha256) {
                    let _ = std::fs::remove_file(&kept);
                    std::fs::rename(&file, &kept)
                        .map_err(|_| VMSetupError::Download { name: name.to_string() })?;
                    return Ok(kept);
                }
                let _ = std::fs::remove_file(&file);
            }
        }
        Err(VMSetupError::Download { name: name.to_string() })
    }

    /// Every item, four at a time; `progress` gets the bytes of the files done (cached ones count at once) and the total.
    pub fn fetch_all(
        items: &[DownloadItem],
        cache: &Path,
        fetch: &Fetch,
        pause: &Pause,
        progress: &Progress,
    ) -> Result<(), VMSetupError> {
        let total: i64 = items.iter().map(|i| i.size).sum();
        progress(0, total);
        let done = AtomicUsize::new(0);
        let next = AtomicUsize::new(0);
        let failure: Mutex<Option<VMSetupError>> = Mutex::new(None);
        let workers = items.len().min(4);
        std::thread::scope(|scope| {
            for _ in 0..workers {
                scope.spawn(|| {
                    loop {
                        if failure.lock().unwrap().is_some() {
                            return;
                        }
                        let i = next.fetch_add(1, Ordering::SeqCst);
                        let Some(item) = items.get(i) else { return };
                        match Self::fetch(&item.sources, item.size, &item.sha256, &item.name, cache, fetch, pause) {
                            Ok(_) => {
                                let bytes = done.fetch_add(item.size as usize, Ordering::SeqCst) as i64 + item.size;
                                progress(bytes, total);
                            }
                            Err(error) => {
                                let mut slot = failure.lock().unwrap();
                                if slot.is_none() {
                                    *slot = Some(error);
                                }
                                return;
                            }
                        }
                    }
                });
            }
        });
        match failure.into_inner().unwrap() {
            Some(error) => Err(error),
            None => Ok(()),
        }
    }

    pub fn matches(file: &Path, size: i64, sha256: &str) -> bool {
        let Ok(metadata) = std::fs::metadata(file) else { return false };
        if metadata.len() as i64 != size {
            return false;
        }
        match posix::sha256_file(file) {
            Ok(digest) => digest == sha256,
            Err(_) => false,
        }
    }
}

/// The real system's downloader: `/usr/bin/curl` into a temporary file, which the caller checks and keeps.
/// The Rust port has no HTTP client crate, and curl ships with macOS.
pub fn standard_fetch() -> Fetch {
    std::sync::Arc::new(|url: &str| {
        let name = url.rsplit('/').next().unwrap_or("download").to_string();
        // Each file alone in a fresh folder, as Fetch promises: ValveClient::update deletes the folder of what it
        // fetched, so a shared one would take a parallel download with it.
        let directory = std::env::temp_dir().join(format!("macneutron-download-{}-{}", std::process::id(), unique_stamp()));
        std::fs::create_dir(&directory).map_err(|_| ValveClientError::Http(0))?;
        let out = directory.join(name);
        let status = std::process::Command::new("/usr/bin/curl")
            .args(["-fsSL", "--retry", "2", "-o"])
            .arg(&out)
            .arg("--")
            .arg(url)
            .status()
            .map_err(|_| ValveClientError::Http(0))?;
        if !status.success() {
            let _ = std::fs::remove_file(&out);
            return Err(ValveClientError::Http(status.code().unwrap_or(0) as u16));
        }
        Ok(out)
    })
}

/// Nanoseconds plus a per-process counter, so two fetches in the same nanosecond still get their own folders.
fn unique_stamp() -> String {
    use std::sync::atomic::{AtomicU64, Ordering};
    use std::time::{SystemTime, UNIX_EPOCH};
    static NEXT: AtomicU64 = AtomicU64::new(0);
    let nanos = SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_nanos()).unwrap_or(0);
    format!("{nanos}-{}", NEXT.fetch_add(1, Ordering::Relaxed))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_standard_fetch_gets_a_folder_of_its_own() {
        let source = std::env::temp_dir().join(format!("mn-fetch-source-{}", std::process::id()));
        std::fs::write(&source, b"bytes").unwrap();
        let url = format!("file://{}", source.display());
        let fetch = standard_fetch();
        let (a, b) = (fetch(&url).unwrap(), fetch(&url).unwrap());
        assert_ne!(a.parent(), b.parent());
        assert_eq!(a.file_name(), source.file_name());
        assert_eq!(std::fs::read(&a).unwrap(), b"bytes");
        for path in [&a, &b] {
            let _ = std::fs::remove_dir_all(path.parent().unwrap());
        }
        let _ = std::fs::remove_file(&source);
    }
    use crate::vm_setup_manifest::{Download, FEXRootfs, Koji, Package, VMSetupManifest};
    use std::collections::HashMap;

    fn temp(name: &str) -> PathBuf {
        crate::rpm_fixture::temp(name)
    }

    fn sha(data: &[u8]) -> String {
        posix::sha256(data)
    }

    /// Serves fixed bytes per URL, records every request; a URL it doesn't know fails as a 404 would.
    struct FakeMirror {
        files: HashMap<String, Vec<u8>>,
        served: Mutex<Vec<String>>,
    }

    impl FakeMirror {
        fn new(files: HashMap<String, Vec<u8>>) -> Self {
            FakeMirror { files, served: Mutex::new(Vec::new()) }
        }

        fn requests(&self) -> Vec<String> {
            self.served.lock().unwrap().clone()
        }

        fn fetch(self: &Arc<Self>) -> Fetch {
            let this = Arc::clone(self);
            Arc::new(move |url: &str| {
                this.served.lock().unwrap().push(url.to_string());
                let data = this.files.get(url).ok_or(ValveClientError::Http(404))?;
                let name = url.rsplit('/').next().unwrap_or("file").to_string();
                let out = temp(&format!("mirror-{}-{}", std::process::id(), uuid_like()))
                    .join(name);
                std::fs::write(&out, data).map_err(|_| ValveClientError::Http(0))?;
                Ok(out)
            })
        }
    }

    fn uuid_like() -> String {
        use std::time::{SystemTime, UNIX_EPOCH};
        let n = SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos();
        format!("{n}")
    }

    fn kernel() -> Package {
        Package {
            file: "kernel-core-7.2.10-200.fc44.aarch64.rpm".into(),
            name: "kernel-core".into(),
            version: "7.2.10".into(),
            release: "200.fc44".into(),
            arch: "aarch64".into(),
            size: 5,
            sha256: sha(b"core\n"),
            source_rpm: Some("kernel-7.2.10-200.fc44.src.rpm".into()),
        }
    }

    fn manifest() -> VMSetupManifest {
        VMSetupManifest {
            version: 1,
            parts: Download {
                url: "https://example.invalid/parts.tar.xz".into(),
                size: 3,
                sha256: "0".repeat(64),
            },
            kernel: "7.2.10-200.fc44.aarch64".into(),
            packages: vec![kernel()],
            fex_rootfs: FEXRootfs {
                package: Package {
                    file: "fex-emu-rootfs-fedora-44^20260410.n.0-1.fc44.noarch.rpm".into(),
                    name: "fex-emu-rootfs-fedora".into(),
                    version: "44^20260410.n.0".into(),
                    release: "1.fc44".into(),
                    arch: "noarch".into(),
                    size: 1,
                    sha256: "1".repeat(64),
                    source_rpm: None,
                },
                image: "/usr/share/fex-emu/RootFS/default.erofs".into(),
                image_sha256: "2".repeat(64),
            },
            koji: Koji {
                base: "https://kojipkgs.fedoraproject.org/packages/".into(),
                signed_key: "6d9f90a6".into(),
            },
            mirrors: None,
        }
    }

    fn no_pause() -> Pause {
        Arc::new(|_| {})
    }

    fn recording_pause(all: Arc<Mutex<Vec<i64>>>) -> Pause {
        Arc::new(move |s| all.lock().unwrap().push(s))
    }

    #[test]
    fn a_mirror_miss_falls_back_to_koji() {
        let m = manifest();
        let sources = m.sources(&kernel());
        let mut files = HashMap::new();
        files.insert(sources[2].clone(), b"core\n".to_vec());
        let mirror = Arc::new(FakeMirror::new(files));
        let cache = temp("fallback");
        let got = VMDownloads::fetch(&sources, 5, &kernel().sha256, &kernel().file, &cache, &mirror.fetch(), &no_pause()).unwrap();
        assert_eq!(got, cache.join(&kernel().sha256));
        assert_eq!(std::fs::read(&got).unwrap(), b"core\n");
        assert_eq!(mirror.requests(), sources);
    }

    #[test]
    fn a_file_wrong_everywhere_fails_with_its_name() {
        let good = "https://example.invalid/good.rpm".to_string();
        let bad = "https://example.invalid/x.rpm".to_string();
        let mut files = HashMap::new();
        files.insert(good.clone(), b"good\n".to_vec());
        files.insert(bad.clone(), b"evil\n".to_vec());
        let mirror = Arc::new(FakeMirror::new(files));
        let cache = temp("wrong");
        let items = vec![
            DownloadItem { sources: vec![good.clone()], size: 5, sha256: sha(b"good\n"), name: "good.rpm".into() },
            DownloadItem { sources: vec![bad.clone()], size: 5, sha256: sha(b"bad\n"), name: "x.rpm".into() },
        ];
        let progress: Progress = Arc::new(|_, _| {});
        let error = VMDownloads::fetch_all(&items, &cache, &mirror.fetch(), &no_pause(), &progress).unwrap_err();
        assert_eq!(error, VMSetupError::Download { name: "x.rpm".into() });
        assert_eq!(mirror.requests().iter().filter(|u| **u == bad).count(), 3);
        assert!(cache.join(sha(b"good\n")).exists());
    }

    #[test]
    fn a_cached_file_is_not_fetched_again() {
        let cache = temp("cached");
        std::fs::write(cache.join(&kernel().sha256), b"core\n").unwrap();
        let mirror = Arc::new(FakeMirror::new(HashMap::new()));
        let reported = Arc::new(Mutex::new(Vec::<i64>::new()));
        let recorder = Arc::clone(&reported);
        let progress: Progress = Arc::new(move |done, _| recorder.lock().unwrap().push(done));
        let items = vec![DownloadItem {
            sources: manifest().sources(&kernel()),
            size: 5,
            sha256: kernel().sha256,
            name: kernel().file,
        }];
        VMDownloads::fetch_all(&items, &cache, &mirror.fetch(), &no_pause(), &progress).unwrap();
        assert!(mirror.requests().is_empty());
        assert_eq!(*reported.lock().unwrap(), vec![0, 5]);
    }

    #[test]
    fn rounds_wait_longer_each_time() {
        let bad = vec!["https://example.invalid/x.rpm".to_string()];
        let mut files = HashMap::new();
        files.insert(bad[0].clone(), b"evil\n".to_vec());
        let mirror = Arc::new(FakeMirror::new(files));
        let pauses = Arc::new(Mutex::new(Vec::<i64>::new()));
        let error = VMDownloads::fetch(&bad, 5, &sha(b"good\n"), "x.rpm", &temp("rounds"), &mirror.fetch(), &recording_pause(Arc::clone(&pauses))).unwrap_err();
        assert_eq!(error, VMSetupError::Download { name: "x.rpm".into() });
        assert_eq!(*pauses.lock().unwrap(), vec![1, 4]);
    }

    #[test]
    fn a_cancelled_download_stops() {
        let cancelled: Fetch = Arc::new(|_| Err(ValveClientError::Cancelled));
        let error = VMDownloads::fetch(
            &["https://example.invalid/x.rpm".to_string()],
            5,
            "aa",
            "x.rpm",
            &temp("cancelled"),
            &cancelled,
            &no_pause(),
        )
        .unwrap_err();
        assert_eq!(error, VMSetupError::Cancelled);
    }

    #[test]
    fn the_release_names_a_package_no_source_serves() {
        // Koji files a binary package under its source package: kernel-core is in packages/kernel/.
        let m = manifest();
        let sources = m.sources(&kernel());
        let fex = m.sources(&m.fex_rootfs.package);
        let only_kernel = VMSetupManifest { packages: vec![kernel()], ..m.clone() };
        let (k, f) = (sources[2].clone(), fex[0].clone());
        // the FEX root answers, at the wrong size
        let head = move |url: &str| if url == k { Some(5) } else if url == f { Some(7) } else { None };
        let missing = crate::vm_setup::VMSetup::missing_sources(&only_kernel, &head);
        assert_eq!(missing, vec![m.fex_rootfs.package.file.clone()]);
    }
}

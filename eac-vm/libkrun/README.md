# libkrun for the EAC microVM

The microVM host links libkrun (Apache-2.0, github.com/containers/libkrun) at the release in `PIN`, plus the patches
here. `build.sh` builds it into `build/eac-vm-work/libkrun/`. Make a new patch in `build/eac-vm-work/libkrun-src`
(branch `macneutron`), then `git format-patch --no-signature -o patches <pin>..macneutron`.

Patches:
- 0001: boot EFI zboot kernels (Fedora's vmlinuz has a zstd payload; the PE loader only scanned for gzip).
- 0002: free the guest's reported pages for real on macOS: unmap the range from the VM, MADV_FREE, map it back
  (madvise alone frees nothing while the VM maps a page a vCPU wrote).

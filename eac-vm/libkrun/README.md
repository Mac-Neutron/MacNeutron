# libkrun for the EAC microVM

The microVM host links libkrun (Apache-2.0, github.com/containers/libkrun) at the release in `PIN`, plus the patches
here. `build.sh` builds it into `build/eac-vm-work/libkrun/`. Make a new patch in `build/eac-vm-work/libkrun-src`
(branch `macneutron`), then `git format-patch --no-signature -o patches <pin>..macneutron`.

Patches:
- 0001: boot EFI zboot kernels (Fedora's vmlinuz has a zstd payload; the PE loader only scanned for gzip).

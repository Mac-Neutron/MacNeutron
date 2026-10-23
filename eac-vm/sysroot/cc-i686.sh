#!/bin/sh
# clang for i686 Linux (old WoW64's 32-bit Unix side) against eac-vm/sysroot's multilib sysroot (in the builder).
# 64-bit file offsets: a virtio-gpu blob's mmap offset (DRM's fake offsets start at 4 GiB on a 64-bit kernel) doesn't
# fit a 32-bit off_t.
exec clang --target=i686-redhat-linux --sysroot="${ROOT:-/var/cache/mn-sysroot-multilib}" -fuse-ld=lld \
  -D_FILE_OFFSET_BITS=64 "$@"

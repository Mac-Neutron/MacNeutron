#!/bin/sh
# clang for i686 Linux (old WoW64's 32-bit Unix side) against eac-vm/sysroot's multilib sysroot (in the builder).
exec clang --target=i686-redhat-linux --sysroot="${ROOT:-/var/cache/mn-sysroot-multilib}" -fuse-ld=lld "$@"

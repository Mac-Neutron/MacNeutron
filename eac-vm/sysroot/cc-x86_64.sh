#!/bin/sh
# clang for x86-64 Linux against eac-vm/sysroot's sysroot (in the builder).
exec clang --target=x86_64-redhat-linux --sysroot="${ROOT:-/var/cache/mn-sysroot-x86_64}" -fuse-ld=lld "$@"

#!/bin/sh
# clang for x86-64 Linux against vm/sysroot's sysroot (in the builder).
HERE=$(cd "$(dirname "$0")" && pwd)
exec clang --target=x86_64-redhat-linux --sysroot="${ROOT:-/var/cache/mn-sysroot-x86_64}" -fuse-ld="$HERE/ld-elf" "$@"

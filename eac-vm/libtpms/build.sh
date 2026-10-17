#!/bin/sh
# Builds libtpms (IBM's TPM 2.0 reference code, BSD-3-Clause) at the pinned tag and commit (PIN) as a static library
# for macneutron-vm's TPM engine (eac-vm/host/tpm.c): TPM 2.0 only, against Homebrew's openssl@3 libcrypto.a.
# Output: build/eac-vm-work/libtpms/{libtpms.a,include/libtpms/}. Source: build/eac-vm-work/libtpms-src.
# Plain clang over the sources src/Makefile.am lists, with the defines configure.ac picks on OpenSSL 3: autotools
# (automake, libtool) aren't installed here.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HERE=$ROOT/eac-vm/libtpms
SRC=$ROOT/build/eac-vm-work/libtpms-src
OUT=$ROOT/build/eac-vm-work/libtpms
set -- $(cat "$HERE/PIN"); TAG=$1; SHA=$2
SSL=${OPENSSL:-$(brew --prefix openssl@3)}

[ -d "$SRC/.git" ] || git clone -q --depth 1 --branch "$TAG" https://github.com/stefanberger/libtpms.git "$SRC"
[ "$(git -C "$SRC" rev-parse HEAD)" = "$SHA" ] || { echo "libtpms: $SRC is not at $SHA" >&2; exit 1; }
rm -rf "$OUT" && mkdir -p "$OUT/include/libtpms" "$OUT/obj"
cp "$SRC"/include/libtpms/*.h "$OUT/include/libtpms/"
set -- $(echo "${TAG#v}" | tr . ' ')
sed -e "s/@LIBTPMS_VER_MAJOR@/$1/" -e "s/@LIBTPMS_VER_MINOR@/$2/" -e "s/@LIBTPMS_VER_MICRO@/$3/" \
  "$SRC/include/libtpms/tpm_library.h.in" > "$OUT/include/libtpms/tpm_library.h"
printf '#define WITH_TPM2 1\n#define USE_OPENSSL_CRYPTO_LIBRARY 1\n' > "$OUT/obj/config.h"

# src/Makefile.am's common_CFLAGS, and libtpms_tpm2_la_CFLAGS for the TPM 2 sources (-w: its warnings aren't ours).
COMMON="-O2 -w -include tpm_library_conf.h -I$SRC/src -I$OUT/obj -I$OUT/include/libtpms -I$SSL/include
  -DOPENSSL_SUPPRESS_DEPRECATED -DTPM_LIBTPMS_CALLBACKS -DTPM_NV_DISK
  -DUSE_OPENSSL_FUNCTIONS_SYMMETRIC=1 -DUSE_OPENSSL_FUNCTIONS_EC=1 -DUSE_OPENSSL_FUNCTIONS_ECDSA=1
  -DUSE_OPENSSL_FUNCTIONS_RSA=1 -DUSE_OPENSSL_FUNCTIONS_SSKDF=1 -DUSE_EC_POINT_GET_AFFINE_COORDINATES_API=1"
TPM2="-D_POSIX_ -DTPM_POSIX -I$SRC/src/tpm2 -I$SRC/src/tpm2/crypto -I$SRC/src/tpm2/crypto/openssl"
sources() {  # the .c files of automake variable $1 (its = and += lists)
  awk -v v="$1" '$1 == v && ($2 == "=" || $2 == "+=") {on=1; next} on {print} on && !/\\$/ {on=0}' "$SRC/src/Makefile.am" |
    tr -d '\\' | tr ' \t' '\n\n' | /usr/bin/grep '\.c$'
}
export SRC OUT COMMON TPM2
{ sources libtpms_tpm2_la_SOURCES | sed 's/^/tpm2 /'; sources libtpms_la_SOURCES | sed 's/^/lib /'; } |
  xargs -P "$(sysctl -n hw.ncpu)" -n 2 sh -c 'flags=$COMMON; [ "$0" = lib ] || flags="$flags $TPM2"
    clang $flags -c -o "$OUT/obj/$(echo "$1" | tr / _).o" "$SRC/src/$1"'
ar rcs "$OUT/libtpms.a" "$OUT"/obj/*.o
echo "$OUT/libtpms.a"

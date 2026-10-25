# glibc-eac: Rogue Company's EOS EAC and glibc 2.34+

Rogue Company's EOS Easy Anti-Cheat is broken by glibc 2.34+ (Steam/Proton
issue [5214](https://github.com/ValveSoftware/Proton/issues/5214)).
`GloriousEggroll` bisected it to the ABI-name install and `GLIBC_PRIVATE`
libdl changes; reverting them (the `glibc-eac` patch set from
[Frogging-Family/glibc-eac](https://github.com/Frogging-Family/glibc-eac))
makes the game work on glibc 2.43.

`rogue_company_reverts.patch` is that revert set. It is applied to:

- the guest's aarch64 glibc 2.43 (the base image), rebuilt from glibc
  2.43 with the patch and installed into the base root before
  `mkfs.erofs`; and
- the FEX x86-64 RootFS glibc (Fedora's `x86-64-rootfs.erofs`), replaced
  with the glibc-eac 2.43 x86_64 build (the `glibc-eac` release package),
  then re-packed with `mkfs.erofs`.

Build sketch (aarch64, in the Fedora builder):

```sh
curl -LO https://ftp.gnu.org/gnu/glibc/glibc-2.43.tar.xz
tar -xf glibc-2.43.tar.xz && cd glibc-2.43
patch -p1 < ../eac-vm/glibc-eac/rogue_company_reverts.patch   # test-only hunks in elf/Makefile may reject
mkdir build && cd build
../configure --prefix=/usr --disable-werror --enable-kernel=5.10
make -j$(nproc) && sudo make install DESTDIR=/tmp/glibc-inst
sudo cp -a /tmp/glibc-inst/usr/lib64/. "$WORK/root/usr/lib64/"
sudo cp -a /tmp/glibc-inst/lib64/ld-linux-aarch64.so.1 "$WORK/root/lib64/"
```

x86-64 RootFS: extract `x86-64-rootfs.erofs` with `fsck.erofs
--extract`, replace `usr/lib64/{libc,libm,libpthread,libdl,libresolv,librt,
libutil,libcrypt,libnsl,libanl,libBrokenLocale,libthread_db,libmvec,
libc_malloc_debug}.so*` and `ld-linux-x86-64.so.2` with the glibc-eac
2.43 package's copies, then `mkfs.erofs -L mn-rootfs -zlz4hc` the tree.

Note: the patch's two `elf/Makefile` hunks are test-only and can reject on
2.43; the library changes apply.

# Host-side (macOS) builds of the Steam forwarding pieces; outputs go to build/eac-vm-work/steam-forward.
OUT ?= ../../build/eac-vm-work/steam-forward
CFLAGS ?= -O2 -g -Wall -Wextra -Wno-missing-field-initializers -std=c11

$(OUT)/marshal-test: tests/marshal-test.c marshal.c marshal.h wire.h
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/marshal-test.c marshal.c

marshal-test: $(OUT)/marshal-test
	$(OUT)/marshal-test
.PHONY: marshal-test

# lsteamclient (the tree the Mac bridge is built from), Wine's headers, the Mac Unix half.
LSC ?= ../../build/wine-arm64-src/lsteamclient/lsteamclient
WINEINC ?= ../../build/wine-arm64-src/wine/include
MAC_UNIXLIB ?= ../../build/wine-arm64/wine.app/Contents/Resources/lib/wine/aarch64-unix/lsteamclient.so
LSCFLAGS = -DWINE_UNIX_LIB -I. -I$(LSC) -I$(WINEINC) -Wno-unused-parameter -Wno-sign-compare -Wno-pragma-pack -Wno-macro-redefined

$(OUT)/descriptors.c: gen.py $(LSC)/unixlib_generated.h $(LSC)/unixlib.h
	python3 -I gen.py $(LSC) $(OUT)

# mn-steam-host with its ntdll.so stand-in and a copy of the Mac Unix half, together in $(OUT)/host.
$(OUT)/host/ntdll.so: host_wine.c host_wine.h marshal.c marshal.h
	mkdir -p $(OUT)/host
	$(CC) $(CFLAGS) $(LSCFLAGS) -dynamiclib -install_name @rpath/ntdll.so -o $@ host_wine.c marshal.c
$(OUT)/host/lsteamclient.so: $(MAC_UNIXLIB)
	mkdir -p $(OUT)/host
	cp $< $@
$(OUT)/host/mn-steam-host: host.c client.c marshal.c $(OUT)/descriptors.c $(OUT)/host/ntdll.so $(OUT)/host/lsteamclient.so
	$(CC) $(CFLAGS) $(LSCFLAGS) -o $@ host.c client.c marshal.c $(OUT)/descriptors.c $(OUT)/host/ntdll.so \
	    -Wl,-rpath,@executable_path
$(OUT)/host-client: tests/host-client.c tests/script.c client.c marshal.c $(OUT)/descriptors.c
	$(CC) $(CFLAGS) $(LSCFLAGS) -o $@ tests/host-client.c tests/script.c client.c marshal.c $(OUT)/descriptors.c

host: $(OUT)/host/mn-steam-host $(OUT)/host-client
.PHONY: host
$(OUT)/offline-test: tests/offline-test.c client.c marshal.c $(OUT)/descriptors.c $(OUT)/guest_table.c
	$(CC) $(CFLAGS) $(LSCFLAGS) -o $@ tests/offline-test.c client.c marshal.c $(OUT)/descriptors.c $(OUT)/guest_table.c
offline-test: $(OUT)/offline-test
	$(OUT)/offline-test
.PHONY: offline-test
$(OUT)/path-test: tests/path-test.c $(OUT)/host/ntdll.so
	$(CC) $(CFLAGS) $(LSCFLAGS) -o $@ tests/path-test.c $(OUT)/host/ntdll.so -Wl,-rpath,@executable_path/host
path-test: $(OUT)/path-test
	$(OUT)/path-test
.PHONY: path-test

# Host-side (macOS) builds of the Steam forwarding pieces; outputs go to build/eac-vm-work/steam-forward.
OUT ?= $(or $(BUILD_DIR),../../build)/eac-vm-work/steam-forward
CFLAGS ?= -O2 -g -Wall -Wextra -Wno-missing-field-initializers -std=c11

$(OUT)/marshal-test: tests/marshal-test.c marshal.c marshal.h wire.h
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/marshal-test.c marshal.c

marshal-test: $(OUT)/marshal-test
	$(OUT)/marshal-test
.PHONY: marshal-test

# lsteamclient (the tree the Mac bridge is built from), Wine's headers, the Mac Unix half.
LSC ?= $(or $(BUILD_DIR),../../build)/wine-arm64-src/lsteamclient/lsteamclient
WINEINC ?= $(or $(BUILD_DIR),../../build)/wine-arm64-src/wine/include
MAC_UNIXLIB ?= $(or $(BUILD_DIR),../../build)/wine-arm64/wine.app/Contents/Resources/lib/wine/aarch64-unix/lsteamclient.so
LSCFLAGS = -DWINE_UNIX_LIB -I. -I$(LSC) -I$(WINEINC) -Wno-unused-parameter -Wno-sign-compare -Wno-pragma-pack -Wno-macro-redefined

$(OUT)/descriptors.c: gen.py $(LSC)/unixlib_generated.h $(LSC)/unixlib.h
	python3 -I gen.py $(LSC) $(OUT)

# gen.py's arrays: none of them goes out as one element (tests/gen-check.py).
gen-check:
	python3 -I tests/gen-check.py $(LSC)
.PHONY: gen-check

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

# The host as macneutron-vm runs it: a bundle (Contents/MacOS: the binary, its ntdll.so stand-in, the Mac Unix half)
# with the cross-architecture entitlement and MacNeutron's provisioning profile, which the kernel needs before it maps
# the WoW64 calls' region below 4 GiB. Signed when MACNEUTRON_SIGN_IDENTITY and MACNEUTRON_PROVISIONING_PROFILE are set
# (release.sh signs its copy again), ad hoc otherwise: then 32-bit calls are unavailable.
HOST_APP = $(OUT)/host/mn-steam-host.app
HOST_ENT = ../../release/entitlements/mn-steam-host.plist
# The signing variables the bundle was made with, rewritten only when they change: exporting them after an ad hoc
# build signs it again.
HOST_SIGN = $(OUT)/host/.signing
$(HOST_SIGN): FORCE
	@mkdir -p $(@D); printf '%s\n' "$$MACNEUTRON_SIGN_IDENTITY" "$$MACNEUTRON_PROVISIONING_PROFILE" > $@.new; \
	  if cmp -s $@.new $@; then rm $@.new; else mv $@.new $@; fi
FORCE:
.PHONY: FORCE
$(HOST_APP): $(OUT)/host/mn-steam-host $(OUT)/host/ntdll.so $(OUT)/host/lsteamclient.so host-Info.plist $(HOST_ENT) \
             $(HOST_SIGN)
	@# Every symbol the Mac Unix half takes from ntdll.so is the stand-in's: mn-steam-host loads it with RTLD_NOW, and
	@# one missing (0.9.0's RtlUTF8ToUnicodeN) stopped every VM game's Steam.
	@nm -um $(OUT)/host/lsteamclient.so | sed -n 's/.* external _\([^ ]*\) (from @rpath\/ntdll.so)$$/\1/p' | sort > $(OUT)/host/.ntdll-wanted
	@nm -gU $(OUT)/host/ntdll.so | sed -n 's/.* _\([^ ]*\)$$/\1/p' | sort > $(OUT)/host/.ntdll-has
	@m=$$(comm -23 $(OUT)/host/.ntdll-wanted $(OUT)/host/.ntdll-has); \
	  [ -z "$$m" ] || { echo "mn-steam-host: host_wine.c lacks what lsteamclient.so takes from ntdll.so:" $$m >&2; exit 1; }
	rm -rf $@ && mkdir -p $@/Contents/MacOS
	cp host-Info.plist $@/Contents/Info.plist
	cp $(OUT)/host/mn-steam-host $(OUT)/host/ntdll.so $(OUT)/host/lsteamclient.so $@/Contents/MacOS/
	if [ -n "$$MACNEUTRON_SIGN_IDENTITY" ] && [ -n "$$MACNEUTRON_PROVISIONING_PROFILE" ]; then \
	  cp -X "$$MACNEUTRON_PROVISIONING_PROFILE" $@/Contents/embedded.provisionprofile && \
	  codesign -f -s "$$MACNEUTRON_SIGN_IDENTITY" --options runtime $@/Contents/MacOS/ntdll.so $@/Contents/MacOS/lsteamclient.so && \
	  codesign -f -s "$$MACNEUTRON_SIGN_IDENTITY" --options runtime --entitlements $(HOST_ENT) $@; \
	else codesign -f -s - $@; fi

host: $(HOST_APP) $(OUT)/host-client
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

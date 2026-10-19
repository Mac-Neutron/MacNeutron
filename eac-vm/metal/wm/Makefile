# Mac-side parts and tests of winemetal forwarding (milestone 4b-1); outputs go to build/eac-vm-work.
ROOT := $(abspath ../../..)
W := $(or $(BUILD_DIR),$(ROOT)/build)/eac-vm-work
OUT ?= $(W)/metal
HOST := $(W)/metal-host
WINEMETAL := $(or $(BUILD_DIR),$(ROOT)/build)/wine-arm64/wine.app/Contents/Resources/lib/wine/aarch64-unix/winemetal.so
DXMT := $(or $(BUILD_DIR),$(ROOT)/build)/wine-arm64-src/dxmt/src
CFLAGS ?= -O1 -g -Wall -Wextra -std=gnu2x -fsanitize=address,undefined
INC := -DDXMT_GUEST_VM -I$(DXMT)/winemetal -I.. -I.

# The shipped winemetal.so beside stand-ins for the Wine libraries it links (its LC_RPATH has @loader_path/).
$(HOST)/ntdll.so: host/stand-ins.c
	mkdir -p $(HOST)
	$(CC) -O2 -Wall -dynamiclib -install_name @rpath/ntdll.so -o $@ host/stand-ins.c
$(HOST)/winemac.so: host/winemac.m
	mkdir -p $(HOST)
	$(CC) -O2 -Wall -dynamiclib -install_name @rpath/winemac.so -o $@ host/winemac.m -framework AppKit -framework Metal \
	  -framework QuartzCore
$(HOST)/winemetal.so: $(WINEMETAL)
	mkdir -p $(HOST)
	cp $(WINEMETAL) $@
metal-host: $(HOST)/ntdll.so $(HOST)/winemac.so $(HOST)/winemetal.so
.PHONY: metal-host

$(OUT)/host-test: tests/host-test.c tests/sequence.c tests/sequence.h host/loader.c host/loader.h
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(INC) -o $@ tests/host-test.c tests/sequence.c host/loader.c -lpthread

host-test: metal-host $(OUT)/host-test
	$(OUT)/host-test $(HOST)
.PHONY: host-test

$(OUT)/handles-test: tests/handles-test.c guest/ghandles.c host/htable.c handles.h ../proto.h
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(INC) -o $@ tests/handles-test.c guest/ghandles.c host/htable.c -lpthread

handles-test: $(OUT)/handles-test
	$(OUT)/handles-test
.PHONY: handles-test

MARSHAL_SRC := calls.c cmdlist.c marshal.c slots.c guest/ghandles.c guest/galloc.c host/htable.c host/invoke.m host/memory.m \
  host/events.m host/present.m host/loader.c tests/loopback.c
$(OUT)/marshal-test: tests/marshal-test.m $(MARSHAL_SRC) calls.h marshal.h handles.h notify.h ../proto.h
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(INC) -I$(DXMT)/airconv -o $@ tests/marshal-test.m $(MARSHAL_SRC) \
	  -framework Foundation -framework Metal -framework QuartzCore -lpthread

marshal-test: $(OUT)/marshal-test
	MN_WM_CACHE_DIR=$(OUT)/cache-test $(OUT)/marshal-test
.PHONY: marshal-test

# The guest's code and the host's in one process (tests/loopback.c), against the real winemetal.so.
LOOP_SRC := calls.c cmdlist.c marshal.c slots.c guest/ghandles.c guest/galloc.c guest/winemetal.c host/htable.c host/invoke.m \
  host/memory.m host/events.m host/present.m host/loader.c tests/loopback.c tests/sequence.c tests/galloc-cases.c
$(OUT)/memory-test: tests/memory-test.m $(LOOP_SRC) calls.h marshal.h handles.h notify.h guest/galloc.h ../proto.h
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(INC) -I$(DXMT)/airconv -o $@ tests/memory-test.m $(LOOP_SRC) \
	  -framework AppKit -framework Metal -framework QuartzCore -lpthread

# The field kinds Direct3D forwarding adds (M2 Task 3), on memory-test's loopback.
$(OUT)/kinds-test: tests/kinds-test.c $(LOOP_SRC) calls.h marshal.h handles.h notify.h ../proto.h
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(INC) -I$(DXMT)/airconv -o $@ tests/kinds-test.c $(LOOP_SRC) \
	  -framework AppKit -framework Metal -framework QuartzCore -lpthread

kinds-test: metal-host $(OUT)/kinds-test
	$(OUT)/kinds-test
.PHONY: kinds-test

memory-test: metal-host $(OUT)/memory-test
	$(OUT)/memory-test $(HOST)
.PHONY: memory-test

# The game window (milestone 4c): the stand-in winemac.so under the real winemetal.so, its slots called directly.
$(OUT)/window-test: tests/window-test.m host/loader.c host/loader.h
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(INC) -o $@ tests/window-test.m host/loader.c -framework AppKit -framework Metal \
	  -framework QuartzCore -lpthread

window-test: metal-host $(OUT)/window-test
	$(OUT)/window-test $(HOST)
	MN_SCREEN=1600x1000 $(OUT)/window-test $(HOST)
.PHONY: window-test

# Mac-side parts and tests of winemetal forwarding (milestone 4b-1); outputs go to build/eac-vm-work.
ROOT := $(abspath ../../..)
W := $(ROOT)/build/eac-vm-work
OUT ?= $(W)/metal
HOST := $(W)/metal-host
WINEMETAL := $(ROOT)/build/wine-arm64/wine.app/Contents/Resources/lib/wine/aarch64-unix/winemetal.so
DXMT := $(ROOT)/build/wine-arm64-src/dxmt/src
CFLAGS ?= -O1 -g -Wall -Wextra -std=gnu2x -fsanitize=address,undefined
INC := -I$(DXMT)/winemetal -I.. -I.

# The shipped winemetal.so beside stand-ins for the Wine libraries it links (its LC_RPATH has @loader_path/).
$(HOST)/ntdll.so: host/stand-ins.c
	mkdir -p $(HOST)
	$(CC) -O2 -Wall -dynamiclib -DMN_NTDLL -install_name @rpath/ntdll.so -o $@ host/stand-ins.c
$(HOST)/winemac.so: host/stand-ins.c
	mkdir -p $(HOST)
	$(CC) -O2 -Wall -dynamiclib -install_name @rpath/winemac.so -o $@ host/stand-ins.c
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

MARSHAL_SRC := calls.c marshal.c guest/ghandles.c host/htable.c host/invoke.m host/loader.c
$(OUT)/marshal-test: tests/marshal-test.m $(MARSHAL_SRC) calls.h marshal.h handles.h ../proto.h
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(INC) -I$(DXMT)/airconv -o $@ tests/marshal-test.m $(MARSHAL_SRC) \
	  -framework Foundation -lpthread

marshal-test: $(OUT)/marshal-test
	$(OUT)/marshal-test
.PHONY: marshal-test

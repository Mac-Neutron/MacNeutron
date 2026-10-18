# Direct3D forwarding's generated tables and their tests (M2); outputs go to build/eac-vm-work/d3d-gen.
ROOT := $(abspath ../..)
W := $(ROOT)/build/eac-vm-work
GEN := $(W)/d3d-gen
I := $(ROOT)/build/dxmt-native/install/include-native
NT := $(ROOT)/eac-vm/dxmt-native/tests
WM := $(ROOT)/eac-vm/metal/wm
DXMT := $(ROOT)/build/wine-arm64-src/dxmt/src
CFLAGS ?= -O1 -g -Wall -Wextra -std=gnu2x -fsanitize=address,undefined
INC := -DDXMT_GUEST_VM -I$(I) -I$(DXMT)/winemetal -I$(DXMT)/airconv -I$(ROOT)/eac-vm/metal -I$(WM) -I$(WM)/tests -I$(GEN)

$(GEN)/calls.c: gen.py rules.txt
	python3 -W error::ResourceWarning gen.py --headers $(I) --rules rules.txt --out $(GEN) calls

# The guest's layouts the generator computed against the native headers': it compiles, or a size or offset differs.
layout: $(GEN)/calls.c
	clang++ -std=c++17 -fsyntax-only -include $(NT)/native-prelude.h -I$(NT)/include -I$(I)/d3d12 -I$(I)/windows \
	  -I$(I)/directx -I$(I) $(GEN)/layout.cpp
	@echo "layout: ok"
.PHONY: layout

# The host's calls into native DXMT (generated stubs.cpp), with DXMT's own flags: they must compile warning-free.
DXMT_CXXFLAGS := -std=c++17 -DWIDL_EXPLICIT_AGGREGATE_RETURNS -include $(NT)/native-prelude.h -I$(NT)/include \
  -I$(I)/d3d12 -I$(I)/windows -I$(I)/directx -I$(I) -I$(DXMT)/util -I$(DXMT)/d3d12 -I$(GEN) -Ihost
$(GEN)/stubs.o: $(GEN)/calls.c host/stubs.h
	clang++ $(DXMT_CXXFLAGS) -O1 -g -Wall -Werror -c -o $@ $(GEN)/stubs.cpp
stubs: $(GEN)/stubs.o
	@echo "stubs: ok"
.PHONY: stubs

LOOP_SRC := $(addprefix $(WM)/, calls.c cmdlist.c marshal.c slots.c guest/ghandles.c guest/galloc.c guest/winemetal.c \
  host/htable.c host/invoke.m host/memory.m host/events.m host/present.m host/loader.c tests/loopback.c \
  tests/sequence.c tests/galloc-cases.c)
$(GEN)/roundtrip: tests/roundtrip.c $(GEN)/calls.c $(LOOP_SRC) $(WM)/calls.h $(WM)/marshal.c $(WM)/marshal.h
	$(CC) $(CFLAGS) $(INC) -o $@ tests/roundtrip.c $(GEN)/calls.c $(GEN)/roundtrip-cases.c $(LOOP_SRC) \
	  -framework AppKit -framework Metal -framework QuartzCore -lpthread

roundtrip: $(GEN)/roundtrip
	$(GEN)/roundtrip
.PHONY: roundtrip

# The host dispatcher on native DXMT (dispatch-test.c), beside Wine's winemetal.so as macneutron-vm runs them.
NATIVE_LIB := $(ROOT)/build/dxmt-native/install/lib
$(GEN)/native.o: host/native.cpp host/stubs.h $(GEN)/calls.c
	clang++ $(DXMT_CXXFLAGS) -O1 -g -Wall -Werror -fsanitize=address,undefined -c -o $@ host/native.cpp
$(GEN)/stubs-asan.o: $(GEN)/calls.c host/stubs.h
	clang++ $(DXMT_CXXFLAGS) -O1 -g -Wall -Werror -fsanitize=address,undefined -c -o $@ $(GEN)/stubs.cpp
$(GEN)/dispatch-test: tests/dispatch-test.c host/dispatch.c host/memory.c host/events.c host/dispatch.h mn_d3d.h $(GEN)/calls.c $(GEN)/native.o \
  $(GEN)/stubs-asan.o $(LOOP_SRC) $(WM)/host/dispatch.m $(WM)/marshal.c $(WM)/marshal.h
	$(CC) $(CFLAGS) $(INC) -I. -c -o $(GEN)/dispatch-test.o tests/dispatch-test.c
	$(CC) $(CFLAGS) $(INC) -I. -o $@ $(GEN)/dispatch-test.o host/dispatch.c host/memory.c host/events.c $(GEN)/calls.c $(GEN)/ifaces.c \
	  $(GEN)/native.o $(GEN)/stubs-asan.o $(LOOP_SRC) $(WM)/host/dispatch.m $(ROOT)/eac-vm/metal/host/testops.c \
	  -framework AppKit -framework Metal -framework QuartzCore -lpthread -lc++

dispatch-test: $(GEN)/dispatch-test
	make -s -C $(WM) metal-host
	DXMT_LOG_PATH=none $(GEN)/dispatch-test $(W)/metal-host $(NATIVE_LIB)
.PHONY: dispatch-test

test: layout roundtrip
	python3 -W error::ResourceWarning -m unittest discover -s tests -p 'test_*.py'
.PHONY: test

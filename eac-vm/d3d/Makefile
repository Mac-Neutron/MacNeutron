# Direct3D forwarding's generated tables and their tests (M2); outputs go to build/eac-vm-work/d3d-gen.
ROOT := $(abspath ../..)
W := $(ROOT)/build/eac-vm-work
GEN := $(W)/d3d-gen
I := $(ROOT)/build/dxmt-native/install/include-native
NT := $(ROOT)/eac-vm/dxmt-native/tests
WM := $(ROOT)/eac-vm/metal/wm
DXMT := $(ROOT)/build/wine-arm64-src/dxmt/src
CFLAGS ?= -O1 -g -Wall -Wextra -std=gnu2x -fsanitize=address,undefined
INC := -DDXMT_GUEST_VM -I$(DXMT)/winemetal -I$(DXMT)/airconv -I$(ROOT)/eac-vm/metal -I$(WM) -I$(WM)/tests -I$(GEN)

$(GEN)/calls.c: gen.py rules.txt
	python3 -W error::ResourceWarning gen.py --headers $(I) --rules rules.txt --out $(GEN) calls

# The guest's layouts the generator computed against the native headers': it compiles, or a size or offset differs.
layout: $(GEN)/calls.c
	clang++ -std=c++17 -fsyntax-only -include $(NT)/native-prelude.h -I$(NT)/include -I$(I)/d3d12 -I$(I)/windows \
	  -I$(I)/directx -I$(I) $(GEN)/layout.cpp
	@echo "layout: ok"
.PHONY: layout

LOOP_SRC := $(addprefix $(WM)/, calls.c cmdlist.c marshal.c slots.c guest/ghandles.c guest/galloc.c guest/winemetal.c \
  host/htable.c host/invoke.m host/memory.m host/events.m host/present.m host/loader.c tests/loopback.c \
  tests/sequence.c tests/galloc-cases.c)
$(GEN)/roundtrip: tests/roundtrip.c $(GEN)/calls.c $(LOOP_SRC) $(WM)/calls.h $(WM)/marshal.c $(WM)/marshal.h
	$(CC) $(CFLAGS) $(INC) -o $@ tests/roundtrip.c $(GEN)/calls.c $(GEN)/roundtrip-cases.c $(LOOP_SRC) \
	  -framework AppKit -framework Metal -framework QuartzCore -lpthread

roundtrip: $(GEN)/roundtrip
	$(GEN)/roundtrip
.PHONY: roundtrip

test: layout roundtrip
	python3 -W error::ResourceWarning -m unittest discover -s tests -p 'test_*.py'
.PHONY: test

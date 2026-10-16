# Host-side (macOS) builds of the Steam forwarding pieces; outputs go to build/eac-vm-work/steam-forward.
OUT ?= ../../build/eac-vm-work/steam-forward
CFLAGS ?= -O2 -g -Wall -Wextra -Wno-missing-field-initializers -std=c11

$(OUT)/marshal-test: tests/marshal-test.c marshal.c marshal.h wire.h
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/marshal-test.c marshal.c

marshal-test: $(OUT)/marshal-test
	$(OUT)/marshal-test
.PHONY: marshal-test

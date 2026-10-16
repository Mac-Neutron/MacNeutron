# Mac-side tests of the Metal transport; outputs go to build/eac-vm-work/metal.
OUT ?= ../../build/eac-vm-work/metal
CFLAGS ?= -O1 -g -Wall -Wextra -std=c11 -fsanitize=address,undefined

$(OUT)/ring-test: tests/ring-test.c ring.c ring.h proto.h
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -o $@ tests/ring-test.c ring.c -lpthread

ring-test: $(OUT)/ring-test
	$(OUT)/ring-test
.PHONY: ring-test

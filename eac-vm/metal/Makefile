# Mac-side tests of the Metal transport; outputs go to build/eac-vm-work/metal.
OUT ?= $(or $(BUILD_DIR),../../build)/eac-vm-work/metal
CFLAGS ?= -O1 -g -Wall -Wextra -std=c11 -fsanitize=address,undefined

$(OUT)/ring-test: tests/ring-test.c ring.c ring.h proto.h
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -o $@ tests/ring-test.c ring.c -lpthread

ring-test: $(OUT)/ring-test
	$(OUT)/ring-test
.PHONY: ring-test

$(OUT)/executor-test: tests/executor-test.c host/executor.c host/executor.h ring.c ring.h proto.h
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -o $@ tests/executor-test.c host/executor.c ring.c -lpthread

executor-test: $(OUT)/executor-test
	$(OUT)/executor-test
.PHONY: executor-test

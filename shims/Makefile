CC = /opt/toolchains/dc/sh-elf/bin/sh-elf-gcc
NM = /opt/toolchains/dc/sh-elf/bin/sh-elf-nm
OBJCOPY = /opt/toolchains/dc/sh-elf/bin/sh-elf-objcopy
CFLAGS = -ml -m4-single-only -ffreestanding -nostdlib -Os -Wall -Iinclude
SRCS = src/main.c src/scif.c src/util.c src/gd.c src/cart.c
B = build

.PHONY: all test clean
all: $(B)/shim.bin $(B)/shim.map

test:
	mkdir -p $(B)
	cc -DHOST_TEST -Iinclude -o $(B)/test_host test/test_host.c && $(B)/test_host
$(B)/shim.elf: $(SRCS) shim.ld include/shim_iface.h
	mkdir -p $(B)
	$(CC) $(CFLAGS) -Wl,-T,shim.ld -Wl,-Map,$(B)/shim.map.tmp -o $@ $(SRCS)
$(B)/shim.bin: $(B)/shim.elf
	$(OBJCOPY) -O binary $< $@
$(B)/shim.map: $(B)/shim.elf
	$(NM) $< > $@
clean:
	rm -rf $(B)

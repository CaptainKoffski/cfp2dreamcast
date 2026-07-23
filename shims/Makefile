CC = /opt/toolchains/dc/sh-elf/bin/sh-elf-gcc
NM = /opt/toolchains/dc/sh-elf/bin/sh-elf-nm
OBJCOPY = /opt/toolchains/dc/sh-elf/bin/sh-elf-objcopy
CFLAGS = -ml -m4-single-only -ffreestanding -nostdlib -Os -Wall -Wextra -fno-tree-loop-distribute-patterns -Iinclude
B = build
# Embedded data: free-play EEPROM + verbatim MIE ACK templates + JVS I/O-board
# enumeration replies (mie_jvsXX, harvested by scripts/extract_jvs_replies.py).
# All gitignored (ROM-derived blobs).
DATA_SRCS = $(B)/eeprom_img.c $(B)/mie_sub01.c $(B)/mie_sub13.c $(B)/mie_sub17.c \
            $(B)/mie_sub27.c $(B)/mie_sub31.c $(B)/mie_subff.c \
            $(B)/mie_jvsf1.c $(B)/mie_jvs10.c $(B)/mie_jvs11.c \
            $(B)/mie_jvs12.c $(B)/mie_jvs13.c $(B)/mie_jvs14.c
SRCS = src/main.c src/scif.c src/util.c src/gd.c src/cart.c src/maple.c src/jvs.c $(DATA_SRCS)

.PHONY: all test clean
all: $(B)/shim.bin $(B)/shim.map

test:
	mkdir -p $(B)
	cc -DHOST_TEST -Iinclude -o $(B)/test_host test/test_host.c && $(B)/test_host
	cc -Iinclude -o $(B)/test_iface test/test_shim_iface.c && $(B)/test_iface
$(B)/shim.elf: $(SRCS) shim.ld include/shim_iface.h
	mkdir -p $(B)
	$(CC) $(CFLAGS) -Wl,-T,shim.ld -Wl,-Map,$(B)/shim.map.tmp -o $@ $(SRCS)
$(B)/shim.bin: $(B)/shim.elf
	$(OBJCOPY) -O binary $< $@
$(B)/shim.map: $(B)/shim.elf
	$(NM) $< > $@
# xxd -n pins the array symbol to NAME / NAME_len (matches externs in main.c);
# sed makes both const so they land in .rodata (not .data) and match the externs.
$(B)/eeprom_img.c: data/eeprom.bin
	mkdir -p $(B)
	xxd -n eeprom_img -i $< | sed 's/^unsigned/const unsigned/' > $@
$(B)/mie_%.c: ../build/mie_%.bin
	mkdir -p $(B)
	xxd -n mie_$* -i $< | sed 's/^unsigned/const unsigned/' > $@
clean:
	rm -rf $(B)

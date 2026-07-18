# Requires: source tools/kos/environ.sh
TARGET = loader.elf
OBJS = main.o handoff.o shim_blob.o
CFLAGS += -I../shims/include -I../build
all: ../build/1ST_READ.BIN
include $(KOS_BASE)/Makefile.rules
$(TARGET): $(OBJS)
	kos-cc -o $(TARGET) $(OBJS)
# shim.bin embedded as _shim_bin / _shim_bin_end (C's leading-underscore prefix;
# raw objcopy names verified with sh-elf-nm against the ../shims/build/shim.bin path).
shim_blob.o: ../shims/build/shim.bin
	sh-elf-objcopy -I binary -O elf32-shl -B sh4 \
	  --redefine-sym _binary____shims_build_shim_bin_start=_shim_bin \
	  --redefine-sym _binary____shims_build_shim_bin_end=_shim_bin_end \
	  $< $@
../build/1ST_READ.BIN: $(TARGET)
	mkdir -p ../build
	sh-elf-objcopy -R .stack -O binary $(TARGET) $@
clean:
	rm -f $(TARGET) $(OBJS) ../build/1ST_READ.BIN

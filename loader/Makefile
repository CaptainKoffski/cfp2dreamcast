# Requires: source tools/kos/environ.sh
TARGET = loader.elf
OBJS = main.o
all: ../build/1ST_READ.BIN
include $(KOS_BASE)/Makefile.rules
$(TARGET): $(OBJS)
	kos-cc -o $(TARGET) $(OBJS)
../build/1ST_READ.BIN: $(TARGET)
	mkdir -p ../build
	sh-elf-objcopy -R .stack -O binary $(TARGET) $@
clean:
	rm -f $(TARGET) $(OBJS) ../build/1ST_READ.BIN

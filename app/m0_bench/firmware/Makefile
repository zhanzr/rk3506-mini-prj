# M0 benchmark firmware - minimal bare-metal build (SDK prebuilt toolchain)
TC      ?= $(HOME)/sdk/prebuilts/gcc/linux-x86/arm/gcc-arm-none-eabi-10-2020-q4-major-x86_64-linux
CC      := $(TC)/bin/arm-none-eabi-gcc
OBJCOPY := $(TC)/bin/arm-none-eabi-objcopy
SIZE    := $(TC)/bin/arm-none-eabi-size

COREMARK_BUILD ?= 0
CFLAGS  := -mcpu=cortex-m0 -mthumb -Os -ffreestanding -nostdlib \
           -fno-builtin -Wall -Wextra -I. -Icoremark \
           -DCOREMARK_BUILD=$(COREMARK_BUILD) -DPERFORMANCE_RUN=1

all: m0.bin

m0.elf: main.c lnk.ld
	$(CC) $(CFLAGS) -T lnk.ld -nostartfiles -Wl,--gc-sections \
		-o $@ main.c -lgcc
	$(SIZE) $@

m0.bin: m0.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f m0.elf m0.bin

.PHONY: all clean

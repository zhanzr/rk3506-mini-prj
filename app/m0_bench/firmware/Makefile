# RK3506 M0 bare-metal firmware
TC      ?= $(HOME)/sdk/prebuilts/gcc/linux-x86/arm/gcc-arm-none-eabi-10-2020-q4-major-x86_64-linux
CC      := $(TC)/bin/arm-none-eabi-gcc
OBJCOPY := $(TC)/bin/arm-none-eabi-objcopy
SIZE    := $(TC)/bin/arm-none-eabi-size

COREMARK_ITERATIONS ?= 10000
COREMARK_GPIO_BIT_DELAY_LOOPS ?= 2000000
CFLAGS  := -mcpu=cortex-m0 -mthumb -Os -ffreestanding -nostdlib \
           -fno-builtin -ffunction-sections -fdata-sections \
           -fno-unwind-tables -fno-asynchronous-unwind-tables \
           -Wall -Wextra -I. -Icoremark
COREMARK_CFLAGS := $(CFLAGS) -O2 -flto -DCOREMARK_EXTERNAL_TIMING=1 \
                   -DPERFORMANCE_RUN=1 -DITERATIONS=$(COREMARK_ITERATIONS) \
                   -DCOREMARK_GPIO_BIT_DELAY_LOOPS=$(COREMARK_GPIO_BIT_DELAY_LOOPS)
LDFLAGS := -T lnk.ld -nostartfiles -Wl,--gc-sections \
           -Wl,--build-id=none

COREMARK_SOURCES := startup.c coremark_main.c core_portme.c \
                    coremark/core_main.c coremark/core_list_join.c \
                    coremark/core_matrix.c coremark/core_state.c \
                    coremark/core_util.c

all: m0.bin m0-coremark.bin

m0.elf: startup.c main.c lnk.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ startup.c main.c -lgcc
	$(SIZE) $@

m0.bin: m0.elf
	$(OBJCOPY) -O binary $< $@

m0-coremark.elf: $(COREMARK_SOURCES) core_portme.h coremark_report.h \
                 coremark/coremark.h coremark/coremark_config.h lnk.ld
	$(CC) $(COREMARK_CFLAGS) $(LDFLAGS) -o $@ $(COREMARK_SOURCES) -lgcc
	$(SIZE) $@

m0-coremark.bin: m0-coremark.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f m0.elf m0.bin m0-coremark.elf m0-coremark.bin

.PHONY: all clean

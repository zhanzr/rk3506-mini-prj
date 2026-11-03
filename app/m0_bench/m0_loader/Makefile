obj-m := rk3506_m0_loader.o

TC  ?= $(HOME)/sdk/prebuilts/gcc/linux-x86/arm/gcc-arm-10.3-2021.07-x86_64-arm-none-linux-gnueabihf
KDIR ?= $(HOME)/sdk/kernel-6.1

all: rk3506_m0_loader.ko

rk3506_m0_loader.ko: rk3506_m0_loader.c
	$(MAKE) ARCH=arm CROSS_COMPILE=$(TC)/bin/arm-none-linux-gnueabihf- -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean

.PHONY: all clean

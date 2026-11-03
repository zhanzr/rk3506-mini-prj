# A7 reader - buildroot toolchain
TC     ?= $(HOME)/toolchains/arm-buildroot-linux-gnueabihf_sdk-buildroot
CROSS  ?= $(TC)/bin/arm-linux-
BOARD  ?= rk3506-buildroot

all: m0_read

m0_read: m0_read.c
	$(CROSS)gcc -O2 -Wall -o $@ $<

push: m0_read
	scp -q $< $(BOARD):/root/

clean:
	rm -f m0_read

.PHONY: all push clean

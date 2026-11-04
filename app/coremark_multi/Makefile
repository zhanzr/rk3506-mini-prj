# coremark_multi - multi-core CoreMark verification for the RK3506-mini
TC       ?= $(HOME)/toolchains/arm-buildroot-linux-gnueabihf_sdk-buildroot
CROSS    ?= $(TC)/bin/arm-linux-
BOARD    ?= rk3506-buildroot

# A32 variant (proven fastest in app/coremark results)
COREMARK_ITERATIONS ?= 56000
CFLAGS  := -I. -Ilinux -O3 -funroll-loops -flto -marm \
           -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard \
           -DPERFORMANCE_RUN=1 -DSEED_METHOD=SEED_VOLATILE \
           -DMEM_METHOD=MEM_MALLOC -DITERATIONS=$(COREMARK_ITERATIONS) \
           -DCOMPILER_FLAGS="\"GCC12.4 -O3 -flto -marm cortex-a7\"" \
           -DMEM_LOCATION="\"DDR_HEAP\"" \
           -Wall
CORE_SRCS := core_list_join.c core_main.c core_matrix.c \
             core_state.c core_util.c linux/core_portme.c

all: coremark_multi

core_main.o: core_main.c coremark.h linux/core_portme.h
	$(CROSS)gcc $(CFLAGS) -Dmain=coremark_entry -c $< -o $@

linux_portme.o: linux/core_portme.c linux/core_portme.h coremark.h
	$(CROSS)gcc $(CFLAGS) -c $< -o $@

%.o: %.c coremark.h
	$(CROSS)gcc $(CFLAGS) -c $< -o $@

coremark_multi: coremark_multi.o core_main.o core_list_join.o \
                core_matrix.o core_state.o core_util.o linux_portme.o
	$(CROSS)gcc $(CFLAGS) -o $@ $^ -lrt

push: coremark_multi
	scp -q $< $(BOARD):/root/coremark_multi/

clean:
	rm -f *.o coremark_multi

.PHONY: all push clean

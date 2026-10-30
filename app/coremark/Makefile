TARGETS  := coremark-arm coremark-thumb
TC       ?= $(HOME)/toolchains/arm-buildroot-linux-gnueabihf_sdk-buildroot
CROSS    ?= $(TC)/bin/arm-linux-
BOARD    ?= rk3506-buildroot
BOARD_DIR ?= coremark
ITERATIONS ?= 60000
SOURCES  := core_list_join.c core_main.c core_matrix.c core_state.c \
            core_util.c linux/core_portme.c linux/core_portme.h

FLAGS := -O3 -funroll-loops -flto \
         -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard \
         -I. -Ilinux \
         -DFLAGS_STR=\"\" \
         -DSEED_METHOD=SEED_VOLATILE -DMEM_METHOD=MEM_MALLOC \
         -DPERFORMANCE_RUN=1 -DITERATIONS=$(ITERATIONS)

all: $(TARGETS)

coremark-arm: $(SOURCES)
	$(CROSS)gcc $(FLAGS) -marm $(SOURCES) -o $@

coremark-thumb: $(SOURCES)
	$(CROSS)gcc $(FLAGS) -mthumb $(SOURCES) -o $@

push: all
	ssh $(BOARD) "mkdir -p $(BOARD_DIR)"
	scp -q $(TARGETS) $(BOARD):$(BOARD_DIR)/
	ssh $(BOARD) "chmod 755 $(BOARD_DIR)/$(TARGETS)"

run:
	ssh -t $(BOARD) "cd $(BOARD_DIR) && taskset -c 0 ./coremark-arm && taskset -c 0 ./coremark-thumb"

size: $(TARGETS)
	$(CROSS)size $(TARGETS)

clean:
	rm -f $(TARGETS)

.PHONY: all push run size clean

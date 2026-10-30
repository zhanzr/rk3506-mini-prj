TARGET    := st7789_demo
TC        ?= $(HOME)/toolchains/arm-buildroot-linux-gnueabihf_sdk-buildroot
CROSS     ?= $(TC)/bin/arm-linux-
CROSS_FLAGS := -O2 -Wall -Wextra -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard
BOARD     ?= rk3506-buildroot
BOARD_DIR ?= st7789_md169_240x280_spi
SOURCES   := main.c lcd.c gpio_bb.c lcd.h font_1608.h gpio_bb.h Makefile README.md

all: $(TARGET)

$(TARGET): main.c lcd.c gpio_bb.c font_1608.c gpio_bb.h lcd.h font_1608.h
	$(CROSS)gcc $(CROSS_FLAGS) -o $@ main.c lcd.c gpio_bb.c font_1608.c

push: $(TARGET)
	ssh $(BOARD) "mkdir -p $(BOARD_DIR)"
	scp -q $(TARGET) $(BOARD):$(BOARD_DIR)/

run:
	ssh -t $(BOARD) "cd $(BOARD_DIR) && ./$(TARGET)"

matrix:
	ssh -t $(BOARD) "cd $(BOARD_DIR) && for cs in 7 48; do for ex in '' -f; do for sw in '' -S; do echo \"=== CS=$$cs extra='\$$ex \$$sw' ===\"; timeout 30 ./st7789_demo -c $$cs -1 $$ex $$sw; sleep 1; done; done; done"

clean:
	rm -f $(TARGET)

.PHONY: all push run matrix clean

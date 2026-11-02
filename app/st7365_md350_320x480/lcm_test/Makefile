TARGET    := lcm_test
TC        ?= $(HOME)/toolchains/arm-buildroot-linux-gnueabihf_sdk-buildroot
CROSS     ?= $(TC)/bin/arm-linux-
CROSS_FLAGS := -O2 -Wall -Wextra -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard
BOARD     ?= rk3506-buildroot
BOARD_DIR ?= st7365_md350_320x480/lcm_test

all: $(TARGET)

$(TARGET): main.c lcd.c spi_hw.c ref/lcd_fonts.c ref/lcd_font_1608.c spi_hw.h lcd.h ref/lcd_fonts.h ref/lcd_font_1608.h logo64.h
	$(CROSS)gcc $(CROSS_FLAGS) -o $@ main.c lcd.c spi_hw.c ref/lcd_fonts.c ref/lcd_font_1608.c -lm

push: $(TARGET)
	ssh $(BOARD) "mkdir -p $(BOARD_DIR)"
	scp -q $(TARGET) $(BOARD):$(BOARD_DIR)/

run:
	ssh -t $(BOARD) "cd $(BOARD_DIR) && ./$(TARGET)"

clean:
	rm -f $(TARGET)

.PHONY: all push run clean

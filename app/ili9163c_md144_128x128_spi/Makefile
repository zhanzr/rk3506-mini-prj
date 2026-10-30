TARGET    := ili9163c_demo
TC        ?= $(HOME)/toolchains/arm-buildroot-linux-gnueabihf_sdk-buildroot
CROSS     ?= $(TC)/bin/arm-linux-
CROSS_FLAGS := -O2 -Wall -Wextra -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard
LDLIBS     := -lm
BOARD     ?= rk3506-buildroot
BOARD_DIR ?= ili9163c_md144_128x128_spi
SOURCES   := main.c lcd.c spi_hw.c ref/lcd_font_1608.c ref/lcd_fonts.c spi_hw.h lcd.h ref/lcd_font_1608.h ref/lcd_fonts.h Makefile README.md

all: $(TARGET)

$(TARGET): main.c lcd.c spi_hw.c ref/lcd_font_1608.c ref/lcd_fonts.c spi_hw.h lcd.h ref/lcd_font_1608.h ref/lcd_fonts.h
	$(CROSS)gcc $(CROSS_FLAGS) -o $@ main.c lcd.c spi_hw.c ref/lcd_font_1608.c ref/lcd_fonts.c -lm

push: $(TARGET)
	ssh $(BOARD) "mkdir -p $(BOARD_DIR)"
	scp -q $(TARGET) $(BOARD):$(BOARD_DIR)/

run:
	ssh -t $(BOARD) "cd $(BOARD_DIR) && ./$(TARGET)"

clean:
	rm -f $(TARGET)

.PHONY: all push run clean

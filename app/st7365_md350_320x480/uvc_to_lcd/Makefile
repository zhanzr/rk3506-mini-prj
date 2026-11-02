TARGET    := uvc_to_lcd
TC        ?= $(HOME)/toolchains/arm-buildroot-linux-gnueabihf_sdk-buildroot
CROSS     ?= $(TC)/bin/arm-linux-
CROSS_FLAGS := -O2 -Wall -Wextra -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -Iinclude -Ivendor
BOARD     ?= rk3506-buildroot
BOARD_DIR ?= st7365_md350_320x480/uvc_to_lcd
# vendored libjpeg 8.2.2 (IJG, ARM hard-float) - the stock board image
# has no libjpeg, so the app ships its own and finds it via LD_LIBRARY_PATH

all: $(TARGET)

$(TARGET): uvc_to_lcd.c lcd.c spi_hw.c ref/lcd_fonts.c ref/lcd_font_1608.c spi_hw.h lcd.h ref/lcd_fonts.h ref/lcd_font_1608.h vendor/libjpeg.so.8
	$(CROSS)gcc $(CROSS_FLAGS) -o $@ uvc_to_lcd.c lcd.c spi_hw.c ref/lcd_fonts.c ref/lcd_font_1608.c -Lvendor -l:libjpeg.so.8 -lm

push: $(TARGET)
	ssh $(BOARD) "mkdir -p $(BOARD_DIR)/vendor"
	scp -q $(TARGET) $(BOARD):$(BOARD_DIR)/
	scp -q vendor/libjpeg.so.8.2.2 $(BOARD):$(BOARD_DIR)/vendor/

run:
	ssh -t $(BOARD) "cd $(BOARD_DIR) && LD_LIBRARY_PATH=$$PWD/vendor ./$(TARGET)"

clean:
	rm -f $(TARGET)

.PHONY: all push run clean

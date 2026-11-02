TC        ?= $(HOME)/toolchains/arm-buildroot-linux-gnueabihf_sdk-buildroot
CROSS     ?= $(TC)/bin/arm-linux-
CXX       ?= $(TC)/bin/arm-linux-g++
BOARD     ?= rk3506-buildroot
BOARD_DIR ?= st7365_md350_320x480/whiteband_test
CROSS_FLAGS := -O2 -Wall -Wextra -march=armv7-a+neon-vfpv4 -mfloat-abi=hard -Iref
TARGET     := whiteband_test

all: $(TARGET)

$(TARGET): whiteband_test.c lcd.c spi_hw.c ref/lcd_fonts.c ref/lcd_font_1608.c
	$(CROSS)gcc $(CROSS_FLAGS) -o $@ whiteband_test.c lcd.c spi_hw.c \
		ref/lcd_fonts.c ref/lcd_font_1608.c -lm

push: $(TARGET)
	ssh $(BOARD) "mkdir -p $(BOARD_DIR)"
	scp -q $(TARGET) $(BOARD):$(BOARD_DIR)/
	run:
	ssh -t $(BOARD) "cd $(BOARD_DIR) && ./$(TARGET)"

clean:
	rm -f $(TARGET) *.o ref/*.o

.PHONY: all push run clean

# qr_scan - builds NATIVELY on the board (needs libzbar-dev);
# cross-build also possible if an armhf libzbar sysroot exists.
TARGET  := qr_scan
CROSS   ?=
CFLAGS  := -O2 -Wall -Wextra -Iref
LDLIBS  := -lzbar -lm -lpthread

SRCS := qr_scan.c lcd.c spi_hw.c ref/lcd_fonts.c ref/lcd_font_1608.c

all: $(TARGET)

$(TARGET): $(SRCS) lcd.h spi_hw.h ref/lcd_fonts.h ref/lcd_font_1608.h
	$(CROSS)gcc $(CFLAGS) -o $@ $(SRCS) $(LDLIBS)

push-src: $(SRCS) lcd.h spi_hw.h ref/lcd_fonts.h ref/lcd_font_1608.h Makefile README.md
	ssh $(BOARD) "mkdir -p $(BOARD_DIR)"
	rsync -az qr_scan.c lcd.c lcd.h spi_hw.c spi_hw.h Makefile README.md $(BOARD):$(BOARD_DIR)/
	rsync -az ref $(BOARD):$(BOARD_DIR)/

board: push-src
	ssh $(BOARD) "cd $(BOARD_DIR) && make && chmod 755 $(TARGET)"

run:
	ssh $(BOARD) "cd $(BOARD_DIR) && ./$(TARGET)"

clean:
	rm -f $(TARGET) *.o

BOARD ?= opz
BOARD_DIR ?= st7365_md350_320x480/qr_scan
.PHONY: all push-src board run clean

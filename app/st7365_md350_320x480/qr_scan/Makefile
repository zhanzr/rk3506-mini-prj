# qr_scan - QR/barcode reader (vendored libzbar, cross-built from
# mchehab/zbar 0.23.92: image scanning only, no video/X/GTK/Qt).
TARGET    := qr_scan
TC        ?= $(HOME)/toolchains/arm-buildroot-linux-gnueabihf_sdk-buildroot
CROSS     ?= $(TC)/bin/arm-linux-
CROSS_FLAGS := -O2 -Wall -Wextra -march=armv7-a+neon-vfpv4 -mfloat-abi=hard -Iref -Ivendor/zbar_inc
RPATH      := -Wl,-rpath,'$$ORIGIN'/vendor
BOARD     ?= rk3506-buildroot
BOARD_DIR ?= st7365_md350_320x480/qr_scan
LDLIBS    := $(RPATH) -Lvendor -l:libzbar.so.0.3.0 -lm -lpthread

all: $(TARGET)

$(TARGET): qr_scan.c lcd.c spi_hw.c ref/lcd_fonts.c ref/lcd_font_1608.c vendor/libzbar.so.0.3.0
	$(CROSS)gcc $(CROSS_FLAGS) -o $@ qr_scan.c lcd.c spi_hw.c \
		ref/lcd_fonts.c ref/lcd_font_1608.c $(LDLIBS)

push: $(TARGET)
	ssh $(BOARD) "mkdir -p $(BOARD_DIR)/vendor"
	scp -q $(TARGET) $(BOARD):$(BOARD_DIR)/
	scp -q vendor/libzbar.so.0.3.0 $(BOARD):$(BOARD_DIR)/vendor/

run:
	ssh -t $(BOARD) "cd $(BOARD_DIR) && LD_LIBRARY_PATH=\$$PWD/vendor ./$(TARGET)"

clean:
	rm -f $(TARGET) *.o ref/*.o

.PHONY: all push run clean

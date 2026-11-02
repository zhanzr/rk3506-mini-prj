# ped_detect - builds NATIVELY on the board (needs libopencv-dev + g++);
# OpenCV HOG people detector (pre-trained, no model download).
TARGET  := ped_detect
CXX     ?= g++
CC      ?= gcc
CFLAGS  := -O2 -Wall -Iref -I/usr/include/opencv4
CXXFLAGS := -O2 -Wall -Iref -I/usr/include/opencv4
LIBS    := $(shell PKG_CONFIG_PATH=/usr/lib/arm-linux-gnueabihf/pkgconfig pkg-config --libs opencv4) -lpthread

all: $(TARGET)

ped_detect.o: ped_detect.cpp lcd.h spi_hw.h ref/lcd_fonts.h ref/lcd_font_1608.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

lcd.o: lcd.c lcd.h
	$(CC) $(CFLAGS) -c $< -o $@

spi_hw.o: spi_hw.c spi_hw.h
	$(CC) $(CFLAGS) -c $< -o $@

ref/lcd_fonts.o: ref/lcd_fonts.c ref/lcd_fonts.h
	$(CC) $(CFLAGS) -c $< -o $@

ref/lcd_font_1608.o: ref/lcd_font_1608.c ref/lcd_font_1608.h
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): ped_detect.o lcd.o spi_hw.o ref/lcd_fonts.o ref/lcd_font_1608.o
	$(CXX) $(CXXFLAGS) -o $@ ped_detect.o lcd.o spi_hw.o \
		ref/lcd_fonts.o ref/lcd_font_1608.o $(LIBS)

push-src: ped_detect.cpp lcd.c lcd.h spi_hw.c spi_hw.h Makefile README.md ref/lcd_fonts.c ref/lcd_fonts.h ref/lcd_font_1608.c ref/lcd_font_1608.h
	ssh $(BOARD) "mkdir -p $(BOARD_DIR)"
	rsync -az ped_detect.cpp lcd.c lcd.h spi_hw.c spi_hw.h Makefile README.md $(BOARD):$(BOARD_DIR)/
	rsync -az ref $(BOARD):$(BOARD_DIR)/

board: push-src
	ssh $(BOARD) "cd $(BOARD_DIR) && make && chmod 755 $(TARGET)"

run:
	ssh $(BOARD) "cd $(BOARD_DIR) && ./$(TARGET)"

clean:
	rm -f $(TARGET) *.o ref/*.o

BOARD ?= opz
BOARD_DIR ?= st7365_md350_320x480/ped_detect
.PHONY: all push-src board run clean

TC        ?= $(HOME)/toolchains/arm-buildroot-linux-gnueabihf_sdk-buildroot
CROSS     ?= $(TC)/bin/arm-linux-
CXX       = $(TC)/bin/arm-linux-g++
BOARD     ?= rk3506-buildroot
BOARD_DIR ?= st7365_md350_320x480/person_vis
CROSS_FLAGS := -O2 -Wall -Wextra -march=armv7-a+neon-vfpv4 -mfloat-abi=hard \
               -Iinclude -I. -Iref -I../../vendor/tflm/inc
TARGET     := person_vis

all: $(TARGET)

$(TARGET): person_vis.cpp model_settings.cc model_settings.h lcd.c spi_hw.c ref/lcd_fonts.c ref/lcd_font_1608.c ../../vendor/tflm/lib/libtensorflow-microlite.a
	$(CROSS)gcc $(CROSS_FLAGS) -c lcd.c -o lcd.o
	$(CROSS)gcc $(CROSS_FLAGS) -c spi_hw.c -o spi_hw.o
	$(CROSS)gcc $(CROSS_FLAGS) -c ref/lcd_fonts.c -o ref/lcd_fonts.o
	$(CROSS)gcc $(CROSS_FLAGS) -c ref/lcd_font_1608.c -o ref/lcd_font_1608.o
	$(CXX) $(CROSS_FLAGS) -c model_settings.cc -o model_settings.o
	$(CXX) $(CROSS_FLAGS) -c person_vis.cpp -o person_vis.o
	$(CXX) $(CROSS_FLAGS) -o $@ person_vis.o model_settings.o lcd.o spi_hw.o \
		ref/lcd_fonts.o ref/lcd_font_1608.o ../../vendor/tflm/lib/libtensorflow-microlite.a \
		-lm -lpthread

push: $(TARGET)
	ssh $(BOARD) "mkdir -p $(BOARD_DIR)"
	scp -q $(TARGET) $(BOARD):$(BOARD_DIR)/
	scp -q person_detect.tflite $(BOARD):$(BOARD_DIR)/
	run:
	ssh -t $(BOARD) "cd $(BOARD_DIR) && ./$(TARGET)"

clean:
	rm -f $(TARGET) *.o ref/*.o

.PHONY: all push run clean

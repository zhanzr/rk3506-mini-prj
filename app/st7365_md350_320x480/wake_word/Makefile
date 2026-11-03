TC        ?= $(HOME)/toolchains/arm-buildroot-linux-gnueabihf_sdk-buildroot
CROSS     ?= $(TC)/bin/arm-linux-
CXX       = $(TC)/bin/arm-linux-g++
BOARD     ?= rk3506-buildroot
BOARD_DIR ?= st7365_md350_320x480/wake_word
RPATH      := -Wl,-rpath,'$$ORIGIN'/vendor
CROSS_FLAGS := -O2 -Wall -Wextra -march=armv7-a+neon-vfpv4 -mfloat-abi=hard \
               -Iinclude -I. -Iref -I../../vendor/tflm/inc
TARGET     := wake_word

all: $(TARGET)

$(TARGET): wake_word.cpp lcd.c spi_hw.c ref/lcd_fonts.c ref/lcd_font_1608.c ../../vendor/tflm/lib/libtensorflow-microlite.a vendor/libasound.so.2
	$(CROSS)gcc $(CROSS_FLAGS) -c lcd.c -o lcd.o
	$(CROSS)gcc $(CROSS_FLAGS) -c spi_hw.c -o spi_hw.o
	$(CROSS)gcc $(CROSS_FLAGS) -c ref/lcd_fonts.c -o ref/lcd_fonts.o
	$(CROSS)gcc $(CROSS_FLAGS) -c ref/lcd_font_1608.c -o ref/lcd_font_1608.o
	$(CXX) $(CROSS_FLAGS) -c wake_word.cpp -o wake_word.o
	$(CXX) $(CROSS_FLAGS) -o $@ wake_word.o lcd.o spi_hw.o \
		ref/lcd_fonts.o ref/lcd_font_1608.o ../../vendor/tflm/lib/libtensorflow-microlite.a \
		$(RPATH) -Lvendor -l:libasound.so.2 -lm -lpthread

push: $(TARGET)
	ssh $(BOARD) "mkdir -p $(BOARD_DIR)/vendor"
	scp -q $(TARGET) $(BOARD):$(BOARD_DIR)/
	scp -q vendor/libasound.so.2.0.0 $(BOARD):$(BOARD_DIR)/vendor/
	scp -q audio_preprocessor_int8.tflite micro_speech_quantized.tflite $(BOARD):$(BOARD_DIR)/
	run:
	ssh -t $(BOARD) "cd $(BOARD_DIR) && LD_LIBRARY_PATH=$$PWD/vendor ./$(TARGET)"

clean:
	rm -f $(TARGET) *.o ref/*.o

.PHONY: all push run clean

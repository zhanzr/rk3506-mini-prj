TC        ?= $(HOME)/toolchains/arm-buildroot-linux-gnueabihf_sdk-buildroot
CROSS     ?= $(TC)/bin/arm-linux-
CXX       = $(TC)/bin/arm-linux-g++
BOARD     ?= rk3506-buildroot
BOARD_DIR ?= st7365_md350_320x480/object_detect
CROSS_FLAGS := -O2 -Wall -Wextra -march=armv7-a+neon-vfpv4 -mfloat-abi=hard -Iinclude -Ivendor -Ivendor/ncnn/include -Ivendor/ncnn/include/ncnn
CROSS_FLAGS += -I../common
RPATH      := -Wl,-rpath,'$$ORIGIN'/vendor
TARGET     := object_detect


all: $(TARGET)

$(TARGET): object_detect.c lcd.c spi_hw.c ref/lcd_fonts.c ref/lcd_font_1608.c yolo_shim.cpp yolo_shim.h vendor/ncnn/lib/libncnn.a
	$(CROSS)gcc $(CROSS_FLAGS) -c object_detect.c -o object_detect.o
	$(CROSS)gcc $(CROSS_FLAGS) -c lcd.c -o lcd.o
	$(CROSS)gcc $(CROSS_FLAGS) -c spi_hw.c -o spi_hw.o
	$(CROSS)gcc $(CROSS_FLAGS) -c ref/lcd_fonts.c -o ref/lcd_fonts.o
	$(CROSS)gcc $(CROSS_FLAGS) -c ref/lcd_font_1608.c -o ref/lcd_font_1608.o
	$(CXX) -O2 $(CROSS_FLAGS) -c yolo_shim.cpp -o yolo_shim.o
	$(CXX) $(CROSS_FLAGS) -o $@ object_detect.o lcd.o spi_hw.o \
		ref/lcd_fonts.o ref/lcd_font_1608.o yolo_shim.o \
		vendor/ncnn/lib/libncnn.a \
		$(RPATH) -Lvendor -l:libgomp.so.1 -l:libjpeg.so.8 ../common/log.o -lm -lpthread

push: $(TARGET)
	ssh $(BOARD) "mkdir -p $(BOARD_DIR)/vendor"
	scp -q $(TARGET) $(BOARD):$(BOARD_DIR)/
	scp -q config.json $(BOARD):$(BOARD_DIR)/
	ssh $(BOARD) "mkdir -p $(BOARD_DIR)/vendor/ncnn_model"
	scp -q vendor/libjpeg.so.8.2.2 vendor/libgomp.so.1.0.0 $(BOARD):$(BOARD_DIR)/vendor/
	scp -q vendor/ncnn_model/yf-opt.param vendor/ncnn_model/yf-opt.bin $(BOARD):$(BOARD_DIR)/vendor/ncnn_model/
	run:
	ssh -t $(BOARD) "cd $(BOARD_DIR) && LD_LIBRARY_PATH=$$PWD/vendor ./$(TARGET)"

clean:
	rm -f $(TARGET) *.o ref/*.o

.PHONY: all push run clean

../common/log.o: ../common/log.c ../common/log.h
	$(CROSS)gcc $(CROSS_FLAGS) -c $< -o $@

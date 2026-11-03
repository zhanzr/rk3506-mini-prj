TC        ?= $(HOME)/toolchains/arm-buildroot-linux-gnueabihf_sdk-buildroot
CROSS     ?= $(TC)/bin/arm-linux-
CXX       = $(TC)/bin/arm-linux-g++
BOARD     ?= rk3506-buildroot
BOARD_DIR ?= st7365_md350_320x480/face_detect
CROSS_FLAGS := -O2 -Wall -Wextra -march=armv7-a+neon-vfpv4 -mfloat-abi=hard -Iinclude -Ivendor
RPATH      := -Wl,-rpath,'$$ORIGIN'/vendor
TARGET     := face_detect
DETOBJ     := vendor/facedetect/facedetectcnn.o \
              vendor/facedetect/facedetectcnn-model.o \
              vendor/facedetect/facedetectcnn-data.o \
              det_shim.o

all: $(TARGET)

vendor/facedetect/%.o: vendor/facedetect/%.cpp
	$(CXX) -O3 $(CROSS_FLAGS) -D_ENABLE_NEON -c $< -o $@

det_shim.o: det_shim.cpp det_shim.h
	$(CXX) -O3 $(CROSS_FLAGS) -c $< -o $@

$(TARGET): face_detect.c lcd.c spi_hw.c ref/lcd_fonts.c ref/lcd_font_1608.c $(DETOBJ)
	$(CROSS)gcc $(CROSS_FLAGS) -c face_detect.c -o face_detect.o
	$(CROSS)gcc $(CROSS_FLAGS) -c lcd.c -o lcd.o
	$(CROSS)gcc $(CROSS_FLAGS) -c spi_hw.c -o spi_hw.o
	$(CROSS)gcc $(CROSS_FLAGS) -c ref/lcd_fonts.c -o ref/lcd_fonts.o
	$(CROSS)gcc $(CROSS_FLAGS) -c ref/lcd_font_1608.c -o ref/lcd_font_1608.o
	$(CXX) $(CROSS_FLAGS) -o $@ face_detect.o lcd.o spi_hw.o \
		ref/lcd_fonts.o ref/lcd_font_1608.o $(DETOBJ) \
		$(RPATH) -Lvendor -l:libjpeg.so.8 -lm -lpthread

push: $(TARGET)
	ssh $(BOARD) "mkdir -p $(BOARD_DIR)/vendor"
	scp -q $(TARGET) $(BOARD):$(BOARD_DIR)/
	scp -q vendor/libjpeg.so.8.2.2 $(BOARD):$(BOARD_DIR)/vendor/
	run:
	ssh -t $(BOARD) "cd $(BOARD_DIR) && LD_LIBRARY_PATH=$$PWD/vendor ./$(TARGET)"

clean:
	rm -f $(TARGET) *.o ref/*.o

.PHONY: all push run clean

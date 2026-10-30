TARGET    := ssd1317_demo
TC        ?= $(HOME)/toolchains/arm-buildroot-linux-gnueabihf_sdk-buildroot
CROSS     ?= $(TC)/bin/arm-linux-
CROSS_FLAGS := -O2 -Wall -Wextra -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard
BOARD     ?= rk3506-buildroot
BOARD_DIR ?= ssd1317_md066_64x48_iic
SOURCES   := main.c oled.c oledfont.c oled.h Makefile README.md

all: $(TARGET)

$(TARGET): main.c oled.c oledfont.c oled.h
	$(CROSS)gcc $(CROSS_FLAGS) -o $@ main.c oled.c oledfont.c

push: $(TARGET)
	ssh $(BOARD) "mkdir -p $(BOARD_DIR)"
	scp -q $(TARGET) $(BOARD):$(BOARD_DIR)/

run:
	ssh -t $(BOARD) "cd $(BOARD_DIR) && ./$(TARGET)"

deploy:
	ssh $(BOARD) "mkdir -p $(BOARD_DIR)"
	scp -q $(SOURCES) $(BOARD):$(BOARD_DIR)/

build-remote:
	ssh $(BOARD) "cd $(BOARD_DIR) && make CC=gcc"

clean:
	rm -f $(TARGET)

.PHONY: all push run deploy build-remote clean

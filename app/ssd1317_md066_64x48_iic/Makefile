TARGET     := ssd1317_demo
TARGET2    := lcm_logo
TC        ?= $(HOME)/toolchains/arm-buildroot-linux-gnueabihf_sdk-buildroot
CROSS     ?= $(TC)/bin/arm-linux-
CROSS_FLAGS := -O2 -Wall -Wextra -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard
BOARD     ?= rk3506-buildroot
BOARD_DIR ?= ssd1317_md066_64x48_iic
SOURCES   := main.c lcm_logo.c oled.c oledfont.c oled.h logo64.h Makefile README.md

all: $(TARGET) $(TARGET2)

$(TARGET): main.c oled.c oledfont.c oled.h
	$(CROSS)gcc $(CROSS_FLAGS) -o $@ main.c oled.c oledfont.c

$(TARGET2): lcm_logo.c oled.c oledfont.c oled.h logo64.h
	$(CROSS)gcc $(CROSS_FLAGS) -o $@ lcm_logo.c oled.c oledfont.c

push: $(TARGET) $(TARGET2)
	ssh $(BOARD) "mkdir -p $(BOARD_DIR)"
	scp -q $(TARGET) $(TARGET2) $(BOARD):$(BOARD_DIR)/

run: run-demo

run-demo:
	ssh -t $(BOARD) "cd $(BOARD_DIR) && ./$(TARGET)"

run-logo:
	ssh -t $(BOARD) "cd $(BOARD_DIR) && ./$(TARGET2)"

deploy:
	ssh $(BOARD) "mkdir -p $(BOARD_DIR)"
	scp -q $(SOURCES) $(BOARD):$(BOARD_DIR)/

build-remote:
	ssh $(BOARD) "cd $(BOARD_DIR) && make CC=gcc"

clean:
	rm -f $(TARGET) $(TARGET2)

.PHONY: all push run run-demo run-logo deploy build-remote clean

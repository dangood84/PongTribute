# Pong Tribute — Raylib window (C11)
#
# macOS:   make && make run          Apple Silicon, or Catalina once cmake is installed
# Linux:   make                      Raspberry Pi OS desktop (or: make linux)
# Windows: build-win.bat            or: make, if GNU make and CMake are on PATH
#
# CMake finds an installed Raylib, or downloads the pinned tag. This Makefile
# does not cross-compile: run it on the machine you want the binary for.

BUILD_DIR := build
BIN       := $(BUILD_DIR)/pong

ifeq ($(OS),Windows_NT)
  BIN := $(BUILD_DIR)/pong.exe
endif

CMAKE_ARGS ?=

.PHONY: all run linux windows clean

all:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release $(CMAKE_ARGS)
	cmake --build $(BUILD_DIR) --config Release --parallel
ifneq ($(OS),Windows_NT)
	ln -sfn $(BUILD_DIR)/compile_commands.json compile_commands.json
endif

run: all
	./$(BIN)

# Same build. Named so a Pi or a Windows prompt can say which machine it is.
linux: all
windows: all

clean:
	rm -rf $(BUILD_DIR) compile_commands.json

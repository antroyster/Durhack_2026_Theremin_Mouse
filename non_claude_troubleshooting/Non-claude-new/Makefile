# Builds ConsoleApplication1/ConsoleApplication1.cpp against the Pico's IMU reader (Linux only).
#   make          -> build/ConsoleApplication1
#   make run      -> build and run it (Pico plugged in)
#   make clean
#
# Dependencies, all inside the repo:
#   Eigen/                      (this folder)              headers only
#   ../../host/imu_reader.[ch]  open / read / close the port
#   ../../host/imu_parser.[ch]  split the byte stream into samples
#   ../../common/protocol.h     packet layout
#   ../../common/crc16.c        packet checksum
ROOT     = ../..
CXX     ?= g++
CC      ?= gcc
# The repo headers come first: the copies of imu_reader.h / imu_parser.h in this folder are out of step with the .c files.
CXXFLAGS = -std=c++17 -O2 -g -I$(ROOT)/host -I$(ROOT)/common -I.
CFLAGS   = -std=c11 -O2 -g -D_DEFAULT_SOURCE -I$(ROOT)/host -I$(ROOT)/common
CSRC     = $(ROOT)/host/imu_reader.c $(ROOT)/host/imu_parser.c $(ROOT)/common/crc16.c
OBJ     = $(patsubst %.c,build/%.o,$(notdir $(CSRC)))
vpath %.c $(ROOT)/host $(ROOT)/common

.PHONY: all run clean
all: build/ConsoleApplication1

build/%.o: %.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/ConsoleApplication1: ConsoleApplication1/ConsoleApplication1.cpp $(COBJ)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $< $(COBJ) -lm -o $@

run: build/ConsoleApplication1
	./build/ConsoleApplication1

clean:
	rm -rf build

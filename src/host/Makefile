# host/Makefile - builds host/build/imu_reader (POSIX + libc only).
CC      = gcc
CFLAGS  = -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -O2 -I../common
COMMON  = $(wildcard ../common/*.c)
SRC     = main.c imu_reader.c imu_parser.c
HDR     = imu_reader.h imu_parser.h $(wildcard ../common/*.h)

all: build/imu_reader

build/imu_reader: $(SRC) $(COMMON) $(HDR)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(SRC) $(COMMON) -lm

clean:
	rm -rf build

.PHONY: all clean

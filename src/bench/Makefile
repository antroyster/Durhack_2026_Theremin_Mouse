# bench/Makefile - builds bench/build/pico_sim (fake Pico on a pty) and
# bench/build/monitor (live terminal view). No external libraries.
CC      = gcc
CFLAGS  = -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -O2 -I../common -I../host
COMMON  = $(wildcard ../common/*.c)
HDR     = $(wildcard ../common/*.h) $(wildcard ../host/*.h)

all: build/pico_sim build/monitor

build/pico_sim: pico_sim.c $(COMMON) $(HDR)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ pico_sim.c $(COMMON) -lm

build/monitor: monitor.c ../host/imu_reader.c ../host/imu_parser.c $(COMMON) $(HDR)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ monitor.c ../host/imu_reader.c ../host/imu_parser.c $(COMMON) -lm

clean:
	rm -rf build

.PHONY: all clean

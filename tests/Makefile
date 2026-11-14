# L0 unit tests: pure logic on the PC, no hardware, no test framework.
#   make -C tests          build and run every test
#   make -C tests M=m1     run only the tests for milestone 1
#   make -C tests clean
CC     ?= gcc
CFLAGS  = -std=c11 -Wall -Wextra -Werror -O2 -I../common -I../host
# Every pure module, plus the reader's parser once it exists.
SRC     = $(wildcard ../common/*.c) $(wildcard ../host/imu_parser.c)
TESTS   = $(sort $(patsubst %.c,build/%,$(wildcard test_*.c)))
M      ?=

.PHONY: run clean
run: $(TESTS)
	@fail=0; for t in $(filter build/test_$(M)%,$(TESTS)); do \
		echo "== $$t"; ./$$t || fail=1; done; \
	if [ $$fail -eq 0 ]; then echo "L0: ALL PASS"; else echo "L0: FAILURES"; fi; exit $$fail

build/%: %.c $(SRC) check.h
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $< $(SRC) -lm

clean:
	rm -rf build

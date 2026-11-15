# theremin: 3D IMU view, table-mode cursor, notes, effects and gestures.
#   make            build/theremin
#   make test       run the motion maths and sound effects tests (no hardware needed)
#   make run        start it on the plugged-in Pico
# Needs: sudo apt install freeglut3-dev libsdl2-dev   (Eigen is in this folder)
ROOT     = ..
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -I. -I$(ROOT)/host -I$(ROOT)/common $(shell pkg-config --cflags sdl2) -DSDL_MAIN_HANDLED
CFLAGS   = -std=c11 -O2 -Wall -Wextra -D_DEFAULT_SOURCE -I$(ROOT)/host -I$(ROOT)/common
CSRC     = $(ROOT)/host/imu_reader.c $(ROOT)/host/imu_parser.c $(ROOT)/common/crc16.c
COBJ     = $(patsubst %.c,build/%.o,$(notdir $(CSRC)))
LIBS     = -lglut -lGLU -lGL $(shell pkg-config --libs sdl2) -lm
vpath %.c $(ROOT)/host $(ROOT)/common

.PHONY: all test run clean
all: build/theremin

build/%.o: %.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@
build/theremin: main.cpp motion.hpp fx.hpp $(COBJ)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) main.cpp $(COBJ) $(LIBS) -o $@
build/test_%: test_%.cpp motion.hpp fx.hpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $< -o $@

test: build/test_motion build/test_fx
	./build/test_motion
	./build/test_fx
run: build/theremin
	./build/theremin
clean:
	rm -rf build

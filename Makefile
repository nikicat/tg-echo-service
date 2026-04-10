BUILD_TYPE ?= Debug

# Extract -jN from MAKEFLAGS (set by make -jN), default to nproc
ifneq (,$(filter -j%, $(MAKEFLAGS)))
  CMAKE_BUILD_PARALLEL_LEVEL := $(patsubst -j%,%,$(filter -j%, $(MAKEFLAGS)))
else
  CMAKE_BUILD_PARALLEL_LEVEL := $(shell nproc)
endif
export CMAKE_BUILD_PARALLEL_LEVEL

.PHONY: all submodules tdlib configure build clean run prompt

all: build

submodules:
	git submodule update --init

# Build TDLib from vendor/td into td-install/
tdlib: td-install/lib/cmake/Td/TdConfig.cmake

td-install/lib/cmake/Td/TdConfig.cmake: submodules
	cmake -B vendor/td/build -S vendor/td -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
	cmake --build vendor/td/build -j$(CMAKE_BUILD_PARALLEL_LEVEL)
	cmake --install vendor/td/build --prefix "$(CURDIR)/td-install"

# Configure the main project
configure: build/Makefile

build/Makefile: CMakeLists.txt tdlib
	cmake -B build -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

# Build call_service binary
build: build/call_service

build/call_service: build/Makefile main.cpp
	cmake --build build -j$(CMAKE_BUILD_PARALLEL_LEVEL)

# Generate a test prompt (440Hz beep loop, 5 seconds)
prompt: prompt.raw

prompt.raw:
	ffmpeg -f lavfi -i "sine=frequency=440:duration=0.3" \
	  -f lavfi -i "anullsrc=r=48000:cl=stereo" \
	  -filter_complex "[0]aresample=48000,pan=stereo|c0=c0|c1=c0[beep];[1]atrim=0:0.7[silence];[beep][silence]concat=n=2:v=0:a=1,aloop=loop=4:size=48000[loop];[loop]aresample=48000" \
	  -t 5 -f s16le -acodec pcm_s16le -ac 2 -ar 48000 $@ -y

# Run the service (requires API_ID and API_HASH env vars)
run: build prompt
	mkdir -p recordings
	./build/call_service

clean:
	rm -rf build vendor/td/build

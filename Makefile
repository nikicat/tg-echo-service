BUILD_TYPE ?= Debug
BEEPS ?= 5
GLADOS_TEXT ?= Hello. Please leave your message after the beep.

# Extract -jN from MAKEFLAGS (set by make -jN), default to nproc
ifneq (,$(filter -j%, $(MAKEFLAGS)))
  CMAKE_BUILD_PARALLEL_LEVEL := $(patsubst -j%,%,$(filter -j%, $(MAKEFLAGS)))
else
  CMAKE_BUILD_PARALLEL_LEVEL := $(shell nproc)
endif
export CMAKE_BUILD_PARALLEL_LEVEL

IMAGE ?= docker.io/nikicat/tg-echo-service

.PHONY: all submodules tdlib configure build clean run prompt glados-prompt image push

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

# Generate a test prompt (440Hz beeps, MP3). Override count: make prompt BEEPS=2
prompt: prompt.mp3

prompt.mp3:
	ffmpeg -f lavfi -i "sine=frequency=440:duration=0.3" \
	  -f lavfi -i "anullsrc=r=48000:cl=stereo" \
	  -filter_complex "[0]aresample=48000,pan=stereo|c0=c0|c1=c0[beep];[1]atrim=0:0.7[silence];[beep][silence]concat=n=2:v=0:a=1,aloop=loop=$$(($(BEEPS)-1)):size=48000[loop];[loop]aresample=48000" \
	  -ac 2 -ar 48000 $@ -y

# Generate prompt using GLaDOS TTS (https://glados.c-net.org/)
# Usage: make glados-prompt GLADOS_TEXT="Your message here"
glados-prompt:
	curl -L --retry 30 --get --fail \
	  --data-urlencode "text=$(GLADOS_TEXT)" \
	  -o glados.wav "https://glados.c-net.org/generate"
	ffmpeg -i glados.wav -ac 2 -ar 48000 prompt.mp3 -y
	rm -f glados.wav
	@echo "Generated prompt.mp3 from GLaDOS TTS"

# Run the service (requires API_ID and API_HASH env vars)
run: build prompt
	mkdir -p recordings
	./build/call_service

image:
	podman build -t $(IMAGE) .

push: image
	podman push $(IMAGE)

clean:
	rm -rf build vendor/td/build

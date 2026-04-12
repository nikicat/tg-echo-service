# Stage 1: build
FROM docker.io/archlinux:latest AS builder

RUN pacman -Syu --noconfirm \
    base-devel git cmake openssl opus libvpx libyuv ffmpeg zlib gperf lame

WORKDIR /src
COPY . .

RUN git submodule update --init
RUN make BUILD_TYPE=Release -j$(nproc)
RUN strip -s build/call_service

# Generate default prompt if not present
RUN make prompt

# Stage 2: runtime
FROM docker.io/archlinux:latest

RUN pacman -Syu --noconfirm openssl opus libvpx libyuv ffmpeg zlib lame && \
    pacman -Scc --noconfirm

WORKDIR /app

COPY --from=builder /src/build/call_service .
COPY --from=builder /src/prompt.mp3 .

ENTRYPOINT ["./call_service"]

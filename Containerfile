# Stage 1: build
FROM docker.io/archlinux:latest AS builder

RUN pacman -Syu --noconfirm \
    base-devel git cmake openssl opus libvpx libyuv ffmpeg zlib gperf lame

WORKDIR /src

# Dep sources (cached unless vendor/stub/CMakeLists.txt change)
COPY vendor/ vendor/
COPY stub/ stub/
COPY CMakeLists.txt ./

# Build TDLib
RUN cmake -B vendor/td/build -S vendor/td -DCMAKE_BUILD_TYPE=Release && \
    cmake --build vendor/td/build -j$(nproc) && \
    cmake --install vendor/td/build --prefix /src/td-install

# Configure and build all deps (tgcalls pulls in webrtc, absl, etc.)
RUN touch main.cpp && cmake -B build -DCMAKE_BUILD_TYPE=Release
RUN cmake --build build -j$(nproc) --target tgcalls

# App source (only this layer rebuilds on main.cpp changes)
COPY main.cpp .
RUN cmake --build build -j$(nproc) --target call_service
RUN strip -s build/call_service

# Stage 2: runtime
FROM docker.io/archlinux:latest

RUN pacman -Syu --noconfirm openssl opus libvpx libyuv ffmpeg zlib lame && \
    pacman -Scc --noconfirm

WORKDIR /app

COPY --from=builder /src/build/call_service .

ENTRYPOINT ["./call_service"]

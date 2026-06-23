# Base image. Ubuntu's official image is glibc + multi-arch (amd64/arm64), so a
# single base serves both architectures. Declared before FROM so both stages
# pick it up; override BASE_IMAGE to pin a different tag.
ARG BASE_IMAGE=docker.io/ubuntu:24.04

# Stage 1: build
FROM ${BASE_IMAGE} AS builder

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential git cmake pkg-config gperf ca-certificates \
    libssl-dev libopus-dev libvpx-dev libavcodec-dev libavformat-dev libavutil-dev \
    zlib1g-dev libmp3lame-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# Dep sources (cached unless vendor/stub/CMakeLists.txt change)
COPY vendor/ vendor/
COPY stub/ stub/
COPY CMakeLists.txt ./

# WebRTC includes ffmpeg as "third_party/ffmpeg/libav*/...". The vendored
# symlink targets /usr/include (Arch's flat layout); Debian/Ubuntu keep those
# headers under a multiarch triplet dir, so repoint it for this build.
RUN ln -sfn "/usr/include/$(uname -m)-linux-gnu" vendor/third_party/ffmpeg

# Build TDLib
RUN cmake -B vendor/td/build -S vendor/td -DCMAKE_BUILD_TYPE=Release && \
    cmake --build vendor/td/build -j$(nproc) && \
    cmake --install vendor/td/build --prefix /src/td-install

# libyuv isn't packaged on Ubuntu, so build the vendored submodule as a static,
# position-independent lib that links into the (PIE) executable. The project
# locates it via find_library(yuv) and uses the vendored headers directly.
RUN cmake -B vendor/libyuv/build -S vendor/libyuv -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON && \
    cmake --build vendor/libyuv/build -j$(nproc) --target yuv && \
    install -Dm644 vendor/libyuv/build/libyuv.a /usr/local/lib/libyuv.a

# Custom tgcalls platform source (compiled into the tgcalls lib, so it must
# exist at configure time). Copied after the deps so editing it doesn't bust the
# cached TDLib/libyuv layers, but before configure since CMakeLists.txt uses it.
COPY video_platform.cpp .

# Configure and build all deps (tgcalls pulls in webrtc, absl, etc.)
RUN touch main.cpp && cmake -B build -DCMAKE_BUILD_TYPE=Release
RUN cmake --build build -j$(nproc) --target tgcalls

# App source (only this layer rebuilds on main.cpp changes)
COPY main.cpp .
RUN cmake --build build -j$(nproc) --target tg-echo
RUN strip -s build/tg-echo

# Collect the binary's shared-library closure (minus the glibc core the runtime
# base already ships) into a flat dir. The runtime stage drops these into
# /usr/local/lib; copying flat avoids recreating /lib as a real directory, which
# (via Ubuntu's /lib -> /usr/lib usrmerge symlink) a path-preserving copy onto /
# would clobber, breaking the dynamic linker. This keeps the runtime minimal
# without hand-listing version-suffixed Debian packages, identically on amd64/arm64.
RUN mkdir -p /rootfs && \
    ldd build/tg-echo | awk '/=> \//{print $3}' | sort -u | \
    grep -vE '/(ld-linux.*|libc|libm|libdl|libpthread|librt|libresolv|libgcc_s)\.so' | \
    xargs -I{} cp -L {} /rootfs/

# Stage 2: runtime
FROM ${BASE_IMAGE}

# ca-certificates for TLS to Telegram; the app's shared libs come from /rootfs.
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# /usr/local/lib is on the default loader path; ldconfig registers the copied
# libs (and their soname links) without touching system lib dirs.
COPY --from=builder /rootfs/ /usr/local/lib/
RUN ldconfig
COPY --from=builder /src/build/tg-echo .

ENTRYPOINT ["./tg-echo"]

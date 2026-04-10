# tg-call-service

Pure C++ service that accepts incoming Telegram voice calls, plays a PCM audio prompt in a loop, and records the caller's audio.

Uses [TDLib](https://github.com/tdlib/td) for Telegram signaling and [tgcalls](https://github.com/TGX-Android/tgcalls) for WebRTC audio.

## Architecture

```
TDLib (signaling)              tgcalls (WebRTC audio)
  updateCall ──────────────>  Instance::Create()
  acceptCall                  FakeAudioDeviceModule
  sendCallSignalingData <───  signalingDataEmitted
  updateNewCallSignaling ───> receiveSignalingData()
  discardCall                 FilePlayer  -> prompt.raw loop
                              FileRecorder -> recordings/*.raw
```

## Prerequisites

### System packages (Arch Linux)

```
pacman -S openssl opus libvpx libyuv ffmpeg zlib gperf cmake
```

All C++ dependencies (tgcalls, WebRTC, abseil, libsrtp, usrsctp, openh264, rnnoise, crc32c, libevent, libyuv) are vendored as git submodules under `vendor/`.

## Build

```bash
git clone --recurse-submodules <repo-url>
cd tg-call-service
make
```

This runs all build steps: init submodules, build TDLib, configure cmake, compile ~1400 sources. First build takes several minutes; subsequent `make` only recompiles changed files.

Individual targets:

| Target | Description |
|--------|-------------|
| `make` | Full build (submodules + TDLib + call_service) |
| `make tdlib` | Build TDLib only (into `td-install/`) |
| `make build` | Build call_service only (assumes TDLib is built) |
| `make prompt` | Generate a test beep prompt (`prompt.raw`) |
| `make run` | Build + generate prompt + run the service |
| `make clean` | Remove build directories |

Pass `BUILD_TYPE=Release` for optimized build.

Output: `build/call_service` (~270MB debug, strip with `strip -s` for ~30MB).

## Usage

### Telegram API credentials

Obtain `api_id` and `api_hash` from https://my.telegram.org/apps.

Pass via environment variables or command-line flags:

```bash
# env vars
export API_ID=12345
export API_HASH=abcdef1234567890

# or flags
./build/call_service --api-id 12345 --api-hash abcdef1234567890
```

Tip: use a `.envrc` with [direnv](https://direnv.net/) to load them automatically.

### Prepare audio prompt

The prompt must be raw PCM: 48kHz, stereo, signed 16-bit little-endian.

```bash
# convert any audio file
ffmpeg -i input.mp3 -f s16le -ac 2 -ar 48000 prompt.raw

# or generate a test beep
ffmpeg -f lavfi -i "sine=frequency=440:duration=0.3" \
  -f lavfi -i "anullsrc=r=48000:cl=stereo" \
  -filter_complex "[0]aresample=48000,pan=stereo|c0=c0|c1=c0[beep];[1]atrim=0:0.7[silence];[beep][silence]concat=n=2:v=0:a=1,aloop=loop=4:size=48000[loop];[loop]aresample=48000" \
  -t 5 -f s16le -acodec pcm_s16le -ac 2 -ar 48000 prompt.raw -y
```

### Run

```bash
./build/call_service [--prompt prompt.raw] [--recordings-dir recordings/]
```

On first run, TDLib will prompt interactively for:
1. Phone number
2. Auth code (sent to Telegram)
3. 2FA password (if enabled)

### TDLib session storage

TDLib persists its session in `tdlib_db/` in the working directory. Subsequent runs skip authentication. Delete this directory to log out / switch accounts.

### Recordings

Caller audio is saved to `recordings/recording_<unix_timestamp>.raw` in the same PCM format as the prompt (48kHz, stereo, s16le).

Play back:

```bash
ffplay -f s16le -ar 48000 -ch_layout stereo recordings/recording_*.raw
```

Convert to WAV:

```bash
ffmpeg -f s16le -ar 48000 -ch_layout stereo -i recording.raw recording.wav
```

## How it works

1. TDLib receives `updateCall` with `callStatePending` (incoming call)
2. Service sends `acceptCall` with supported tgcalls protocol versions
3. TDLib receives `callStateReady` with relay servers and encryption key
4. Service maps TDLib call servers to tgcalls endpoints/RTC servers, creates a tgcalls `Instance` via `Meta::Create`
5. `FakeAudioDeviceModule` provides custom audio I/O: `FilePlayer` reads `prompt.raw` in a loop, `FileRecorder` writes caller audio to disk
6. Signaling data is relayed bidirectionally between TDLib and tgcalls
7. On hangup, the recording file is closed and the service waits for the next call

## Project structure

```
CMakeLists.txt                Build orchestrator (vendored deps + tgcalls + our code)
main.cpp                      FilePlayer, FileRecorder, CallService, auth flow, event loop
stub/                         Build stubs (config.h, crc32c config, AudioDeviceModule stub)
vendor/                       Vendored dependencies (git submodules + cmake files)
  Build*.cmake                CMake build scripts (from Telegram-X)
  tgcalls/                    TGX-Android/tgcalls (call protocol library)
  webrtc/                     TGX-Android/webrtc (WebRTC fork)
  abseil-cpp/                 abseil/abseil-cpp
  libsrtp/                    cisco/libsrtp
  usrsctp/                    sctplab/usrsctp
  openh264/                   cisco/openh264
  rnnoise/                    xiph/rnnoise
  crc32c/                     google/crc32c
  libevent/                   TGX-Android/libevent
  libyuv/                     chromium/libyuv
  td/                         tdlib/td (TDLib source, built separately into td-install/)
  webrtc_deps/                Copied files from chromium (pffft, rnnoise weights, field trials)
  third_party/                Symlinks for WebRTC include resolution (libyuv, libsrtp, etc.)
td-install/                   TDLib cmake exports and static libraries (not committed)
build/                        CMake build directory (not committed)
tdlib_db/                     TDLib session data (not committed)
recordings/                   Recorded audio files (not committed)
```

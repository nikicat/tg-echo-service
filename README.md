# tg-call-service

Pure C++ service that accepts incoming Telegram voice calls, plays a PCM audio prompt, echoes the caller's audio back with a configurable delay, records to MP3, and sends the recording as a voice message after the call ends.

Uses [TDLib](https://github.com/tdlib/td) for Telegram signaling and [tgcalls](https://github.com/TGX-Android/tgcalls) for WebRTC audio.

## Architecture

```
TDLib (signaling)              tgcalls (WebRTC audio)
  updateCall ──────────────>  Instance::Create()
  acceptCall                  FakeAudioDeviceModule
  sendCallSignalingData <───  signalingDataEmitted
  updateNewCallSignaling ───> receiveSignalingData()
  discardCall                 EchoPlayer  -> prompt once, then echo
  sendMessage (voice note)    FileRecorder -> recordings/*.mp3
```

## Prerequisites

### System packages (Arch Linux)

```
pacman -S openssl opus libvpx libyuv ffmpeg zlib gperf cmake lame
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
| `make prompt` | Generate a test beep prompt (MP3) |
| `make glados-prompt` | Generate prompt via GLaDOS TTS |
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

The prompt must be an MP3 file. It is decoded to PCM at startup via LAME.

```bash
# use any MP3 file directly
cp greeting.mp3 prompt.mp3

# or generate a test beep
make prompt

# or generate a GLaDOS voice prompt (https://glados.c-net.org/)
make glados-prompt
make glados-prompt GLADOS_TEXT="The cake is a lie. Leave a message."
```

### Run

```bash
./build/call_service [--prompt prompt.mp3] [--recordings-dir recordings/] [--echo-delay 1000]
```

| Flag | Env var | Default | Description |
|------|---------|---------|-------------|
| `--prompt` | — | `prompt.mp3` | PCM audio prompt file |
| `--recordings-dir` | — | `recordings/` | Directory for MP3 recordings |
| `--echo-delay` | `ECHO_DELAY` | `1000` | Echo delay in milliseconds |

On first run, TDLib will prompt interactively for:
1. Phone number
2. Auth code (sent to Telegram)
3. 2FA password (if enabled)

### TDLib session storage

TDLib persists its session in `tdlib_db/` in the working directory. Subsequent runs skip authentication. Delete this directory to log out / switch accounts.

### Recordings

Caller audio is saved as MP3 to `recordings/recording_<unix_timestamp>.mp3` (VBR, high quality). After the call ends, the recording is automatically sent back to the caller as a Telegram voice message.

Play back:

```bash
ffplay recordings/recording_*.mp3
```

## How it works

1. TDLib receives `updateCall` with `callStatePending` (incoming call)
2. Service sends `acceptCall` with supported tgcalls protocol versions
3. TDLib receives `callStateReady` with relay servers and encryption key
4. Service maps TDLib call servers to tgcalls endpoints/RTC servers, creates a tgcalls `Instance` via `Meta::Create`
5. `FakeAudioDeviceModule` provides custom audio I/O via `EchoPlayer` (implements both `Recorder` and `Renderer`): plays the prompt once, then echoes caller audio back with a configurable delay. `FileRecorder` encodes to MP3 via LAME
6. Signaling data is relayed bidirectionally between TDLib and tgcalls
7. On hangup, the MP3 recording is finalized and sent to the caller as a voice message via `sendMessage` + `inputMessageVoiceNote`

## Project structure

```
CMakeLists.txt                Build orchestrator (vendored deps + tgcalls + our code)
main.cpp                      EchoPlayer, FileRecorder, CallService, auth flow, event loop
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

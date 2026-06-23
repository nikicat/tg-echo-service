# Telegram Echo

> A self-hosted echo-test service for Telegram voice & video calls — call it to verify that your calls actually connect and that audio gets through your network.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Container image](https://img.shields.io/badge/image-docker.io%2Fnikicat%2Ftg--echo-2496ed.svg)](https://hub.docker.com/r/nikicat/tg-echo)

Think of it as Telegram's own version of Skype's old "Echo / Sound Test Service", but one you run yourself. When you ring it, **Telegram Echo** answers the call on a Telegram account, plays a short prompt, echoes your own voice back to you with a delay, records the call, and — after you hang up — sends the recording back as a voice message.

Because it exercises a real call end-to-end, it's a quick way to answer a question that's otherwise hard to test alone: *do Telegram voice/video calls actually work on this connection?*

## What it's for

Telegram calls use peer-to-peer WebRTC, which is exactly the kind of traffic that flaky links and censorship infrastructure tend to break. This service gives you a call partner that's always available, so you can check the call path on demand:

- 📶 **Flaky or high-latency connections** — confirm a call can connect and stay up, and hear the echo to judge the round-trip audio quality.
- 🧱 **DPI / state-level firewalls** — verify whether voice/video calls get through deep-packet-inspection censorship at all.
- 🔀 **Proxied Telegram** — check that calls still work when you route Telegram through a proxy or VPN, not just chats.
- 🛠️ **A working reference for Telegram call automation** — a complete, runnable example of wiring [TDLib](https://github.com/tdlib/td) signaling to [tgcalls](https://github.com/TGX-Android/tgcalls) WebRTC audio with a custom audio device, MP3 recording, and voice-note delivery. Working code for this is hard to find.

## What happens when you call it

```
1. You dial the service on Telegram        →  it auto-accepts the call
2. It plays your prompt.mp3 once            →  you hear the greeting = signaling works
3. You speak                               →  your voice is echoed back (with delay) = media flows
4. The call is recorded                    →  recordings/recording_<timestamp>.mp3
5. You hang up                             →  the recording is sent back as a voice message
```

```
TDLib (signaling)              tgcalls (WebRTC audio)
  updateCall ──────────────>  Instance::Create()
  acceptCall                  FakeAudioDeviceModule
  sendCallSignalingData <───  signalingDataEmitted
  updateNewCallSignaling ───> receiveSignalingData()
  discardCall                 EchoPlayer  -> prompt once, then echo
  sendMessage (voice note)    FileRecorder -> recordings/*.mp3
```

## Quick start (no build toolchain)

Runs from a pre-built container image — you only need Podman (or Docker) and Telegram API credentials.

1. **Get Telegram API credentials.** Create an app at <https://my.telegram.org/apps> to obtain an `api_id` and `api_hash`.

   ```bash
   export API_ID=12345
   export API_HASH=abcdef1234567890
   ```

2. **Make a prompt** (first time only):

   ```bash
   podman compose --profile tools run --rm prompt          # simple beep
   # or, with a GLaDOS voice:
   podman compose --profile tools run --rm glados-prompt
   ```

3. **Log in to Telegram** (first time only, interactive — asks for phone, code, 2FA):

   ```bash
   podman compose --profile auth run --rm auth
   ```

4. **Start the service**, then call its account from another device to run a test:

   ```bash
   podman compose up -d tg-echo
   ```

Session data (`tdlib_db/`), recordings, and `prompt.mp3` are bind-mounted from the project directory, so they persist across restarts.

> ⚠️ This logs in as a **real Telegram user account**, not a bot account — bot accounts can't receive calls. Use a dedicated account you control as the always-on test endpoint, then call it from your own account to test.

## Configuration

The service is configured via flags or environment variables:

| Flag | Env var | Default | Description |
|------|---------|---------|-------------|
| `--api-id` | `API_ID` | — | Telegram API id (required) |
| `--api-hash` | `API_HASH` | — | Telegram API hash (required) |
| `--prompt` | — | `prompt.mp3` | Prompt played to the caller |
| `--recordings-dir` | — | `recordings/` | Where MP3 recordings are written |
| `--echo-delay` | `ECHO_DELAY` | `1000` | Echo delay in milliseconds |

The prompt must be an MP3 (decoded to PCM at startup via LAME). Use any MP3 directly, or generate one with `make prompt` / `make glados-prompt GLADOS_TEXT="The cake is a lie."`.

Recordings land in `recordings/recording_<unix_timestamp>.mp3` (VBR, high quality). Play them back with `ffplay recordings/recording_*.mp3`.

TDLib persists its session in `tdlib_db/`; delete that directory to log out or switch accounts.

## Build from source

### Prerequisites (Arch Linux)

```bash
pacman -S openssl opus libvpx libyuv ffmpeg zlib gperf cmake lame
```

All C++ dependencies (tgcalls, WebRTC, abseil, libsrtp, usrsctp, openh264, rnnoise, crc32c, libevent, libyuv) are vendored as git submodules under `vendor/`.

### Build

```bash
git clone --recurse-submodules ssh://git@github.com/nikicat/tg-echo.git
cd tg-echo
make
```

`make` runs every step: init submodules, build TDLib, configure cmake, compile ~1400 sources. The first build takes several minutes; later builds only recompile what changed. Pass `BUILD_TYPE=Release` for an optimized build.

Output: `build/tg-echo` (~270 MB debug; `strip -s` it to ~30 MB).

| Target | Description |
|--------|-------------|
| `make` | Full build (submodules + TDLib + tg-echo) |
| `make tdlib` | Build TDLib only (into `td-install/`) |
| `make build` | Build the tg-echo binary only (assumes TDLib is built) |
| `make prompt` | Generate a test beep prompt (MP3) |
| `make glados-prompt` | Generate a prompt via GLaDOS TTS |
| `make run` | Build + generate prompt + run |
| `make image` / `make push` | Build / push the container image |
| `make clean` | Remove build directories |

### Run

```bash
./build/tg-echo [--prompt prompt.mp3] [--recordings-dir recordings/] [--echo-delay 1000]
```

On first run TDLib asks interactively for your phone number, the auth code Telegram sends you, and your 2FA password if set.

## How it works

1. TDLib receives `updateCall` with `callStatePending` (an incoming call).
2. The service sends `acceptCall` with the supported tgcalls protocol versions.
3. TDLib reports `callStateReady` with relay servers and the encryption key.
4. The service maps TDLib's call servers to tgcalls endpoints/RTC servers and creates a tgcalls `Instance` via `Meta::Create`.
5. A `FakeAudioDeviceModule` supplies custom audio I/O: `EchoPlayer` (acting as both `Recorder` and `Renderer`) plays the prompt once, then echoes caller audio back with the configured delay, while `FileRecorder` encodes everything to MP3 via LAME.
6. Signaling data is relayed bidirectionally between TDLib and tgcalls.
7. On hangup, the MP3 is finalized and sent to the caller as a voice message via `sendMessage` + `inputMessageVoiceNote`.

## Project structure

```
main.cpp                      EchoPlayer, FileRecorder, CallService, auth flow, event loop
video_platform.cpp            Video frame plumbing
CMakeLists.txt                Build orchestrator (vendored deps + tgcalls + our code)
compose.yaml                  Podman/Docker Compose services (tg-echo, auth, tools)
deploy/                       Podman Quadlet unit for systemd-managed deployment
stub/                         Build stubs (config.h, crc32c config, AudioDeviceModule stub)
vendor/                       Vendored dependencies (git submodules + cmake build scripts)
  tgcalls/  webrtc/  td/      Core: call protocol, WebRTC fork, TDLib
  abseil-cpp/ libsrtp/ ...    Supporting libraries
td-install/                   TDLib cmake exports + static libs (not committed)
build/                        CMake build directory (not committed)
tdlib_db/                     TDLib session data (not committed)
recordings/                   Recorded audio files (not committed)
```

## License

This project's original code is released under the [MIT License](LICENSE). It links several third-party libraries with their own terms — most notably **tgcalls (LGPL v3)**. See [NOTICE.md](NOTICE.md) for the full dependency licensing breakdown and what it means for redistributing binaries.

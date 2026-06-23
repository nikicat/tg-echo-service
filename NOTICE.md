# Third-party licenses

The original source in this repository (`main.cpp`, `video_platform.cpp`, the
`stub/` helpers, and the build glue) is licensed under the MIT License — see
[LICENSE](LICENSE).

It is built against, and statically links, several vendored dependencies
(under `vendor/`) that carry their own licenses. The most important one is
**tgcalls (LGPL v3)**: because the resulting binary links it, any binary you
distribute must remain relinkable against a modified tgcalls, per the LGPL.
If you only run the service yourself, this does not apply.

| Dependency | Upstream | License |
|------------|----------|---------|
| tgcalls | [TGX-Android/tgcalls](https://github.com/TGX-Android/tgcalls) | LGPL v3 |
| WebRTC | [TGX-Android/webrtc](https://github.com/TGX-Android/webrtc) | BSD-3-Clause |
| TDLib | [tdlib/td](https://github.com/tdlib/td) | Boost Software License 1.0 |
| abseil-cpp | [abseil/abseil-cpp](https://github.com/abseil/abseil-cpp) | Apache-2.0 |
| libsrtp | [cisco/libsrtp](https://github.com/cisco/libsrtp) | BSD-3-Clause |
| usrsctp | [sctplab/usrsctp](https://github.com/sctplab/usrsctp) | BSD-3-Clause |
| openh264 | [cisco/openh264](https://github.com/cisco/openh264) | BSD-2-Clause |
| rnnoise | [xiph/rnnoise](https://github.com/xiph/rnnoise) | BSD-3-Clause |
| crc32c | [google/crc32c](https://github.com/google/crc32c) | BSD-3-Clause |
| libevent | [TGX-Android/libevent](https://github.com/TGX-Android/libevent) | BSD-3-Clause |
| libyuv | [chromium/libyuv](https://chromium.googlesource.com/libyuv/libyuv) | BSD-3-Clause |

Each dependency's full license text is available in its respective directory
under `vendor/`.

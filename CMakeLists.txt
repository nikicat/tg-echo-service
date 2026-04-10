cmake_minimum_required(VERSION 3.16)
project(call_service LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
cmake_policy(SET CMP0069 NEW)  # IPO

# -- Paths to vendored sources --
set(VENDOR_DIR "${CMAKE_SOURCE_DIR}/vendor")
set(TGCALLS_DEPS_DIR "${VENDOR_DIR}")
set(STUB_DIR "${CMAKE_SOURCE_DIR}/stub")

# Hack: BuildWebRTC.cmake uses ANDROID_ABI for platform-conditional sources
set(ANDROID_ABI "x86_64")

# -- System library targets (must exist before Build*.cmake includes) --

# OpenSSL
find_package(OpenSSL REQUIRED)
add_library(ssl INTERFACE)
target_link_libraries(ssl INTERFACE OpenSSL::SSL)
add_library(crypto INTERFACE)
target_link_libraries(crypto INTERFACE OpenSSL::Crypto)

# Opus
find_library(OPUS_LIB opus REQUIRED)
add_library(opus INTERFACE)
target_link_libraries(opus INTERFACE ${OPUS_LIB})
target_include_directories(opus INTERFACE /usr/include/opus)

# libvpx
find_library(VPX_LIB vpx REQUIRED)
add_library(vpx INTERFACE)
target_link_libraries(vpx INTERFACE ${VPX_LIB})

# libyuv — WebRTC expects third_party/libyuv/include/libyuv/ path
find_library(YUV_LIB yuv REQUIRED)
add_library(yuv INTERFACE)
target_link_libraries(yuv INTERFACE ${YUV_LIB})
target_include_directories(yuv INTERFACE "${VENDOR_DIR}/libyuv/include")

# FFmpeg avcodec (needed by WebRTC for H.265 decode)
find_library(AVCODEC_LIB avcodec REQUIRED)
find_library(AVUTIL_LIB avutil REQUIRED)
add_library(avcodec INTERFACE)
target_link_libraries(avcodec INTERFACE ${AVCODEC_LIB} ${AVUTIL_LIB})

# -- Include vendored dependency builds --
# These create static library targets: usrsctp, srtp, openh264, absl, json11, rnnoise, webrtc

include("${VENDOR_DIR}/BuildUsrSCTP.cmake")
include("${VENDOR_DIR}/BuildLibSRTP.cmake")
include("${VENDOR_DIR}/BuildOpenH264.cmake")
include("${VENDOR_DIR}/BuildAbsl.cmake")
include("${VENDOR_DIR}/BuildJson11.cmake")
include("${VENDOR_DIR}/BuildRNNoise.cmake")
include("${VENDOR_DIR}/BuildWebRTC.cmake")

# -- Fix WebRTC for Linux desktop (undo Android-specific bits) --

# Remove WEBRTC_ANDROID define, keep WEBRTC_POSIX + WEBRTC_LINUX
# Must fix both COMPILE_DEFINITIONS and INTERFACE_COMPILE_DEFINITIONS
foreach(_prop COMPILE_DEFINITIONS INTERFACE_COMPILE_DEFINITIONS)
  get_target_property(_defs webrtc ${_prop})
  if (_defs)
    list(REMOVE_ITEM _defs "WEBRTC_ANDROID")
    list(REMOVE_ITEM _defs "WEBRTC_INCLUDE_INTERNAL_AUDIO_DEVICE")
    list(APPEND _defs "_EVENT_HAVE_FD_MASK")
    set_target_properties(webrtc PROPERTIES ${_prop} "${_defs}")
  endif()
endforeach()

# Remove Android-only sources that include <android/log.h>
get_target_property(_webrtc_sources webrtc SOURCES)
set(WEBRTC_DIR "${TGCALLS_DEPS_DIR}/webrtc")
list(REMOVE_ITEM _webrtc_sources
  "${WEBRTC_DIR}/modules/utility/source/helpers_android.cc"
  "${WEBRTC_DIR}/modules/utility/source/jvm_android.cc"
  "${WEBRTC_DIR}/rtc_base/ifaddrs_android.cc"
  "${WEBRTC_DIR}/rtc_base/system/warn_current_thread_is_deadlocked.cc"
  "${WEBRTC_DIR}/modules/audio_device/audio_device_impl.cc"
)
list(APPEND _webrtc_sources
  "${TGCALLS_DEPS_DIR}/crc32c/src/crc32c_sse42.cc"
  "${STUB_DIR}/audio_device_stub.cc"
)
set_target_properties(webrtc PROPERTIES SOURCES "${_webrtc_sources}")

# crc32c_sse42.cc needs SSE4.2
set_source_files_properties("${TGCALLS_DEPS_DIR}/crc32c/src/crc32c_sse42.cc"
  PROPERTIES COMPILE_FLAGS "-msse4.2")

# Add pthread (Linux needs it explicitly)
target_link_libraries(webrtc PUBLIC pthread)

# WebRTC includes paths like "third_party/libyuv/include/..." and
# "third_party/libsrtp/crypto/include/..." — vendor/third_party/ has
# symlinks to resolve these
target_include_directories(webrtc PRIVATE
  "${VENDOR_DIR}"
)

# Suppress warnings-as-errors for vendored code on GCC 15
# Also force-include stdint.h and stddef.h for GCC 15 compat
target_compile_options(webrtc PRIVATE -Wno-error -Wno-deprecated-declarations
  "SHELL:-include stdint.h" "SHELL:-include stddef.h")
target_compile_options(absl PRIVATE -Wno-error
  "SHELL:-include stdint.h" "SHELL:-include stddef.h")
target_compile_options(openh264 PRIVATE
  "SHELL:-include stdint.h" "SHELL:-include stddef.h")
target_compile_options(usrsctp PRIVATE
  "SHELL:-include stdint.h" "SHELL:-include stddef.h")
target_compile_options(srtp PRIVATE
  "SHELL:-include stdint.h" "SHELL:-include stddef.h")

# -- Build tgcalls (modified from BuildTgCalls.cmake) --
# Excluded: legacy (needs libtgvoip), group (needs ffmpeg), desktop_capturer (needs X11)
# Replaced: platform/android → platform/fake

set(TGCALLS_DIR "${TGCALLS_DEPS_DIR}/tgcalls")

add_library(tgcalls STATIC
  "${TGCALLS_DIR}/tgcalls/utils/gzip.cpp"

  # Platform: fake (no video, no hardware audio)
  "${TGCALLS_DIR}/tgcalls/platform/fake/FakeInterface.cpp"

  # Core
  "${TGCALLS_DIR}/tgcalls/Manager.cpp"
  "${TGCALLS_DIR}/tgcalls/MediaManager.cpp"
  "${TGCALLS_DIR}/tgcalls/AudioDeviceHelper.cpp"
  "${TGCALLS_DIR}/tgcalls/ChannelManager.cpp"
  "${TGCALLS_DIR}/tgcalls/CodecSelectHelper.cpp"
  "${TGCALLS_DIR}/tgcalls/CryptoHelper.cpp"
  "${TGCALLS_DIR}/tgcalls/EncryptedConnection.cpp"
  "${TGCALLS_DIR}/tgcalls/FakeAudioDeviceModule.cpp"
  "${TGCALLS_DIR}/tgcalls/FakeVideoTrackSource.cpp"
  "${TGCALLS_DIR}/tgcalls/FieldTrialsConfig.cpp"
  "${TGCALLS_DIR}/tgcalls/Instance.cpp"
  "${TGCALLS_DIR}/tgcalls/InstanceImpl.cpp"
  "${TGCALLS_DIR}/tgcalls/LogSinkImpl.cpp"
  "${TGCALLS_DIR}/tgcalls/Message.cpp"
  "${TGCALLS_DIR}/tgcalls/NetworkManager.cpp"
  "${TGCALLS_DIR}/tgcalls/SctpDataChannelProviderInterfaceImpl.cpp"
  "${TGCALLS_DIR}/tgcalls/StaticThreads.cpp"
  "${TGCALLS_DIR}/tgcalls/ThreadLocalObject.cpp"
  "${TGCALLS_DIR}/tgcalls/TurnCustomizerImpl.cpp"
  "${TGCALLS_DIR}/tgcalls/VideoCaptureInterface.cpp"
  "${TGCALLS_DIR}/tgcalls/VideoCaptureInterfaceImpl.cpp"

  # V2 protocol (modern calls)
  "${TGCALLS_DIR}/tgcalls/v2/ContentNegotiation.cpp"
  "${TGCALLS_DIR}/tgcalls/v2/DirectNetworkingImpl.cpp"
  "${TGCALLS_DIR}/tgcalls/v2/ExternalSignalingConnection.cpp"
  "${TGCALLS_DIR}/tgcalls/v2/InstanceV2Impl.cpp"
  "${TGCALLS_DIR}/tgcalls/v2/InstanceV2ReferenceImpl.cpp"
  "${TGCALLS_DIR}/tgcalls/v2/NativeNetworkingImpl.cpp"
  "${TGCALLS_DIR}/tgcalls/v2/RawTcpSocket.cpp"
  "${TGCALLS_DIR}/tgcalls/v2/ReflectorPort.cpp"
  "${TGCALLS_DIR}/tgcalls/v2/ReflectorRelayPortFactory.cpp"
  "${TGCALLS_DIR}/tgcalls/v2/Signaling.cpp"
  "${TGCALLS_DIR}/tgcalls/v2/SignalingEncryption.cpp"
  "${TGCALLS_DIR}/tgcalls/v2/SignalingConnection.cpp"
  "${TGCALLS_DIR}/tgcalls/v2/SignalingSctpConnection.cpp"
)

target_include_directories(tgcalls PRIVATE
  "${CMAKE_SOURCE_DIR}"
  "${STUB_DIR}"
)
target_include_directories(tgcalls PUBLIC
  "${TGCALLS_DIR}"
  "${TGCALLS_DIR}/tgcalls"
)

target_compile_options(tgcalls PUBLIC
  -Wno-deprecated-declarations
  -fno-strict-aliasing
  -frtti
  -funroll-loops
  -fexceptions
  -fno-math-errno
  -Wno-error
)

target_link_libraries(tgcalls PUBLIC
  webrtc
)
target_link_libraries(tgcalls PRIVATE
  rnnoise
  json11
  z
)

# -- Find TDLib --
set(TD_INSTALL_DIR "${CMAKE_SOURCE_DIR}/td-install")
list(APPEND CMAKE_PREFIX_PATH "${TD_INSTALL_DIR}")
find_package(Td REQUIRED)

# -- Our call_service binary --
add_executable(call_service main.cpp)

target_link_libraries(call_service PRIVATE
  tgcalls
  Td::TdStatic
)

target_include_directories(call_service PRIVATE
  "${TGCALLS_DIR}"
  "${TGCALLS_DIR}/tgcalls"
  "${WEBRTC_DIR}"
  "${TGCALLS_DEPS_DIR}/abseil-cpp"
  "${TGCALLS_DEPS_DIR}/webrtc_deps"
  "${STUB_DIR}"
)

target_compile_definitions(call_service PRIVATE
  WEBRTC_POSIX
  WEBRTC_LINUX
  HAVE_SSE2
  NDEBUG
  OPENSSL_SUPPRESS_DEPRECATED
)

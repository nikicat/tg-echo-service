// Custom tgcalls PlatformInterface for the echo service.
//
// Replaces vendor/tgcalls/tgcalls/platform/fake/FakeInterface.cpp in the build.
// The upstream fake platform stubs out all video (makeVideoSource -> nullptr,
// supportsEncoding -> false), which disables outgoing/incoming video entirely.
//
// Here we:
//   - return a self-feeding, looping animated outgoing video source
//     (AnimatedVideoTrackSource, built on the existing FrameSource::chess()), and
//   - advertise H264/VP8 encoding (both verified available in this build:
//     openh264 + system libvpx), so the video channel gets negotiated.
//
// Incoming video decoding uses the builtin decoder factory (VP8/VP9/H264), so the
// service receives already-decoded I420 frames via Instance::setIncomingVideoOutput().

#include "platform/fake/FakeInterface.h"
#include "FakeVideoTrackSource.h"  // tgcalls::FrameSource

#include "api/video_codecs/builtin_video_encoder_factory.h"
#include "api/video_codecs/builtin_video_decoder_factory.h"
#include "api/video/video_source_interface.h"
#include "media/base/media_constants.h"
#include "media/base/video_broadcaster.h"
#include "pc/video_track_source.h"
#include "rtc_base/ref_counted_object.h"
#include "rtc_base/time_utils.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <utility>

namespace tgcalls {

namespace {

// A video source that generates frames on its own thread and broadcasts them to
// whatever sink the outgoing encoder attaches. Modeled on the upstream
// FakeVideoSource in FakeVideoTrackSource.cpp, but with a joinable thread that is
// cleanly stopped in the destructor (the upstream version detaches the thread and
// never stops it, which would leak a running thread per call).
class AnimatedVideoSource : public rtc::VideoSourceInterface<webrtc::VideoFrame> {
public:
    explicit AnimatedVideoSource(std::unique_ptr<FrameSource> source) {
        thread_ = std::thread([this, source = std::move(source)]() mutable {
            std::uint32_t step = 0;
            while (!stop_.load()) {
                step++;
                std::this_thread::sleep_for(std::chrono::milliseconds(1000 / 30));
                auto frame = source->next_frame();
                frame.set_id(static_cast<std::uint16_t>(step));
                frame.set_timestamp_us(rtc::TimeMicros());
                broadcaster_.OnFrame(frame);
            }
        });
    }

    ~AnimatedVideoSource() override {
        stop_.store(true);
        if (thread_.joinable())
            thread_.join();
    }

    void AddOrUpdateSink(rtc::VideoSinkInterface<webrtc::VideoFrame> *sink,
                         const rtc::VideoSinkWants &wants) override {
        broadcaster_.AddOrUpdateSink(sink, wants);
    }

    void RemoveSink(rtc::VideoSinkInterface<webrtc::VideoFrame> *sink) override {
        broadcaster_.RemoveSink(sink);
    }

private:
    std::atomic<bool> stop_{false};
    rtc::VideoBroadcaster broadcaster_;
    std::thread thread_;  // declared last: started after, joined before, the members above
};

class AnimatedVideoTrackSource : public webrtc::VideoTrackSource {
public:
    static webrtc::scoped_refptr<AnimatedVideoTrackSource> Create(std::unique_ptr<FrameSource> source) {
        return webrtc::scoped_refptr<AnimatedVideoTrackSource>(
            new rtc::RefCountedObject<AnimatedVideoTrackSource>(std::move(source)));
    }

    explicit AnimatedVideoTrackSource(std::unique_ptr<FrameSource> source)
        : VideoTrackSource(/*remote=*/false), source_(std::move(source)) {}

protected:
    rtc::VideoSourceInterface<webrtc::VideoFrame> *source() override {
        return &source_;
    }

private:
    AnimatedVideoSource source_;
};

}  // namespace

std::unique_ptr<webrtc::VideoEncoderFactory> FakeInterface::makeVideoEncoderFactory(bool preferHardwareEncoding,
                                                                                    bool isScreencast) {
    return webrtc::CreateBuiltinVideoEncoderFactory();
}

std::unique_ptr<webrtc::VideoDecoderFactory> FakeInterface::makeVideoDecoderFactory() {
    return webrtc::CreateBuiltinVideoDecoderFactory();
}

webrtc::scoped_refptr<webrtc::VideoTrackSourceInterface> FakeInterface::makeVideoSource(rtc::Thread *signalingThread,
                                                                                        rtc::Thread *workerThread) {
    // Self-feeding looping animation (the "video prompt"). The chess FrameSource
    // wraps modulo its frame count, so it loops for the whole call.
    return AnimatedVideoTrackSource::Create(FrameSource::chess());
}

bool FakeInterface::supportsEncoding(const std::string &codecName) {
    return (codecName == cricket::kH264CodecName) || (codecName == cricket::kVp8CodecName);
}

void FakeInterface::adaptVideoSource(webrtc::scoped_refptr<webrtc::VideoTrackSourceInterface> videoSource, int width,
                                     int height, int fps) {
}

std::unique_ptr<VideoCapturerInterface> FakeInterface::makeVideoCapturer(
    webrtc::scoped_refptr<webrtc::VideoTrackSourceInterface> source, std::string deviceId,
    std::function<void(VideoState)> stateUpdated, std::function<void(PlatformCaptureInfo)> captureInfoUpdated,
    std::shared_ptr<PlatformContext> platformContext, std::pair<int, int> &outResolution) {
    // Our source feeds itself, so no capturer is needed. Every use site in
    // VideoCaptureInterfaceImpl is guarded by `if (_videoCapturer)`.
    return nullptr;
}

std::unique_ptr<PlatformInterface> CreatePlatformInterface() {
    return std::make_unique<FakeInterface>();
}

}  // namespace tgcalls

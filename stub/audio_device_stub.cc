// Stub for AudioDeviceModule::Create — we always provide a custom createAudioDeviceModule
// in the tgcalls Descriptor, so this factory is never called at runtime.
// But the linker needs the symbol because tgcalls fallback code references it.

#include "modules/audio_device/include/audio_device.h"
#include "api/make_ref_counted.h"
#include "modules/audio_device/include/test_audio_device.h"

namespace webrtc {

rtc::scoped_refptr<AudioDeviceModule> AudioDeviceModule::Create(
    AudioLayer /*audio_layer*/,
    TaskQueueFactory* /*task_queue_factory*/) {
  // Should never be called — we provide FakeAudioDeviceModule via descriptor
  return nullptr;
}

rtc::scoped_refptr<AudioDeviceModuleForTest> AudioDeviceModule::CreateForTest(
    AudioLayer /*audio_layer*/,
    TaskQueueFactory* /*task_queue_factory*/) {
  return nullptr;
}

}  // namespace webrtc

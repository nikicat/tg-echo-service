// Telegram Call Service — accepts incoming calls, plays prompt, records caller audio
// Uses TDLib for signaling + tgcalls for audio

#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>

#include "tgcalls/Instance.h"
#include "tgcalls/InstanceImpl.h"
#include "tgcalls/v2/InstanceV2Impl.h"
#include "tgcalls/v2/InstanceV2ReferenceImpl.h"
#include "tgcalls/FakeAudioDeviceModule.h"

#include <lame/lame.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace td_api = td::td_api;

// -- File-based audio player (reads prompt.raw, loops) --

class FilePlayer : public tgcalls::FakeAudioDeviceModule::Recorder {
public:
    explicit FilePlayer(const std::string &path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            std::cerr << "Failed to open prompt file: " << path << std::endl;
            return;
        }
        auto size = file.tellg();
        file.seekg(0);
        data_.resize(size);
        file.read(reinterpret_cast<char *>(data_.data()), size);
        std::cout << "Loaded prompt: " << path << " (" << size << " bytes)" << std::endl;
    }

    tgcalls::AudioFrame Record() override {
        tgcalls::AudioFrame frame{};
        if (data_.empty()) {
            frame.num_samples = 0;
            return frame;
        }

        constexpr size_t samples_per_frame = 480;  // 10ms at 48kHz
        constexpr size_t channels = 2;
        constexpr size_t frame_bytes = samples_per_frame * channels * sizeof(int16_t);

        // Ensure buffer is allocated
        if (buffer_.size() < samples_per_frame * channels) {
            buffer_.resize(samples_per_frame * channels);
        }

        size_t bytes_copied = 0;
        while (bytes_copied < frame_bytes) {
            size_t remaining = frame_bytes - bytes_copied;
            size_t available = data_.size() - position_;
            size_t to_copy = std::min(remaining, available);
            std::memcpy(reinterpret_cast<char *>(buffer_.data()) + bytes_copied,
                        data_.data() + position_, to_copy);
            bytes_copied += to_copy;
            position_ += to_copy;
            if (position_ >= data_.size()) {
                position_ = 0;  // Loop
            }
        }

        frame.audio_samples = buffer_.data();
        frame.num_samples = samples_per_frame;
        frame.bytes_per_sample = sizeof(int16_t);
        frame.num_channels = channels;
        frame.samples_per_sec = 48000;
        return frame;
    }

private:
    std::vector<uint8_t> data_;
    std::vector<int16_t> buffer_;
    size_t position_ = 0;
};

// -- File-based audio recorder (writes caller audio to file) --

class FileRecorder : public tgcalls::FakeAudioDeviceModule::Renderer {
public:
    explicit FileRecorder(const std::string &path, int sample_rate = 48000, int num_channels = 2)
        : path_(path), num_channels_(num_channels) {
        file_.open(path, std::ios::binary);
        if (!file_) {
            std::cerr << "Failed to open recording file: " << path << std::endl;
            return;
        }

        lame_ = lame_init();
        if (!lame_) {
            std::cerr << "Failed to initialize LAME encoder" << std::endl;
            file_.close();
            return;
        }
        lame_set_in_samplerate(lame_, sample_rate);
        lame_set_num_channels(lame_, num_channels);
        lame_set_quality(lame_, 2);  // high quality
        lame_set_VBR(lame_, vbr_default);
        if (lame_init_params(lame_) < 0) {
            std::cerr << "Failed to set LAME parameters" << std::endl;
            lame_close(lame_);
            lame_ = nullptr;
            file_.close();
        }
    }

    bool Render(const tgcalls::AudioFrame &frame) override {
        if (!file_ || !lame_ || frame.num_samples == 0 || !frame.audio_samples)
            return true;

        int samples_per_channel = static_cast<int>(frame.num_samples / num_channels_);

        // Ensure MP3 buffer is large enough (worst case: 1.25 * samples + 7200)
        size_t mp3_buf_size = static_cast<size_t>(1.25 * samples_per_channel) + 7200;
        if (mp3_buffer_.size() < mp3_buf_size)
            mp3_buffer_.resize(mp3_buf_size);

        int mp3_bytes;
        if (num_channels_ == 2) {
            mp3_bytes = lame_encode_buffer_interleaved(
                lame_, const_cast<short *>(frame.audio_samples),
                samples_per_channel, mp3_buffer_.data(),
                static_cast<int>(mp3_buffer_.size()));
        } else {
            mp3_bytes = lame_encode_buffer(
                lame_, frame.audio_samples, nullptr,
                samples_per_channel, mp3_buffer_.data(),
                static_cast<int>(mp3_buffer_.size()));
        }

        if (mp3_bytes > 0) {
            file_.write(reinterpret_cast<const char *>(mp3_buffer_.data()), mp3_bytes);
            total_bytes_ += mp3_bytes;
        }
        return true;
    }

    ~FileRecorder() override {
        if (lame_ && file_) {
            // Flush remaining MP3 frames
            if (mp3_buffer_.size() < 7200)
                mp3_buffer_.resize(7200);
            int flush_bytes = lame_encode_flush(lame_, mp3_buffer_.data(),
                                                static_cast<int>(mp3_buffer_.size()));
            if (flush_bytes > 0) {
                file_.write(reinterpret_cast<const char *>(mp3_buffer_.data()), flush_bytes);
                total_bytes_ += flush_bytes;
            }
        }
        if (lame_)
            lame_close(lame_);
        if (file_) {
            file_.close();
            std::cout << "Recording saved: " << path_ << " (" << total_bytes_ << " bytes)" << std::endl;
        }
    }

private:
    std::string path_;
    std::ofstream file_;
    lame_global_flags *lame_ = nullptr;
    int num_channels_;
    std::vector<unsigned char> mp3_buffer_;
    size_t total_bytes_ = 0;
};

// -- Utility --

static std::string hex_string(const std::string &bytes) {
    static const char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (unsigned char c : bytes) {
        result.push_back(hex[c >> 4]);
        result.push_back(hex[c & 0xf]);
    }
    return result;
}

static std::string timestamp_string() {
    auto now = std::chrono::system_clock::now();
    auto epoch = now.time_since_epoch();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(epoch).count();
    return std::to_string(seconds);
}

// -- Call Service --

class CallService {
public:
    CallService(int32_t api_id, const std::string &api_hash,
                const std::string &prompt_file, const std::string &recordings_dir)
        : api_id_(api_id), api_hash_(api_hash),
          prompt_file_(prompt_file), recordings_dir_(recordings_dir) {

        // Register tgcalls implementations
        tgcalls::Register<tgcalls::InstanceImpl>();
        tgcalls::Register<tgcalls::InstanceV2Impl>();
        tgcalls::Register<tgcalls::InstanceV2ReferenceImpl>();

        auto versions = tgcalls::Meta::Versions();
        std::cout << "tgcalls versions:";
        for (const auto &v : versions) std::cout << " " << v;
        std::cout << std::endl;

        td::ClientManager::execute(td_api::make_object<td_api::setLogVerbosityLevel>(1));
        client_manager_ = std::make_unique<td::ClientManager>();
        client_id_ = client_manager_->create_client_id();
        send_query(td_api::make_object<td_api::getOption>("version"), {});
    }

    void run() {
        while (!quit_) {
            auto response = client_manager_->receive(1.0);
            if (response.object) {
                process_response(std::move(response));
            }
        }
    }

private:
    using Object = td_api::object_ptr<td_api::Object>;

    int32_t api_id_;
    std::string api_hash_;
    std::string prompt_file_;
    std::string recordings_dir_;
    bool quit_ = false;
    bool authorized_ = false;

    std::unique_ptr<td::ClientManager> client_manager_;
    td::ClientManager::ClientId client_id_{0};
    uint64_t query_id_{0};
    uint64_t auth_query_id_{0};
    std::map<uint64_t, std::function<void(Object)>> handlers_;

    // Active call state
    int32_t active_call_id_ = 0;
    bool active_call_outgoing_ = false;
    std::unique_ptr<tgcalls::Instance> tgcalls_instance_;
    std::shared_ptr<FilePlayer> player_;
    std::shared_ptr<FileRecorder> recorder_;

    void send_query(td_api::object_ptr<td_api::Function> f, std::function<void(Object)> handler) {
        auto id = ++query_id_;
        if (handler) handlers_.emplace(id, std::move(handler));
        client_manager_->send(client_id_, id, std::move(f));
    }

    void process_response(td::ClientManager::Response response) {
        if (!response.object) return;
        if (response.request_id == 0) {
            process_update(std::move(response.object));
        } else {
            auto it = handlers_.find(response.request_id);
            if (it != handlers_.end()) {
                it->second(std::move(response.object));
                handlers_.erase(it);
            }
        }
    }

    void process_update(Object update) {
        auto id = update->get_id();

        if (id == td_api::updateAuthorizationState::ID) {
            auto &u = static_cast<td_api::updateAuthorizationState &>(*update);
            on_auth_state(std::move(u.authorization_state_));
        } else if (id == td_api::updateCall::ID) {
            auto &u = static_cast<td_api::updateCall &>(*update);
            on_call_update(std::move(u.call_));
        } else if (id == td_api::updateNewCallSignalingData::ID) {
            auto &u = static_cast<td_api::updateNewCallSignalingData &>(*update);
            on_signaling_data(u.call_id_, u.data_);
        }
    }

    // -- Auth flow --

    void on_auth_state(td_api::object_ptr<td_api::AuthorizationState> state) {
        auth_query_id_++;
        auto handler = [this, expected = auth_query_id_](Object obj) {
            if (expected != auth_query_id_) return;
            if (obj->get_id() == td_api::error::ID) {
                auto &err = static_cast<td_api::error &>(*obj);
                std::cerr << "Auth error: " << err.code_ << " " << err.message_ << std::endl;
            }
        };

        switch (state->get_id()) {
            case td_api::authorizationStateWaitTdlibParameters::ID: {
                auto req = td_api::make_object<td_api::setTdlibParameters>();
                req->database_directory_ = "tdlib_db";
                req->use_message_database_ = false;
                req->use_secret_chats_ = false;
                req->api_id_ = api_id_;
                req->api_hash_ = api_hash_;
                req->system_language_code_ = "en";
                req->device_model_ = "CallService";
                req->application_version_ = "1.0";
                send_query(std::move(req), handler);
                break;
            }
            case td_api::authorizationStateWaitPhoneNumber::ID: {
                std::cout << "Enter phone number: " << std::flush;
                std::string phone;
                std::getline(std::cin, phone);
                send_query(td_api::make_object<td_api::setAuthenticationPhoneNumber>(phone, nullptr), handler);
                break;
            }
            case td_api::authorizationStateWaitCode::ID: {
                std::cout << "Enter auth code: " << std::flush;
                std::string code;
                std::getline(std::cin, code);
                send_query(td_api::make_object<td_api::checkAuthenticationCode>(code), handler);
                break;
            }
            case td_api::authorizationStateWaitPassword::ID: {
                std::cout << "Enter 2FA password: " << std::flush;
                std::string password;
                std::getline(std::cin, password);
                send_query(td_api::make_object<td_api::checkAuthenticationPassword>(password), handler);
                break;
            }
            case td_api::authorizationStateReady::ID:
                authorized_ = true;
                std::cout << "Authorized. Waiting for incoming calls..." << std::endl;
                break;
            case td_api::authorizationStateClosed::ID:
                std::cout << "Session closed." << std::endl;
                quit_ = true;
                break;
            default:
                break;
        }
    }

    // -- Call handling --

    void on_call_update(td_api::object_ptr<td_api::call> call) {
        auto call_id = call->id_;
        auto state_id = call->state_->get_id();

        if (state_id == td_api::callStatePending::ID) {
            if (!call->is_outgoing_ && active_call_id_ == 0) {
                // Incoming call — accept it
                active_call_id_ = call_id;
                active_call_outgoing_ = false;
                std::cout << "Incoming call #" << call_id << " from user " << call->user_id_ << " — accepting" << std::endl;

                auto versions = tgcalls::Meta::Versions();
                auto protocol = td_api::make_object<td_api::callProtocol>(
                    true, true, 65, 92, std::move(versions));
                send_query(td_api::make_object<td_api::acceptCall>(call_id, std::move(protocol)), {});
            } else if (!call->is_outgoing_ && active_call_id_ != 0) {
                std::cout << "Rejecting call #" << call_id << " — already in a call" << std::endl;
                send_query(td_api::make_object<td_api::discardCall>(call_id, false, "", 0, false, 0), {});
            }
        } else if (state_id == td_api::callStateReady::ID) {
            if (call_id == active_call_id_) {
                auto &ready = static_cast<td_api::callStateReady &>(*call->state_);
                start_tgcalls(call_id, call->is_outgoing_, ready);
            }
        } else if (state_id == td_api::callStateDiscarded::ID ||
                   state_id == td_api::callStateError::ID ||
                   state_id == td_api::callStateHangingUp::ID) {
            if (call_id == active_call_id_) {
                std::cout << "Call #" << call_id << " ended" << std::endl;
                stop_tgcalls();
            }
        }
    }

    void on_signaling_data(int32_t call_id, const std::string &data) {
        if (call_id == active_call_id_ && tgcalls_instance_) {
            std::vector<uint8_t> bytes(data.begin(), data.end());
            tgcalls_instance_->receiveSignalingData(bytes);
        }
    }

    void start_tgcalls(int32_t call_id, bool is_outgoing, td_api::callStateReady &ready) {
        std::cout << "Call #" << call_id << " ready — starting tgcalls" << std::endl;
        std::cout << "  servers: " << ready.servers_.size() << std::endl;
        std::cout << "  encryption_key: " << ready.encryption_key_.size() << " bytes" << std::endl;
        std::cout << "  allow_p2p: " << ready.allow_p2p_ << std::endl;

        // Pick version
        std::string version;
        if (ready.protocol_ && !ready.protocol_->library_versions_.empty()) {
            // Use the best matching version
            auto our_versions = tgcalls::Meta::Versions();
            for (const auto &theirs : ready.protocol_->library_versions_) {
                for (const auto &ours : our_versions) {
                    if (theirs == ours) {
                        version = ours;
                        break;
                    }
                }
                if (!version.empty()) break;
            }
            if (version.empty()) {
                version = our_versions.back();
            }
        } else {
            version = tgcalls::Meta::Versions().back();
        }
        std::cout << "  using tgcalls version: " << version << std::endl;

        // Map servers
        std::vector<tgcalls::Endpoint> endpoints;
        std::vector<tgcalls::RtcServer> rtc_servers;
        std::vector<int64_t> reflector_ids;

        for (const auto &srv : ready.servers_) {
            auto type_id = srv->type_->get_id();

            if (type_id == td_api::callServerTypeTelegramReflector::ID) {
                const auto &ref = static_cast<const td_api::callServerTypeTelegramReflector &>(*srv->type_);

                tgcalls::Endpoint ep{};
                ep.endpointId = srv->id_;
                ep.host = {srv->ip_address_, srv->ipv6_address_};
                ep.port = static_cast<uint16_t>(srv->port_);
                ep.type = ref.is_tcp_ ? tgcalls::EndpointType::TcpRelay : tgcalls::EndpointType::UdpRelay;
                if (ref.peer_tag_.size() >= 16) {
                    std::memcpy(ep.peerTag, ref.peer_tag_.data(), 16);
                }
                endpoints.push_back(ep);
                reflector_ids.push_back(srv->id_);
            } else if (type_id == td_api::callServerTypeWebrtc::ID) {
                const auto &webrtc = static_cast<const td_api::callServerTypeWebrtc &>(*srv->type_);

                tgcalls::RtcServer rtc{};
                rtc.id = static_cast<uint8_t>(srv->id_);
                rtc.host = srv->ip_address_.empty() ? srv->ipv6_address_ : srv->ip_address_;
                rtc.port = static_cast<uint16_t>(srv->port_);
                rtc.login = webrtc.username_;
                rtc.password = webrtc.password_;
                rtc.isTurn = webrtc.supports_turn_;
                rtc_servers.push_back(rtc);
            }
        }

        // Add reflectors as RtcServers too (like Telegram-X does)
        std::sort(reflector_ids.begin(), reflector_ids.end());
        for (const auto &srv : ready.servers_) {
            if (srv->type_->get_id() == td_api::callServerTypeTelegramReflector::ID) {
                const auto &ref = static_cast<const td_api::callServerTypeTelegramReflector &>(*srv->type_);
                auto itr = std::find(reflector_ids.begin(), reflector_ids.end(), srv->id_);
                size_t reflector_id = itr - reflector_ids.begin() + 1;

                tgcalls::RtcServer rtc{};
                rtc.id = static_cast<uint8_t>(reflector_id);
                rtc.host = srv->ip_address_.empty() ? srv->ipv6_address_ : srv->ip_address_;
                rtc.port = static_cast<uint16_t>(srv->port_);
                rtc.login = "reflector";
                rtc.password = hex_string(ref.peer_tag_);
                rtc.isTurn = true;
                rtc.isTcp = ref.is_tcp_;
                rtc_servers.push_back(rtc);
            }
        }

        // Encryption key
        auto key = std::make_shared<std::array<uint8_t, tgcalls::EncryptionKey::kSize>>();
        if (ready.encryption_key_.size() >= tgcalls::EncryptionKey::kSize) {
            std::memcpy(key->data(), ready.encryption_key_.data(), tgcalls::EncryptionKey::kSize);
        }

        // Audio: file player + recorder
        player_ = std::make_shared<FilePlayer>(prompt_file_);
        std::string rec_path = recordings_dir_ + "/recording_" + timestamp_string() + ".mp3";
        recorder_ = std::make_shared<FileRecorder>(rec_path);

        int max_layer = ready.protocol_ ? ready.protocol_->max_layer_ : 92;

        // Build descriptor
        tgcalls::Descriptor descriptor{
            .version = version,
            .config = tgcalls::Config{
                .initializationTimeout = 30.0,
                .receiveTimeout = 30.0,
                .dataSaving = tgcalls::DataSaving::Never,
                .enableP2P = ready.allow_p2p_,
                .allowTCP = false,
                .enableStunMarking = false,
                .enableAEC = false,
                .enableNS = false,
                .enableAGC = false,
                .enableCallUpgrade = false,
                .enableVolumeControl = false,
                .maxApiLayer = max_layer,
                .enableHighBitrateVideo = false,
                .customParameters = ready.custom_parameters_
            },
            .endpoints = std::move(endpoints),
            .rtcServers = std::move(rtc_servers),
            .initialNetworkType = tgcalls::NetworkType::WiFi,
            .encryptionKey = tgcalls::EncryptionKey(std::move(key), is_outgoing),
            .videoCapture = nullptr,
            .stateUpdated = [this](tgcalls::State state) {
                const char *names[] = {"WaitInit", "WaitInitAck", "Established", "Failed", "Reconnecting"};
                std::cout << "tgcalls state: " << names[static_cast<int>(state)] << std::endl;
            },
            .signalingDataEmitted = [this, call_id](const std::vector<uint8_t> &data) {
                std::string bytes(data.begin(), data.end());
                send_query(td_api::make_object<td_api::sendCallSignalingData>(call_id, std::move(bytes)), {});
            },
            .createAudioDeviceModule = tgcalls::FakeAudioDeviceModule::Creator(
                recorder_,   // Renderer — receives caller's audio
                player_,     // Recorder — provides our audio to send
                tgcalls::FakeAudioDeviceModule::Options{
                    .samples_per_sec = 48000,
                    .num_channels = 2
                }
            ),
        };

        tgcalls_instance_ = tgcalls::Meta::Create(version, std::move(descriptor));
        if (tgcalls_instance_) {
            tgcalls_instance_->setNetworkType(tgcalls::NetworkType::WiFi);
            tgcalls_instance_->setMuteMicrophone(false);
            std::cout << "tgcalls instance created" << std::endl;
        } else {
            std::cerr << "Failed to create tgcalls instance for version: " << version << std::endl;
        }
    }

    void stop_tgcalls() {
        if (tgcalls_instance_) {
            tgcalls_instance_->stop([](tgcalls::FinalState) {});
            tgcalls_instance_.reset();
        }
        player_.reset();
        recorder_.reset();
        active_call_id_ = 0;
        std::cout << "Waiting for next call..." << std::endl;
    }
};

// -- Main --

int main(int argc, char *argv[]) {
    // Defaults
    int32_t api_id = 0;
    std::string api_hash;
    std::string prompt_file = "prompt.raw";
    std::string recordings_dir = "recordings";

    // Parse args
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--api-id" && i + 1 < argc) {
            api_id = std::stoi(argv[++i]);
        } else if (arg == "--api-hash" && i + 1 < argc) {
            api_hash = argv[++i];
        } else if (arg == "--prompt" && i + 1 < argc) {
            prompt_file = argv[++i];
        } else if (arg == "--recordings-dir" && i + 1 < argc) {
            recordings_dir = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " --api-id ID --api-hash HASH [--prompt FILE] [--recordings-dir DIR]" << std::endl;
            return 0;
        }
    }

    // Env var fallback
    if (api_id == 0) {
        const char *env = std::getenv("API_ID");
        if (env) api_id = std::stoi(env);
    }
    if (api_hash.empty()) {
        const char *env = std::getenv("API_HASH");
        if (env) api_hash = env;
    }

    if (api_id == 0 || api_hash.empty()) {
        std::cerr << "API_ID and API_HASH required (via --api-id/--api-hash or env vars)" << std::endl;
        return 1;
    }

    // Ensure recordings dir exists
    std::string mkdir_cmd = "mkdir -p " + recordings_dir;
    system(mkdir_cmd.c_str());

    CallService service(api_id, api_hash, prompt_file, recordings_dir);
    service.run();
    return 0;
}

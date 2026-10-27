// Adapted from endpoint/peer.cc and endpoint/vpl_encoder.cc at the rb pin.
#ifdef UNSAFE_BUFFERS_BUILD
#pragma allow_unsafe_buffers
#pragma allow_unsafe_libc_calls
#endif
#include "re_native/include/re_native_rtc.h"

#include <algorithm>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/string_split.h"
#include "base/time/time.h"
#include "re_native/rtc/latency_stats.h"
#include "re_native/runtime.h"
#include "third_party/webrtc/api/audio_codecs/audio_decoder_factory_template.h"
#include "third_party/webrtc/api/audio_codecs/audio_encoder_factory_template.h"
#include "third_party/webrtc/api/audio_codecs/opus/audio_decoder_opus.h"
#include "third_party/webrtc/api/audio_codecs/opus/audio_encoder_opus.h"
#include "third_party/webrtc/api/create_modular_peer_connection_factory.h"
#include "third_party/webrtc/api/enable_media.h"
#include "third_party/webrtc/api/environment/environment_factory.h"
#include "third_party/webrtc/api/field_trials_view.h"
#include "third_party/webrtc/api/make_ref_counted.h"
#include "third_party/webrtc/api/peer_connection_interface.h"
#include "third_party/webrtc/api/set_local_description_observer_interface.h"
#include "third_party/webrtc/api/set_remote_description_observer_interface.h"
#include "third_party/webrtc/api/stats/rtc_stats_collector_callback.h"
#include "third_party/webrtc/api/video/adapted_video_track_source.h"
#include "third_party/webrtc/api/video/encoded_image.h"
#include "third_party/webrtc/api/video_codecs/builtin_video_decoder_factory.h"
#include "third_party/webrtc/api/video_codecs/video_encoder_factory.h"
#include "third_party/webrtc/modules/audio_device/include/audio_device_default.h"  // nogncheck
#include "third_party/webrtc/modules/video_coding/include/video_codec_interface.h"
#include "third_party/webrtc/modules/video_coding/include/video_error_codes.h"
#include "third_party/webrtc/rtc_base/ssl_adapter.h"
#include "third_party/webrtc/rtc_base/thread.h"
#include "third_party/webrtc/rtc_base/win32_socket_init.h"  // nogncheck
#include "third_party/webrtc_overrides/environment.h"
#include "third_party/webrtc_overrides/init_webrtc.h"

static_assert(sizeof(re_rtc_config) == 64);
static_assert(sizeof(re_rtc_frame) == 80);
static_assert(sizeof(re_rtc_callbacks) == 96);
static_assert(offsetof(re_rtc_frame, timestamp_us) == 24);
static_assert(offsetof(re_rtc_callbacks, on_frame) == 88);

namespace re::native {
namespace {
constexpr size_t kMaxFrameBytes = 16 * 1024 * 1024;
int64_t NowUs() {
  return TraceUs(base::TimeTicks::Now());
}
base::TimeTicks Ticks(int64_t us) {
  return base::TimeTicks() + base::Microseconds(us);
}
struct WallClock {
  const base::TimeTicks ticks = base::TimeTicks::Now();
  const double wall = base::Time::Now().InMillisecondsFSinceUnixEpoch();
};
double WallMs() {
  static const WallClock clock;
  return clock.wall + (base::TimeTicks::Now() - clock.ticks).InMillisecondsF();
}
bool InitRtc() {
  EnsureRuntime();
  static const bool initialized = [] {
    // Winsock and SSL live as long as the DLL, shared by every session.
    static base::NoDestructor<webrtc::WinsockInitializer> winsock;
    return winsock->error() == 0 && InitializeWebRtcModuleBeforeSandbox() &&
           webrtc::InitializeSSL();
  }();
  return initialized;
}
webrtc::SdpVideoFormat H264Format() {
  // SDP deliberately retains level 3.1 + level-asymmetry, as the legacy peer.
  // The externally encoded SPS carries the honest level (4.2 at 1080p60).
  return {"H264",
          {{"profile-level-id", "42e01f"},
           {"packetization-mode", "1"},
           {"level-asymmetry-allowed", "1"}}};
}
class Trials : public webrtc::FieldTrialsView {
 public:
  bool Enable(const char* name) {
    auto [it, inserted] = values_.emplace(name, "Enabled");
    return inserted || it->second.starts_with("Enabled");
  }
  bool Parse(std::string_view text) {
    while (!text.empty()) {
      size_t end = text.find('/');
      if (end == std::string_view::npos || end == 0)
        return false;
      std::string name(text.substr(0, end));
      text.remove_prefix(end + 1);
      end = text.find('/');
      if (end == std::string_view::npos || end == 0)
        return false;
      if (!values_.emplace(name, text.substr(0, end)).second)
        return false;
      text.remove_prefix(end + 1);
    }
    // Dropping an already encoded reference invalidates the external encoder
    // chain. Rate control and raw-frame drops belong before Rust's encoder.
    values_["WebRTC-FrameDropper"] = "Disabled";
    return true;
  }
  std::string Lookup(absl::string_view key) const override {
    auto it = values_.find(std::string(key));
    return it == values_.end() ? std::string() : it->second;
  }
  std::unique_ptr<webrtc::FieldTrialsView> CreateCopy() const override {
    return std::make_unique<Trials>(*this);
  }

 private:
  std::map<std::string, std::string> values_;
};
struct Shared {
  re_rtc_callbacks cb;
  re_rtc_config config;
  std::atomic<bool> active{true};
  std::atomic<uint64_t> submitted{0}, handed{0}, suppressed{0};
  void Keyframe() {
    if (active && cb.on_keyframe)
      cb.on_keyframe(cb.user);
  }
};
class EncodedBuffer : public webrtc::VideoFrameBuffer {
 public:
  EncodedBuffer(const re_rtc_frame& f, uint64_t seq)
      : metadata(f),
        sequence(seq),
        bytes(webrtc::EncodedImageBuffer::Create(f.annex_b, f.size)) {
    metadata.annex_b = nullptr;
  }
  Type type() const override { return Type::kNative; }
  int width() const override { return metadata.width; }
  int height() const override { return metadata.height; }
  webrtc::scoped_refptr<webrtc::I420BufferInterface> ToI420() override {
    return nullptr;  // No raw pixels exist in an encoded frame.
  }
  re_rtc_frame metadata;
  const uint64_t sequence;
  const webrtc::scoped_refptr<webrtc::EncodedImageBuffer> bytes;
};
class PassthroughEncoder : public webrtc::VideoEncoder {
 public:
  explicit PassthroughEncoder(std::shared_ptr<Shared> state)
      : state_(std::move(state)) {}
  int InitEncode(const webrtc::VideoCodec*, const Settings&) override {
    last_sequence_ = 0;
    need_idr_ = true;
    state_->Keyframe();
    return WEBRTC_VIDEO_CODEC_OK;
  }
  int32_t RegisterEncodeCompleteCallback(
      webrtc::EncodedImageCallback* cb) override {
    callback_ = cb;
    return WEBRTC_VIDEO_CODEC_OK;
  }
  int32_t Release() override {
    callback_ = nullptr;
    return WEBRTC_VIDEO_CODEC_OK;
  }
  int32_t Encode(const webrtc::VideoFrame& frame,
                 const std::vector<webrtc::VideoFrameType>* types) override {
    if (!callback_ || frame.video_frame_buffer()->type() !=
                          webrtc::VideoFrameBuffer::Type::kNative)
      return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
    // This source and factory are private to this session; no foreign native
    // frame can reach here (same private-source contract as legacy VPL).
    const auto& b =
        *static_cast<const EncodedBuffer*>(frame.video_frame_buffer().get());
    const auto& f = b.metadata;
    if ((last_sequence_ && b.sequence != last_sequence_ + 1) ||
        (types &&
         std::ranges::find(*types, webrtc::VideoFrameType::kVideoFrameKey) !=
             types->end())) {
      state_->Keyframe();
      if (last_sequence_ && b.sequence != last_sequence_ + 1)
        need_idr_ = true;
    }
    last_sequence_ = b.sequence;
    if (f.keyframe)
      need_idr_ = false;
    if (need_idr_) {
      ++state_->suppressed;
      state_->Keyframe();
      callback_->OnFrameDropped(frame.rtp_timestamp(), 0, true);
      return WEBRTC_VIDEO_CODEC_OK;
    }
    webrtc::EncodedImage output;
    output.SetEncodedData(b.bytes);
    output._encodedWidth = f.width;
    output._encodedHeight = f.height;
    output.SetRtpTimestamp(frame.rtp_timestamp());
    output.capture_time_ms_ = frame.timestamp_us() / 1000;
    output.ntp_time_ms_ = frame.ntp_time_ms();
    output.rotation_ = frame.rotation();
    output.SetSimulcastIndex(0);
    output.set_end_of_temporal_unit(true);
    output.set_frame_type(f.keyframe
                              ? webrtc::VideoFrameType::kVideoFrameKey
                              : webrtc::VideoFrameType::kVideoFrameDelta);
    output.content_type_ = webrtc::VideoContentType::SCREENSHARE;
    output.qp_ = -1;
    if (state_->config.playout_delay && state_->config.playout_metadata)
      output.SetPlayoutDelay(webrtc::VideoPlayoutDelay(
          webrtc::TimeDelta::Millis(state_->config.playout_min_ms),
          webrtc::TimeDelta::Millis(state_->config.playout_max_ms)));
    if (f.encode_begin_us && f.encode_end_us)
      output.SetEncodeTime(f.encode_begin_us / 1000, f.encode_end_us / 1000);
    webrtc::CodecSpecificInfo info;
    info.codecType = webrtc::kVideoCodecH264;
    info.codecSpecific.H264.packetization_mode =
        webrtc::H264PacketizationMode::NonInterleaved;
    info.codecSpecific.H264.temporal_idx = webrtc::kNoTemporalIdx;
    info.codecSpecific.H264.base_layer_sync = false;
    info.codecSpecific.H264.idr_frame = f.keyframe != 0;
    const auto handed = base::TimeTicks::Now();
    auto& stats = LatencyStats::Get();
    stats.Add("capture", Ticks(f.capture_begin_us), Ticks(f.capture_end_us));
    stats.Add("encode", Ticks(f.encode_begin_us), Ticks(f.encode_end_us));
    stats.Add("encoded_to_handoff", Ticks(f.encode_end_us), handed);
    stats.Add("capture_to_encoded", Ticks(f.capture_end_us),
              Ticks(f.encode_end_us));
    if (TraceEnabled()) {
      LOG(INFO) << "PRTS TR E ce=" << f.timestamp_us
                << " rtp=" << frame.rtp_timestamp()
                << " t0=" << f.encode_begin_us;
      LOG(INFO) << "PRTS TR X rtp=" << frame.rtp_timestamp()
                << " t1=" << f.encode_end_us << " bytes=" << f.size
                << " key=" << f.keyframe;
    }
    auto result = callback_->OnEncodedImage(output, &info);
    stats.Add("handoff_callback", handed, base::TimeTicks::Now());
    ++state_->handed;
    if (state_->active && state_->cb.on_frame)
      state_->cb.on_frame(state_->cb.user, f.timestamp_us,
                          frame.rtp_timestamp(), TraceUs(handed), f.size,
                          f.keyframe);
    if (result.error != webrtc::EncodedImageCallback::Result::OK) {
      need_idr_ = true;
      state_->Keyframe();
      return WEBRTC_VIDEO_CODEC_ERROR;
    }
    return WEBRTC_VIDEO_CODEC_OK;
  }
  void SetRates(const RateControlParameters& p) override {
    if (state_->active && state_->cb.on_rates)
      state_->cb.on_rates(state_->cb.user, p.bitrate.get_sum_bps(),
                          p.framerate_fps);
  }
  EncoderInfo GetEncoderInfo() const override {
    EncoderInfo info;
    info.implementation_name = "re external H264";
    info.supports_native_handle = true;
    info.has_trusted_rate_controller = false;
    info.is_qp_trusted = false;
    info.scaling_settings = ScalingSettings::kOff;
    info.requested_resolution_alignment = 2;
    info.max_pixels_per_frame = 1920 * 1080;
    info.resolution_bitrate_limits.emplace_back(
        1920 * 1080, state_->config.min_kbps * 1000,
        state_->config.min_kbps * 1000, state_->config.max_kbps * 1000);
    return info;
  }

 private:
  std::shared_ptr<Shared> state_;
  webrtc::EncodedImageCallback* callback_ = nullptr;
  uint64_t last_sequence_ = 0;
  bool need_idr_ = true;
};
class EncoderFactory : public webrtc::VideoEncoderFactory {
 public:
  explicit EncoderFactory(std::shared_ptr<Shared> state)
      : state_(std::move(state)) {}
  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
    return {H264Format()};
  }
  std::unique_ptr<webrtc::VideoEncoder> Create(
      const webrtc::Environment&,
      const webrtc::SdpVideoFormat& format) override {
    if (!format.IsSameCodec(H264Format()))
      return nullptr;
    return std::make_unique<PassthroughEncoder>(state_);
  }

 private:
  std::shared_ptr<Shared> state_;
};
class TrackSource : public webrtc::AdaptedVideoTrackSource {
 public:
  SourceState state() const override { return kLive; }
  bool remote() const override { return false; }
  bool is_screencast() const override { return true; }
  std::optional<bool> needs_denoising() const override { return false; }
  void Push(const webrtc::VideoFrame& frame) { OnFrame(frame); }
};
class OfferObserver : public webrtc::CreateSessionDescriptionObserver {
 public:
  using Callback =
      std::function<void(std::unique_ptr<webrtc::SessionDescriptionInterface>,
                         webrtc::RTCError)>;
  explicit OfferObserver(Callback cb) : cb_(std::move(cb)) {}
  void OnSuccess(webrtc::SessionDescriptionInterface* description) override {
    cb_(std::unique_ptr<webrtc::SessionDescriptionInterface>(description),
        webrtc::RTCError::OK());
  }
  void OnFailure(webrtc::RTCError e) override { cb_(nullptr, std::move(e)); }

 private:
  Callback cb_;
};
class LocalObserver : public webrtc::SetLocalDescriptionObserverInterface {
 public:
  explicit LocalObserver(std::function<void(webrtc::RTCError)> cb)
      : cb_(std::move(cb)) {}
  void OnSetLocalDescriptionComplete(webrtc::RTCError e) override {
    cb_(std::move(e));
  }

 private:
  std::function<void(webrtc::RTCError)> cb_;
};
class RemoteObserver : public webrtc::SetRemoteDescriptionObserverInterface {
 public:
  explicit RemoteObserver(std::function<void(webrtc::RTCError)> cb)
      : cb_(std::move(cb)) {}
  void OnSetRemoteDescriptionComplete(webrtc::RTCError e) override {
    cb_(std::move(e));
  }

 private:
  std::function<void(webrtc::RTCError)> cb_;
};
class Session : public webrtc::PeerConnectionObserver,
                public std::enable_shared_from_this<Session> {
 public:
  Session(webrtc::Thread& thread, std::shared_ptr<Shared> state)
      : thread_(thread), state_(std::move(state)) {}
  template <typename F>
  void Post(F task) {
    thread_.PostTask(
        [weak = weak_from_this(), task = std::move(task)]() mutable {
          if (auto s = weak.lock(); s && !s->closed_)
            task(*s);
        });
  }
  void Error(std::string_view message) {
    LOG(ERROR) << "PRTS RTC " << message;
    if (state_->active && state_->cb.on_error) {
      std::string copy(message);
      state_->cb.on_error(state_->cb.user, copy.c_str());
    }
  }
  bool Check(webrtc::RTCError e) {
    if (e.ok())
      return true;
    Error(e.message());
    return false;
  }
  bool Start(webrtc::PeerConnectionFactoryInterface& factory,
             const std::string& servers) {
    webrtc::PeerConnectionInterface::RTCConfiguration config;
    config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
    config.bundle_policy =
        webrtc::PeerConnectionInterface::kBundlePolicyMaxBundle;
    if (state_->config.relay_only)
      config.type = webrtc::PeerConnectionInterface::kRelay;
    auto list = base::JSONReader::Read(servers, base::JSON_PARSE_RFC);
    if (!list || !list->is_list()) {
      Error("ice_servers_json must be an array");
      return false;
    }
    for (const auto& value : list->GetList()) {
      if (!value.is_dict()) {
        Error("invalid ICE server");
        return false;
      }
      const auto& item = value.GetDict();
      webrtc::PeerConnectionInterface::IceServer server;
      if (const auto* url = item.FindString("urls"))
        server.urls.push_back(*url);
      if (const auto* urls = item.FindList("urls"))
        for (const auto& url : *urls)
          if (url.is_string())
            server.urls.push_back(url.GetString());
      if (const auto* v = item.FindString("username"))
        server.username = *v;
      if (const auto* v = item.FindString("credential"))
        server.password = *v;
      if (server.urls.empty()) {
        Error("ICE server has no urls");
        return false;
      }
      config.servers.push_back(std::move(server));
    }
    auto result = factory.CreatePeerConnectionOrError(
        config, webrtc::PeerConnectionDependencies(this));
    if (!result.ok()) {
      Error(result.error().message());
      return false;
    }
    pc_ = result.MoveValue();
    source_ = webrtc::make_ref_counted<TrackSource>();
    auto track = factory.CreateVideoTrack(source_, "screen");
    track->set_content_hint(webrtc::VideoTrackInterface::ContentHint::kFluid);
    webrtc::RtpTransceiverInit init;
    init.direction = webrtc::RtpTransceiverDirection::kSendOnly;
    init.stream_ids = {"rb"};
    init.send_encodings.emplace_back();
    auto tr = pc_->AddTransceiver(track, init);
    if (!tr.ok()) {
      Error(tr.error().message());
      return false;
    }
    transceiver_ = tr.MoveValue();
    std::vector<webrtc::RtpCodecCapability> codecs;
    for (const auto& codec :
         factory.GetRtpSenderCapabilities(webrtc::MediaType::VIDEO).codecs)
      if ((codec.name == "H264" &&
           codec.parameters == H264Format().parameters) ||
          codec.name == "rtx" ||
          (state_->config.fec == 1 && (codec.name == "red" || codec.name == "ulpfec")) ||
          (state_->config.fec == 2 && codec.name == "flexfec-03"))
        codecs.push_back(codec);
    if (!Check(transceiver_->SetCodecPreferences(codecs)))
      return false;
    auto extensions = transceiver_->GetHeaderExtensionsToNegotiate();
    bool found = false;
    for (auto& ext : extensions) {
      if (ext.uri != webrtc::RtpExtension::kPlayoutDelayUri)
        continue;
      ext.direction = state_->config.playout_delay
                          ? webrtc::RtpTransceiverDirection::kSendRecv
                          : webrtc::RtpTransceiverDirection::kStopped;
      found = true;
    }
    if (!found && state_->config.playout_delay) {
      Error("playout-delay capability missing");
      return false;
    }
    if (!Check(transceiver_->SetHeaderExtensionsToNegotiate(extensions)))
      return false;
    auto params = transceiver_->sender()->GetParameters();
    if (params.encodings.empty()) {
      Error("sender has no encoding");
      return false;
    }
    params.encodings[0].min_bitrate_bps = state_->config.min_kbps * 1000;
    params.encodings[0].max_bitrate_bps = state_->config.max_kbps * 1000;
    params.encodings[0].max_framerate = state_->config.fps;
    params.degradation_preference =
        webrtc::DegradationPreference::MAINTAIN_RESOLUTION;
    if (!Check(transceiver_->sender()->SetParameters(params)))
      return false;
    webrtc::BitrateSettings bitrate;
    bitrate.min_bitrate_bps = state_->config.min_kbps * 1000;
    bitrate.start_bitrate_bps = state_->config.start_kbps * 1000;
    bitrate.max_bitrate_bps = state_->config.max_kbps * 1000;
    if (!Check(pc_->SetBitrate(bitrate)))
      return false;
    return CreateChannel(0, "input", true, -1) == 0 &&
           CreateChannel(1, "ctl", true, -1) == 0 &&
           CreateChannel(2, "probe", false, 0) == 0;
  }
  void Close() {
    closed_ = true;
    channels_.clear();
    if (pc_)
      pc_->Close();
    transceiver_ = nullptr;
    source_ = nullptr;
    pc_ = nullptr;
  }
  re_native_result Describe(bool offer, bool restart) {
    if (closed_ || description_pending_)
      return RE_NATIVE_INVALID_STATE;
    description_pending_ = true;
    if (offer)
      remote_ready_ = false;
    auto observer = webrtc::make_ref_counted<OfferObserver>(
        [weak = weak_from_this()](auto description, webrtc::RTCError e) {
          if (auto s = weak.lock())
            s->Post([description = std::move(description),
                     e = std::move(e)](Session& s) mutable {
              if (!s.Check(std::move(e))) {
                s.description_pending_ = false;
                return;
              }
              if (s.state_->config.fec == 1) {
                std::string original;
                description->ToString(&original);
                std::string filtered;
                for (const auto& line : base::SplitString(
                         original, "\n", base::TRIM_WHITESPACE,
                         base::SPLIT_WANT_NONEMPTY)) {
                  if (line.starts_with("a=rtcp-fb:") && line.ends_with(" nack"))
                    continue;
                  filtered += line + "\r\n";
                }
                description = webrtc::CreateSessionDescription(
                    description->GetType(), filtered);
                if (!description) {
                  s.Error("invalid ULPFEC local SDP");
                  return;
                }
                LOG(INFO) << "PRTS ULPFEC generic_nack=off pli_fir=preserved";
              }
              s.pc_->SetLocalDescription(
                  std::move(description),
                  webrtc::make_ref_counted<LocalObserver>(
                      [weak = s.weak_from_this()](webrtc::RTCError e) {
                        if (auto s = weak.lock())
                          s->Post([e = std::move(e)](Session& s) mutable {
                            s.description_pending_ = false;
                            if (!s.Check(std::move(e)))
                              return;
                            const auto* description =
                                s.pc_->local_description();
                            std::string sdp;
                            if (!description || !description->ToString(&sdp)) {
                              s.Error("empty local SDP");
                              return;
                            }
                            if (s.state_->cb.on_sdp)
                              s.state_->cb.on_sdp(s.state_->cb.user,
                                                  description->type().c_str(),
                                                  sdp.c_str());
                          });
                      }));
            });
        });
    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
    options.ice_restart = restart;
    if (offer)
      pc_->CreateOffer(observer.get(), options);
    else
      pc_->CreateAnswer(observer.get(), options);
    return RE_NATIVE_OK;
  }
  re_native_result Remote(const std::string& kind, const std::string& sdp) {
    if (closed_ || remote_pending_ || (kind != "offer" && kind != "answer"))
      return RE_NATIVE_INVALID_STATE;
    auto description = webrtc::CreateSessionDescription(
        kind == "offer" ? webrtc::SdpType::kOffer : webrtc::SdpType::kAnswer,
        sdp);
    if (!description)
      return RE_NATIVE_INVALID_ARGUMENT;
    remote_pending_ = true;
    remote_ready_ = false;
    pc_->SetRemoteDescription(
        std::move(description),
        webrtc::make_ref_counted<RemoteObserver>([weak = weak_from_this()](
                                                     webrtc::RTCError e) {
          if (auto s = weak.lock())
            s->Post([e = std::move(e)](Session& s) mutable {
              s.remote_pending_ = false;
              if (!s.Check(std::move(e)))
                return;
              s.remote_ready_ = true;
              bool delay = false;
              for (const auto& ext :
                   s.transceiver_->GetNegotiatedHeaderExtensions())
                delay |=
                    ext.uri == webrtc::RtpExtension::kPlayoutDelayUri &&
                    ext.direction != webrtc::RtpTransceiverDirection::kStopped;
              bool red = false, ulpfec = false, flexfec = false;
              std::string codecs;
              for (const auto& codec : s.transceiver_->sender()->GetParameters().codecs) {
                codecs += codec.name + ',';
                red |= codec.name == "red";
                ulpfec |= codec.name == "ulpfec";
                flexfec |= codec.name == "flexfec-03";
              }
              const int fec = s.state_->config.fec;
              const bool fec_ok = fec == 0 || (fec == 1 && red && ulpfec) ||
                                  (fec == 2 && flexfec);
              LOG(INFO) << "PRTS peer negotiated fec="
                        << (fec == 1 ? "ulpfec" : fec == 2 ? "flexfec" : "off")
                        << " fec_negotiated=" << fec_ok << " codecs=" << codecs
                        << " playout_extension=" << delay;
              if (!fec_ok)
                s.Error("answer omitted requested FEC codec(s)");
              if (s.state_->config.playout_delay && !delay &&
                  s.pc_->remote_description()->GetType() ==
                      webrtc::SdpType::kAnswer)
                s.Error("answer omitted playout-delay");
              for (auto& ice : s.ice_)
                s.pc_->AddIceCandidate(ice.get());
              s.ice_.clear();
            });
        }));
    return RE_NATIVE_OK;
  }
  re_native_result Ice(const std::string& mid,
                       int mline,
                       const std::string& candidate) {
    if (closed_)
      return RE_NATIVE_INVALID_STATE;
    if (candidate.empty())
      return RE_NATIVE_OK;
    auto ice = webrtc::IceCandidate::Create(mid, mline, candidate);
    if (!ice)
      return RE_NATIVE_INVALID_ARGUMENT;
    if (!remote_ready_) {
      if (ice_.size() >= 512)
        return RE_NATIVE_BACKPRESSURE;
      ice_.push_back(std::move(ice));
      return RE_NATIVE_OK;
    }
    return pc_->AddIceCandidate(ice.get()) ? RE_NATIVE_OK
                                           : RE_NATIVE_INVALID_ARGUMENT;
  }
  re_native_result Submit(const re_rtc_frame& frame) {
    if (closed_)
      return RE_NATIVE_INVALID_STATE;
    if (frame.timestamp_us <= last_timestamp_)
      return RE_NATIVE_INVALID_ARGUMENT;
    last_timestamp_ = frame.timestamp_us;
    auto buffer =
        webrtc::make_ref_counted<EncodedBuffer>(frame, ++state_->submitted);
    source_->Push(webrtc::VideoFrame::Builder()
                      .set_video_frame_buffer(buffer)
                      .set_timestamp_us(frame.timestamp_us)
                      .build());
    return RE_NATIVE_OK;
  }
  re_native_result CreateChannel(int id,
                                 const std::string& label,
                                 bool ordered,
                                 int retransmits) {
    if (closed_ || channels_.contains(id))
      return RE_NATIVE_INVALID_STATE;
    webrtc::DataChannelInit init;
    init.negotiated = true;
    init.id = id;
    init.ordered = ordered;
    if (retransmits >= 0)
      init.maxRetransmits = retransmits;
    auto result = pc_->CreateDataChannelOrError(label, &init);
    if (!result.ok()) {
      Error(result.error().message());
      return RE_NATIVE_INTERNAL_ERROR;
    }
    channels_[id] = std::make_unique<Channel>(*this, id, result.MoveValue());
    return RE_NATIVE_OK;
  }
  re_native_result Send(int id,
                        const uint8_t* bytes,
                        size_t size,
                        bool binary) {
    auto it = channels_.find(id);
    if (closed_ || it == channels_.end())
      return RE_NATIVE_INVALID_STATE;
    auto& channel = it->second->channel;
    if (channel->state() != webrtc::DataChannelInterface::kOpen ||
        channel->buffered_amount() > 65536)
      return RE_NATIVE_BACKPRESSURE;
    webrtc::CopyOnWriteBuffer buffer(bytes, size);
    return channel->Send(webrtc::DataBuffer(buffer, binary))
               ? RE_NATIVE_OK
               : RE_NATIVE_BACKPRESSURE;
  }
  void Keyframe() { state_->Keyframe(); }
  void Stats() {
    if (!closed_)
      pc_->GetStats(
          webrtc::make_ref_counted<StatsObserver>(weak_from_this()).get());
  }

 private:
  class StatsObserver : public webrtc::RTCStatsCollectorCallback {
   public:
    explicit StatsObserver(std::weak_ptr<Session> s) : session_(std::move(s)) {}
    void OnStatsDelivered(
        const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report)
        override {
      if (auto s = session_.lock())
        s->Post([json = report->ToJson()](Session& s) {
          const auto& state = *s.state_;
          const std::string snapshot =
              "{\"rtc\":" + json + ",\"native\":{\"submitted\":" +
              std::to_string(state.submitted.load()) +
              ",\"handed\":" + std::to_string(state.handed.load()) +
              ",\"suppressed\":" + std::to_string(state.suppressed.load()) +
              "}}";
          if (s.state_->cb.on_stats)
            s.state_->cb.on_stats(s.state_->cb.user, snapshot.c_str());
          LatencyStats::Get().Log();
        });
    }

   private:
    std::weak_ptr<Session> session_;
  };
  class Channel : public webrtc::DataChannelObserver {
   public:
    Channel(Session& s,
            int id,
            webrtc::scoped_refptr<webrtc::DataChannelInterface> c)
        : channel(std::move(c)), session_(s), id_(id) {
      channel->RegisterObserver(this);
    }
    ~Channel() override {
      channel->UnregisterObserver();
      channel->Close();
    }
    void OnStateChange() override {
      session_.Post(
          [id = id_, state = static_cast<int>(channel->state())](Session& s) {
            if (s.state_->cb.on_channel_state)
              s.state_->cb.on_channel_state(s.state_->cb.user, id, state);
          });
    }
    void OnMessage(const webrtc::DataBuffer& data) override {
      if (data.size() > 65536)
        return;
      const auto received = WallMs();
      const auto ticks = NowUs();
      session_.Post([id = id_, data, received, ticks](Session& s) {
        if (s.state_->cb.on_data)
          s.state_->cb.on_data(s.state_->cb.user, id, data.data.cdata(),
                               data.size(), data.binary, received, ticks);
      });
    }
    webrtc::scoped_refptr<webrtc::DataChannelInterface> channel;

   private:
    Session& session_;
    int id_;
  };
  void OnSignalingChange(
      webrtc::PeerConnectionInterface::SignalingState) override {}
  void OnDataChannel(
      webrtc::scoped_refptr<webrtc::DataChannelInterface> channel) override {
    channel->Close();
  }
  void OnIceGatheringChange(
      webrtc::PeerConnectionInterface::IceGatheringState state) override {
    if (state == webrtc::PeerConnectionInterface::kIceGatheringComplete)
      Post([](Session& s) {
        if (s.state_->cb.on_ice)
          s.state_->cb.on_ice(s.state_->cb.user, "", 0, nullptr);
      });
  }
  void OnIceCandidate(const webrtc::IceCandidate* candidate) override {
    Post([mid = candidate->sdp_mid(), line = candidate->sdp_mline_index(),
          text = candidate->ToString()](Session& s) {
      if (s.state_->cb.on_ice)
        s.state_->cb.on_ice(s.state_->cb.user, mid.c_str(), line, text.c_str());
    });
  }
  void OnConnectionChange(
      webrtc::PeerConnectionInterface::PeerConnectionState state) override {
    Post([state](Session& s) {
      if (s.state_->cb.on_state)
        s.state_->cb.on_state(s.state_->cb.user, static_cast<int>(state));
    });
  }
  webrtc::Thread& thread_;
  std::shared_ptr<Shared> state_;
  webrtc::scoped_refptr<webrtc::PeerConnectionInterface> pc_;
  webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver_;
  webrtc::scoped_refptr<TrackSource> source_;
  std::map<int, std::unique_ptr<Channel>> channels_;
  std::vector<std::unique_ptr<webrtc::IceCandidate>> ice_;
  int64_t last_timestamp_ = 0;
  bool closed_ = false, description_pending_ = false, remote_pending_ = false,
       remote_ready_ = false;
};
}  // namespace
LatencyStats& LatencyStats::Get() {
  static base::NoDestructor<LatencyStats> stats;
  return *stats;
}
}  // namespace re::native

struct re_rtc_session {
  std::shared_ptr<re::native::Shared> state;
  std::unique_ptr<webrtc::Thread> network, signaling;
  webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory;
  std::shared_ptr<re::native::Session> session;
  bool signaling_started = false;
  ~re_rtc_session() {
    if (state)
      state->active = false;
    if (signaling_started)
      signaling->BlockingCall([this] {
        if (session)
          session->Close();
        session.reset();
        factory = nullptr;
      });
    if (signaling)
      signaling->Stop();
    if (network)
      network->Stop();
  }
};
void re_rtc_default_config(re_rtc_config* c) {
  if (c)
    *c = {
        sizeof(*c), 2, 2000, 8000, 8000, 60, 1, 0, nullptr,
        nullptr, 1, 0, 0, 0};
}
re_native_result re_rtc_create(const re_rtc_config* c,
                               const re_rtc_callbacks* cb,
                               re_rtc_session** out) {
  if (!out)
    return RE_NATIVE_INVALID_ARGUMENT;
  *out = nullptr;
  if (!c || !cb || c->struct_size != sizeof(*c) ||
      cb->struct_size != sizeof(*cb) ||
      c->abi_version != 2 ||
      cb->abi_version != RE_NATIVE_ABI_VERSION)
    return RE_NATIVE_ABI_MISMATCH;
  if (c->min_kbps < 1 || c->min_kbps > c->start_kbps ||
      c->start_kbps > c->max_kbps || c->max_kbps > 100000 || c->fps < 1 ||
      c->fps > 120 || c->fec < 0 || c->fec > 2 ||
      c->playout_min_ms < 0 || c->playout_max_ms < c->playout_min_ms ||
      c->playout_max_ms > 40950 || c->playout_min_ms % 10 || c->playout_max_ms % 10)
    return RE_NATIVE_INVALID_ARGUMENT;
  auto trials = std::make_unique<re::native::Trials>();
  if (!trials->Parse(c->field_trials ? c->field_trials
                                     : "WebRTC-Video-Pacing/factor:2.5/"))
    return RE_NATIVE_INVALID_ARGUMENT;
  if (c->fec == 2 && (!trials->Enable("WebRTC-FlexFEC-03") ||
                      !trials->Enable("WebRTC-FlexFEC-03-Advertised")))
    return RE_NATIVE_INVALID_ARGUMENT;
  if (c->fec == 1 && trials->Lookup("WebRTC-DisableUlpFecExperiment").starts_with("Enabled"))
    return RE_NATIVE_INVALID_ARGUMENT;
  if (!re::native::InitRtc())
    return RE_NATIVE_INTERNAL_ERROR;
  auto s = std::make_unique<re_rtc_session>();
  s->state = std::make_shared<re::native::Shared>();
  s->state->cb = *cb;
  s->state->config = *c;
  s->state->config.field_trials = nullptr;
  s->state->config.ice_servers_json = nullptr;
  std::string servers = c->ice_servers_json ? c->ice_servers_json : "[]";
  s->network = webrtc::Thread::CreateWithSocketServer();
  s->signaling = webrtc::Thread::Create();
  if (!s->network->Start() || !s->signaling->Start())
    return RE_NATIVE_INTERNAL_ERROR;
  s->signaling_started = true;
  bool ok = s->signaling->BlockingCall([&] {
    webrtc::PeerConnectionFactoryDependencies deps;
    deps.network_thread = s->network.get();
    deps.signaling_thread = s->signaling.get();
    webrtc::EnvironmentFactory env(WebRtcEnvironment());
    env.Set(std::unique_ptr<const webrtc::FieldTrialsView>(std::move(trials)));
    deps.env = env.Create();
    using NoAudio = webrtc::webrtc_impl::AudioDeviceModuleDefault<
        webrtc::AudioDeviceModule>;
    deps.adm = webrtc::make_ref_counted<NoAudio>();
    deps.audio_encoder_factory =
        webrtc::CreateAudioEncoderFactory<webrtc::AudioEncoderOpus>();
    deps.audio_decoder_factory =
        webrtc::CreateAudioDecoderFactory<webrtc::AudioDecoderOpus>();
    deps.video_encoder_factory =
        std::make_unique<re::native::EncoderFactory>(s->state);
    deps.video_decoder_factory = webrtc::CreateBuiltinVideoDecoderFactory();
    webrtc::EnableMedia(deps);
    s->factory = webrtc::CreateModularPeerConnectionFactory(std::move(deps));
    if (!s->factory)
      return false;
    s->session = std::make_shared<re::native::Session>(*s->signaling, s->state);
    return s->session->Start(*s->factory, servers);
  });
  if (!ok)
    return RE_NATIVE_INTERNAL_ERROR;
  *out = s.release();
  return RE_NATIVE_OK;
}
void re_rtc_destroy(re_rtc_session* s) {
  delete s;
}
re_native_result re_rtc_offer(re_rtc_session* s, int32_t restart) {
  if (!s)
    return RE_NATIVE_INVALID_ARGUMENT;
  return s->signaling->BlockingCall(
      [&] { return s->session->Describe(true, restart != 0); });
}
re_native_result re_rtc_answer(re_rtc_session* s) {
  if (!s)
    return RE_NATIVE_INVALID_ARGUMENT;
  return s->signaling->BlockingCall(
      [&] { return s->session->Describe(false, false); });
}
re_native_result re_rtc_set_remote_description(re_rtc_session* s,
                                               const char* kind,
                                               const char* sdp) {
  if (!s || !kind || !sdp)
    return RE_NATIVE_INVALID_ARGUMENT;
  return s->signaling->BlockingCall(
      [&] { return s->session->Remote(kind, sdp); });
}
re_native_result re_rtc_add_ice(re_rtc_session* s,
                                const char* mid,
                                int32_t mline,
                                const char* candidate) {
  if (!s || mline < 0)
    return RE_NATIVE_INVALID_ARGUMENT;
  return s->signaling->BlockingCall([&] {
    return s->session->Ice(mid ? mid : "", mline, candidate ? candidate : "");
  });
}
re_native_result re_rtc_submit(re_rtc_session* s, const re_rtc_frame* f) {
  if (!s || !f)
    return RE_NATIVE_INVALID_ARGUMENT;
  if (f->struct_size != sizeof(*f) || f->abi_version != RE_NATIVE_ABI_VERSION)
    return RE_NATIVE_ABI_MISMATCH;
  if (!f->annex_b || f->size < 4 || f->size > re::native::kMaxFrameBytes ||
      f->timestamp_us <= 0 || f->width < 2 || f->height < 2 ||
      f->width > 4096 || f->height > 4096 || f->width % 2 || f->height % 2 ||
      uint64_t(f->width) * f->height > 1920 * 1080 || f->annex_b[0] != 0 ||
      f->annex_b[1] != 0 ||
      (f->annex_b[2] != 1 && (f->annex_b[2] != 0 || f->annex_b[3] != 1)))
    return RE_NATIVE_INVALID_ARGUMENT;
  return s->signaling->BlockingCall([&] { return s->session->Submit(*f); });
}
re_native_result re_rtc_create_channel(re_rtc_session* s,
                                       int32_t id,
                                       const char* label,
                                       int32_t ordered,
                                       int32_t retransmits) {
  if (!s || !label || id < 0 || id > 65534 || retransmits < -1 ||
      retransmits > 65535)
    return RE_NATIVE_INVALID_ARGUMENT;
  return s->signaling->BlockingCall([&] {
    return s->session->CreateChannel(id, label, ordered != 0, retransmits);
  });
}
re_native_result re_rtc_send(re_rtc_session* s,
                             int32_t id,
                             const uint8_t* data,
                             size_t size,
                             int32_t binary) {
  if (!s || (!data && size) || size > 65536)
    return RE_NATIVE_INVALID_ARGUMENT;
  return s->signaling->BlockingCall(
      [&] { return s->session->Send(id, data, size, binary != 0); });
}
re_native_result re_rtc_request_keyframe(re_rtc_session* s) {
  if (!s)
    return RE_NATIVE_INVALID_ARGUMENT;
  s->signaling->BlockingCall([&] { s->session->Keyframe(); });
  return RE_NATIVE_OK;
}
re_native_result re_rtc_stats(re_rtc_session* s) {
  if (!s)
    return RE_NATIVE_INVALID_ARGUMENT;
  s->signaling->BlockingCall([&] { s->session->Stats(); });
  return RE_NATIVE_OK;
}
int64_t re_rtc_now_us() {
  return re::native::NowUs();
}
double re_rtc_wall_ms() {
  return re::native::WallMs();
}

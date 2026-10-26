#ifdef UNSAFE_BUFFERS_BUILD
#pragma allow_unsafe_buffers
#pragma allow_unsafe_libc_calls
#endif

#include "prts_endpoint/peer.h"

#include <map>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/command_line.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/string_split.h"
#include "base/synchronization/lock.h"
#include "prts_endpoint/latency_stats.h"
#include "third_party/webrtc/api/audio_codecs/audio_decoder_factory_template.h"
#include "third_party/webrtc/api/audio_codecs/audio_encoder_factory_template.h"
#include "third_party/webrtc/api/audio_codecs/opus/audio_decoder_opus.h"
#include "third_party/webrtc/api/audio_codecs/opus/audio_encoder_opus.h"
#include "third_party/webrtc/api/create_modular_peer_connection_factory.h"
#include "third_party/webrtc/api/enable_media.h"
#include "third_party/webrtc/api/make_ref_counted.h"
#include "third_party/webrtc/api/set_local_description_observer_interface.h"
#include "third_party/webrtc/api/set_remote_description_observer_interface.h"
#include "third_party/webrtc/api/video/adapted_video_track_source.h"
#include "third_party/webrtc/api/video/encoded_image.h"
#include "third_party/webrtc/api/video_codecs/builtin_video_decoder_factory.h"
#include "third_party/webrtc/api/video_codecs/video_encoder_factory.h"
#include "third_party/webrtc/media/engine/internal_encoder_factory.h"
// Header-only default ADM; no standalone audio_device dependency is permitted
// by Chromium WebRTC visibility. Its API dependencies come from the component.
#include "third_party/webrtc/modules/audio_device/include/audio_device_default.h"  // nogncheck
#include "third_party/webrtc_overrides/environment.h"

namespace prts {
namespace {
std::string String(const base::DictValue& m, const char* key) {
  const auto* s = m.FindString(key);
  return s ? *s : std::string();
}
webrtc::SdpVideoFormat H264Format() {
  return {"H264", {{"profile-level-id", "42e01f"},
                   {"packetization-mode", "1"},
                   {"level-asymmetry-allowed", "1"}}};
}

// Decorate the stock encoder's output, as remoting does. VideoFrame has no
// playout delay setter at this pin; it belongs on EncodedImage.
class LowDelayEncoder : public webrtc::VideoEncoder,
                        public webrtc::EncodedImageCallback {
 public:
  explicit LowDelayEncoder(std::unique_ptr<webrtc::VideoEncoder> encoder)
      : encoder_(std::move(encoder)) {}
  int InitEncode(const webrtc::VideoCodec* codec,
                 const Settings& settings) override {
    LOG(INFO) << "PRTS encoder InitEncode " << codec->width << "x"
              << codec->height << " max_fps=" << codec->maxFramerate
              << " start_kbps=" << codec->startBitrate
              << " min_kbps=" << codec->minBitrate
              << " max_kbps=" << codec->maxBitrate
              << " cores=" << settings.number_of_cores
              << " impl=" << encoder_->GetEncoderInfo().implementation_name;
    auto screenshare = *codec;
    screenshare.mode = webrtc::VideoCodecMode::kScreensharing;
    return encoder_->InitEncode(&screenshare, settings);
  }
  int32_t RegisterEncodeCompleteCallback(
      webrtc::EncodedImageCallback* callback) override {
    callback_ = callback;
    return encoder_->RegisterEncodeCompleteCallback(callback ? this : nullptr);
  }
  int32_t Release() override { return encoder_->Release(); }
  int32_t Encode(const webrtc::VideoFrame& frame,
                 const std::vector<webrtc::VideoFrameType>* types) override {
    const auto now = base::TimeTicks::Now();
    const auto capture =
        base::TimeTicks() + base::Microseconds(frame.timestamp_us());
    LatencyStats::Get().Add("capture_to_encode", capture, now);
    LatencyStats::Get().EncodeStart(frame.timestamp_us(), now);
    {
      base::AutoLock lock(timing_lock_);
      timing_[frame.rtp_timestamp()] = {capture, now};
      if (timing_.size() > 240)
        timing_.erase(timing_.begin());
    }
    return encoder_->Encode(frame, types);
  }
  void SetRates(const RateControlParameters& p) override { encoder_->SetRates(p); }
  void SetFecControllerOverride(webrtc::FecControllerOverride* f) override {
    encoder_->SetFecControllerOverride(f);
  }
  void OnPacketLossRateUpdate(float loss) override {
    encoder_->OnPacketLossRateUpdate(loss);
  }
  void OnRttUpdate(int64_t rtt) override { encoder_->OnRttUpdate(rtt); }
  void OnLossNotification(const LossNotification& n) override {
    encoder_->OnLossNotification(n);
  }
  EncoderInfo GetEncoderInfo() const override { return encoder_->GetEncoderInfo(); }
  Result OnEncodedImage(const webrtc::EncodedImage& image,
                         const webrtc::CodecSpecificInfo* info) override {
    const auto encoded = base::TimeTicks::Now();
    {
      base::AutoLock lock(timing_lock_);
      auto it = timing_.find(image.RtpTimestamp());
      if (it != timing_.end()) {
        LatencyStats::Get().Add("encode", it->second.second, encoded);
        LatencyStats::Get().Add("capture_to_encoded", it->second.first, encoded);
        timing_.erase(it);
      }
    }
    auto output = image;  // Encoded buffer is refcounted, no payload copy.
    if (!base::CommandLine::ForCurrentProcess()->HasSwitch(
            "latency-no-playout-delay")) {
      output.SetPlayoutDelay(webrtc::VideoPlayoutDelay::Minimal());
    }
    output.content_type_ = webrtc::VideoContentType::SCREENSHARE;
    const auto handed = base::TimeTicks::Now();
    LatencyStats::Get().Add("encoded_to_handoff", encoded, handed);
    auto result = callback_->OnEncodedImage(output, info);
    LatencyStats::Get().Add("handoff_callback", handed, base::TimeTicks::Now());
    return result;
  }
  void OnFrameDropped(uint32_t ts, int spatial, bool end) override {
    callback_->OnFrameDropped(ts, spatial, end);
  }

 private:
  std::unique_ptr<webrtc::VideoEncoder> encoder_;
  raw_ptr<webrtc::EncodedImageCallback> callback_ = nullptr;
  base::Lock timing_lock_;
  std::map<uint32_t, std::pair<base::TimeTicks, base::TimeTicks>> timing_;
};

class EncoderFactory : public webrtc::VideoEncoderFactory {
 public:
  EncoderFactory()
      : builtin_(std::make_unique<webrtc::InternalEncoderFactory>()) {}
  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
    // Fail initialization if the build has no compatible OpenH264 encoder.
    for (const auto& f : builtin_->GetSupportedFormats()) {
      if (f.name == "H264" && f.parameters == H264Format().parameters)
        return {H264Format()};
    }
    return {};
  }
  std::unique_ptr<webrtc::VideoEncoder> Create(
      const webrtc::Environment& env,
      const webrtc::SdpVideoFormat& format) override {
    auto encoder = builtin_->Create(env, format);
    if (!encoder)
      return nullptr;
    return std::make_unique<LowDelayEncoder>(std::move(encoder));
  }

 private:
  std::unique_ptr<webrtc::VideoEncoderFactory> builtin_;
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
  using Callback = std::function<void(
      std::unique_ptr<webrtc::SessionDescriptionInterface>, webrtc::RTCError)>;
  explicit OfferObserver(Callback callback) : callback_(std::move(callback)) {}
  void OnSuccess(webrtc::SessionDescriptionInterface* description) override {
    callback_(std::unique_ptr<webrtc::SessionDescriptionInterface>(description),
              webrtc::RTCError::OK());
  }
  void OnFailure(webrtc::RTCError error) override {
    callback_(nullptr, std::move(error));
  }

 private:
  Callback callback_;
};
template <typename Interface>
class DescriptionObserver;
template <>
class DescriptionObserver<webrtc::SetLocalDescriptionObserverInterface>
    : public webrtc::SetLocalDescriptionObserverInterface {
 public:
  explicit DescriptionObserver(std::function<void(webrtc::RTCError)> callback)
      : callback_(std::move(callback)) {}
  void OnSetLocalDescriptionComplete(webrtc::RTCError e) override {
    callback_(std::move(e));
  }

 private:
  std::function<void(webrtc::RTCError)> callback_;
};
template <>
class DescriptionObserver<webrtc::SetRemoteDescriptionObserverInterface>
    : public webrtc::SetRemoteDescriptionObserverInterface {
 public:
  explicit DescriptionObserver(std::function<void(webrtc::RTCError)> callback)
      : callback_(std::move(callback)) {}
  void OnSetRemoteDescriptionComplete(webrtc::RTCError e) override {
    callback_(std::move(e));
  }

 private:
  std::function<void(webrtc::RTCError)> callback_;
};
}  // namespace

class Peer::Session : public webrtc::PeerConnectionObserver,
                      public std::enable_shared_from_this<Session> {
 public:
  Session(Peer& owner, std::string sid)
      : owner_(owner), sid_(std::move(sid)), parser_(owner.input_),
        source_(owner.source_factory_()) {}
  ~Session() override { CHECK(closed_); }
  // Every deferred task is weakly bound to this exact session, including ICE.
  template <typename F>
  void Post(F task) {
    owner_.signaling_thread_->PostTask(
        [weak = weak_from_this(), task = std::move(task)]() mutable {
          if (auto s = weak.lock(); s && !s->closed_)
            task(*s);
        });
  }
  void Start(const base::DictValue& message) {
    webrtc::PeerConnectionInterface::RTCConfiguration config;
    config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
    config.bundle_policy = webrtc::PeerConnectionInterface::kBundlePolicyMaxBundle;
    if (const auto* servers = message.FindList("iceServers")) {
      for (const auto& value : *servers) {
        if (!value.is_dict())
          continue;
        const auto& item = value.GetDict();
        webrtc::PeerConnectionInterface::IceServer server;
        if (const auto* urls = item.FindString("urls"))
          server.urls.push_back(*urls);
        if (const auto* urls = item.FindList("urls")) {
          for (const auto& url : *urls)
            if (url.is_string()) server.urls.push_back(url.GetString());
        }
        server.username = String(item, "username");
        server.password = String(item, "credential");
        config.servers.push_back(std::move(server));
      }
    }
    // Network-emulation lab: the control-plane may force relay-only candidates
    // (iceTransportPolicy) and --turn=host:port[,user[,pass]] overrides the
    // server list with a TURN relay (tools/netem), forcing relay-only as well.
    if (String(message, "iceTransportPolicy") == "relay")
      config.type = webrtc::PeerConnectionInterface::kRelay;
    if (const auto* cl = base::CommandLine::ForCurrentProcess();
        cl->HasSwitch("turn")) {
      std::vector<std::string> parts =
          base::SplitString(cl->GetSwitchValueASCII("turn"), ",",
                            base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
      webrtc::PeerConnectionInterface::IceServer server;
      server.urls.push_back("turn:" + parts[0] + "?transport=udp");
      server.username = parts.size() > 1 ? parts[1] : "endpoint";
      server.password = parts.size() > 2 ? parts[2] : "netem";
      config.servers.clear();
      config.servers.push_back(std::move(server));
      config.type = webrtc::PeerConnectionInterface::kRelay;
      LOG(INFO) << "PRTS netem: forced relay-only via TURN " << parts[0];
    }
    auto result = owner_.factory_->CreatePeerConnectionOrError(
        config, webrtc::PeerConnectionDependencies(this));
    if (!result.ok()) {
      Fail(result.error().message());
      return;
    }
    pc_ = result.MoveValue();
    track_source_ = webrtc::make_ref_counted<TrackSource>();
    auto track = owner_.factory_->CreateVideoTrack(track_source_, "screen");
    // Fluid reduced latency in repeated 1080p30 trials. Keep the encoder in
    // screen mode and preserve resolution for browser text.
    // --latency-screen-content restores the original source hint for A/B runs.
    if (!base::CommandLine::ForCurrentProcess()->HasSwitch(
            "latency-screen-content")) {
      track->set_content_hint(webrtc::VideoTrackInterface::ContentHint::kFluid);
    }
    webrtc::RtpTransceiverInit init;
    init.direction = webrtc::RtpTransceiverDirection::kSendOnly;
    init.stream_ids = {"prts"};
    init.send_encodings.emplace_back();
    auto tr = pc_->AddTransceiver(track, init);
    if (!tr.ok()) {
      Fail(tr.error().message());
      return;
    }
    transceiver_ = tr.MoveValue();
    std::vector<webrtc::RtpCodecCapability> codecs;
    for (const auto& codec : owner_.factory_->GetRtpSenderCapabilities(
             webrtc::MediaType::VIDEO).codecs) {
      if ((codec.name == "H264" &&
           codec.parameters == H264Format().parameters) || codec.name == "rtx")
        codecs.push_back(codec);
    }
    auto codec_error = transceiver_->SetCodecPreferences(codecs);
    if (!codec_error.ok()) {
      Fail(codec_error.message());
      return;
    }
    auto extensions = transceiver_->GetHeaderExtensionsToNegotiate();
    bool found = false;
    for (auto& ext : extensions) {
      if (ext.uri == webrtc::RtpExtension::kPlayoutDelayUri) {
        ext.direction = webrtc::RtpTransceiverDirection::kSendRecv;
        found = true;
      }
    }
    if (!found) {
      Fail("playout-delay capability missing");
      return;
    }
    auto error = transceiver_->SetHeaderExtensionsToNegotiate(extensions);
    if (!error.ok()) {
      Fail(error.message());
      return;
    }
    auto parameters = transceiver_->sender()->GetParameters();
    if (parameters.encodings.empty()) {
      Fail("sender has no encoding"); return;
    }
    parameters.encodings[0].min_bitrate_bps = 2000000;
    parameters.encodings[0].max_bitrate_bps = 12000000;
    parameters.encodings[0].max_framerate = owner_.fps_;
    parameters.degradation_preference =
        base::CommandLine::ForCurrentProcess()->HasSwitch(
            "latency-maintain-framerate")
            ? webrtc::DegradationPreference::MAINTAIN_FRAMERATE
            : webrtc::DegradationPreference::MAINTAIN_RESOLUTION;
    error = transceiver_->sender()->SetParameters(parameters);
    if (!error.ok()) {
      Fail(error.message());
      return;
    }
    webrtc::BitrateSettings bitrate;
    bitrate.min_bitrate_bps = 2000000;
    bitrate.start_bitrate_bps = 8000000;
    bitrate.max_bitrate_bps = 12000000;
    error = pc_->SetBitrate(bitrate);
    if (!error.ok()) {
      Fail(error.message());
      return;
    }
    const char* labels[] = {"input", "ctl", "probe"};
    for (int i = 0; i < 3; ++i) {
      webrtc::DataChannelInit dc;
      dc.negotiated = true;
      dc.id = i;
      if (i == 2) {
        dc.ordered = false;
        dc.maxRetransmits = 0;
      }
      auto channel = pc_->CreateDataChannelOrError(labels[i], &dc);
      if (!channel.ok()) {
        Fail(channel.error().message());
        return;
      }
      channels_[i] = std::make_unique<Channel>(*this, i, channel.MoveValue());
    }
    source_->Start([track_source = track_source_](const webrtc::VideoFrame& f) {
      track_source->Push(f);
    });
    MakeOffer(false);
  }
  void Close() {
    if (closed_)
      return;
    closed_ = true;
    source_->Stop();
    parser_.Reset();
    for (auto& ch : channels_)
      ch.reset();  // Unregister observer before closing PC.
    if (pc_)
      pc_->Close();
    transceiver_ = nullptr;
    track_source_ = nullptr;
    pc_ = nullptr;
  }
  void Signal(const base::DictValue& m) {
    if (closed_ || String(m, "sid") != sid_)
      return;
    auto t = String(m, "t");
    if (t == "bye") {
      Close();
      return;
    }
    if (t == "restart") {
      MakeOffer(true);
      return;
    }
    if (t == "ice") {
      if (const auto* candidate = m.Find("candidate")) {
        if (incoming_ice_.size() >= 512) {
          Fail("ICE queue overflow");
          return;
        }
        incoming_ice_.push_back(candidate->Clone());
        DrainIce();
      }
    } else if (t == "answer" && negotiating_ && offer_sent_ && !remote_pending_ &&
               !remote_ready_) {
      auto description = webrtc::CreateSessionDescription(
          webrtc::SdpType::kAnswer, String(m, "sdp"));
      if (!description) {
        Fail("invalid answer");
        return;
      }
      remote_pending_ = true;
      pc_->SetRemoteDescription(std::move(description), webrtc::make_ref_counted<
          DescriptionObserver<webrtc::SetRemoteDescriptionObserverInterface>>(
          [weak = weak_from_this()](webrtc::RTCError e) {
            if (auto s = weak.lock()) s->Post([e = std::move(e)](Session& s) {
              if (!e.ok()) {
                s.Fail(e.message());
                return;
              }
              s.remote_pending_ = false;
              s.remote_ready_ = true;
              s.negotiating_ = false;
              bool delay = false;
              for (const auto& ext : s.transceiver_->GetNegotiatedHeaderExtensions())
                delay |= ext.uri == webrtc::RtpExtension::kPlayoutDelayUri &&
                    ext.direction != webrtc::RtpTransceiverDirection::kStopped;
              if (!delay) {
                s.Fail("answer omitted playout-delay");
                return;
              }
              s.DrainIce();
              if (s.restart_pending_) s.MakeOffer(true);
            });
          }));
    }
  }

  void SendControl(base::DictValue message) {
    if (!closed_ && handshaken_ && channels_[1])
      channels_[1]->Send(std::move(message));
  }
  void RequestKeyFrame() {
    if (closed_ || !transceiver_) return;
    auto error = transceiver_->sender()->GenerateKeyFrame({});
    if (!error.ok()) LOG(WARNING) << "keyframe: " << error.message();
  }

 private:
  class Channel : public webrtc::DataChannelObserver {
   public:
    Channel(Session& session, int id,
            webrtc::scoped_refptr<webrtc::DataChannelInterface> channel)
        : session_(session), id_(id), channel_(std::move(channel)) {
      channel_->RegisterObserver(this);
    }
    ~Channel() override {
      channel_->UnregisterObserver();
      channel_->Close();
    }
    void OnStateChange() override {}
    void OnMessage(const webrtc::DataBuffer& data) override {
      if (data.size() > 65536)
        return;
      double received = session_.owner_.clock_.WallMs();
      session_.Post([id = id_, data, received](Session& s) {
        s.OnData(id, data, received);
      });
    }
    void Send(base::DictValue message) {
      std::string json;
      if (channel_->state() != webrtc::DataChannelInterface::kOpen ||
          channel_->buffered_amount() > 65536)
        return;
      if (base::JSONWriter::Write(message, &json) &&
          !channel_->Send(webrtc::DataBuffer(json)))
        LOG(WARNING) << "DataChannel send dropped";
    }
   private:
    Session& session_;
    int id_;
    webrtc::scoped_refptr<webrtc::DataChannelInterface> channel_;
  };
  void Fail(std::string_view reason) {
    LOG(ERROR) << "session: " << reason;
    owner_.signaling_.Send(base::DictValue().Set("t", "bye")
        .Set("sid", sid_).Set("reason", reason));
    Close();
  }
  void MakeOffer(bool restart) {
    if (negotiating_) {
      restart_pending_ |= restart;
      return;
    }
    negotiating_ = true;
    restart_pending_ = false;
    offer_sent_ = false;
    remote_ready_ = false;
    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
    options.ice_restart = restart;
    auto observer = webrtc::make_ref_counted<OfferObserver>(
        [weak = weak_from_this()](auto description, webrtc::RTCError e) {
          if (auto s = weak.lock()) s->Post(
              [description = std::move(description), e = std::move(e)]
              (Session& s) mutable {
                if (!e.ok()) {
                  s.Fail(e.message());
                  return;
                }
                s.pc_->SetLocalDescription(std::move(description),
                    webrtc::make_ref_counted<DescriptionObserver<
                        webrtc::SetLocalDescriptionObserverInterface>>(
                        [weak = s.weak_from_this()](webrtc::RTCError e) {
                          if (auto s = weak.lock()) s->Post(
                              [e = std::move(e)](Session& s) {
                                if (!e.ok()) {
                                  s.Fail(e.message());
                                  return;
                                }
                                std::string sdp;
                                if (!s.pc_->local_description()->ToString(&sdp)) {
                                  s.Fail("empty local SDP");
                                  return;
                                }
                                s.owner_.signaling_.Send(base::DictValue()
                                    .Set("t", "offer").Set("sid", s.sid_)
                                    .Set("sdp", sdp));
                                s.offer_sent_ = true;
                                for (auto& ice : s.outgoing_ice_)
                                  s.owner_.signaling_.Send(std::move(ice));
                                s.outgoing_ice_.clear();
                              });
                        }));
              });
        });
    pc_->CreateOffer(observer.get(), options);
  }
  void EmitIce(base::Value candidate) {
    auto message = base::DictValue().Set("t", "ice").Set("sid", sid_)
        .Set("candidate", std::move(candidate));
    if (offer_sent_)
      owner_.signaling_.Send(std::move(message));
    else if (outgoing_ice_.size() < 512)
      outgoing_ice_.push_back(std::move(message));
    else
      Fail("outgoing ICE queue overflow");
  }
  void DrainIce() {
    if (!remote_ready_)
      return;
    for (const auto& value : incoming_ice_) {
      // null/empty is an end marker. ICE agent needs no extra action here.
      if (!value.is_dict())
        continue;
      const auto& c = value.GetDict();
      if (String(c, "candidate").empty())
        continue;
      auto candidate = webrtc::IceCandidate::Create(String(c, "sdpMid"),
          c.FindInt("sdpMLineIndex").value_or(0), String(c, "candidate"));
      if (!candidate || !pc_->AddIceCandidate(candidate.get()))
        LOG(WARNING) << "Invalid ICE candidate";
    }
    incoming_ice_.clear();
  }
  void OnData(int id, const webrtc::DataBuffer& data, double received) {
    if (id == 0) {
      if (!handshaken_ || !data.binary)
        return;
      InputMetadata metadata;
      if (parser_.Parse(std::span(data.data.cdata(), data.size()),
                        source_->geometry(), &metadata))
        source_->SetInputSequence(metadata.seq);
      else
        ++invalid_inputs_;
      return;
    }
    if (data.binary)
      return;
    auto message = base::JSONReader::ReadDict(std::string_view(
        reinterpret_cast<const char*>(data.data.cdata()), data.size()),
        base::JSON_PARSE_RFC);
    if (!message)
      return;
    auto t = String(*message, "t");
    if (id == 2 && t == "ping") {
      if (!message->Find("id") || !message->FindDouble("t0"))
        return;
      channels_[2]->Send(base::DictValue().Set("t", "pong")
          .Set("id", message->Find("id")->Clone())
          .Set("t0", *message->FindDouble("t0")).Set("t1", received)
          .Set("t2", owner_.clock_.WallMs()));
    } else if (id == 1 && t == "hello") {
      if (message->FindInt("v") != 1 || message->FindInt("input") != 1) {
        Fail("version"); return;
      }
      handshaken_ = true;
      owner_.input_.Control(*message);
      auto g = source_->geometry();
      channels_[1]->Send(base::DictValue().Set("t", "hello").Set("v", 1)
          .Set("input", 1).Set("frame", base::DictValue()
              .Set("w", g.width).Set("h", g.height))
          .Set("dsf", g.dsf).Set("name", "prts-endpoint"));
      RequestKeyFrame();
      owner_.input_.Control(*message);
      owner_.input_.Control(base::DictValue().Set("t", "keyframe"));
    } else if (id == 1 && handshaken_ &&
               (t.starts_with("ime.") || t == "focus" ||
                t == "keyframe" || t == "resize")) {
      if (t == "focus" && message->FindBool("focused") == false)
        parser_.Reset();
      if (t == "keyframe")
        RequestKeyFrame();
      owner_.input_.Control(*message);
    }
  }
  void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState) override {}
  void OnDataChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface>) override {}
  void OnIceGatheringChange(
      webrtc::PeerConnectionInterface::IceGatheringState state) override {
    if (state == webrtc::PeerConnectionInterface::kIceGatheringComplete)
      Post([](Session& s) { s.EmitIce(base::Value()); });
  }
  void OnIceCandidate(const webrtc::IceCandidate* candidate) override {
    auto value = base::Value(base::DictValue().Set("candidate", candidate->ToString())
        .Set("sdpMid", candidate->sdp_mid())
        .Set("sdpMLineIndex", candidate->sdp_mline_index()));
    Post([value = std::move(value)](Session& s) mutable {
      s.EmitIce(std::move(value));
    });
  }
  void OnConnectionChange(
      webrtc::PeerConnectionInterface::PeerConnectionState state) override {
    Post([state](Session& s) {
      LOG(INFO) << "PeerConnection state=" << static_cast<int>(state);
      auto epoch = ++s.failure_epoch_;
      if (state == webrtc::PeerConnectionInterface::PeerConnectionState::kFailed)
        s.owner_.signaling_thread_->PostDelayedTask(
            [weak = s.weak_from_this(), epoch] {
              if (auto s = weak.lock(); s && !s->closed_ &&
                  s->failure_epoch_ == epoch) s->Fail("connection failed");
            }, webrtc::TimeDelta::Seconds(10));
    });
  }
  Peer& owner_;
  const std::string sid_;
  InputParser parser_;
  std::unique_ptr<FrameSource> source_;
  webrtc::scoped_refptr<webrtc::PeerConnectionInterface> pc_;
  webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver_;
  webrtc::scoped_refptr<TrackSource> track_source_;
  std::unique_ptr<Channel> channels_[3];
  std::vector<base::Value> incoming_ice_;
  std::vector<base::DictValue> outgoing_ice_;
  uint64_t invalid_inputs_ = 0;
  uint64_t failure_epoch_ = 0;
  bool closed_ = false;
  bool handshaken_ = false;
  bool negotiating_ = false;
  bool restart_pending_ = false;
  bool offer_sent_ = false;
  bool remote_pending_ = false;
  bool remote_ready_ = false;
};

Peer::Peer(Signaling& signaling, InputSink& input, const Clock& clock,
           SourceFactory source_factory, int fps)
    : signaling_(signaling), input_(input), clock_(clock),
      source_factory_(std::move(source_factory)), fps_(fps) {}
Peer::~Peer() {
  if (signaling_thread_) {
    signaling_thread_->BlockingCall([this] {
      if (session_) session_->Close();
      session_.reset();
      factory_ = nullptr;
    });
    signaling_thread_->Stop();
  }
  if (network_thread_) network_thread_->Stop();
}
bool Peer::Initialize() {
  network_thread_ = webrtc::Thread::CreateWithSocketServer();
  signaling_thread_ = webrtc::Thread::Create();
  if (!network_thread_->Start()) {
    signaling_thread_.reset();
    return false;
  }
  if (!signaling_thread_->Start()) {
    signaling_thread_.reset();
    return false;
  }
  return signaling_thread_->BlockingCall([this] {
    webrtc::PeerConnectionFactoryDependencies deps;
    deps.network_thread = network_thread_.get();
    deps.signaling_thread = signaling_thread_.get();
    deps.env = WebRtcEnvironment();
    using NoAudio = webrtc::webrtc_impl::AudioDeviceModuleDefault<
        webrtc::AudioDeviceModule>;
    deps.adm = webrtc::make_ref_counted<NoAudio>();
    deps.audio_encoder_factory =
        webrtc::CreateAudioEncoderFactory<webrtc::AudioEncoderOpus>();
    deps.audio_decoder_factory =
        webrtc::CreateAudioDecoderFactory<webrtc::AudioDecoderOpus>();
    auto encoder = std::make_unique<EncoderFactory>();
    if (encoder->GetSupportedFormats().empty()) {
      LOG(ERROR) << "OpenH264 constrained baseline 42e01f unavailable";
      return false;
    }
    deps.video_encoder_factory = std::move(encoder);
    deps.video_decoder_factory = webrtc::CreateBuiltinVideoDecoderFactory();
    webrtc::EnableMedia(deps);
    factory_ = webrtc::CreateModularPeerConnectionFactory(std::move(deps));
    return factory_ != nullptr;
  });
}
void Peer::Handle(base::DictValue message) {
  signaling_thread_->BlockingCall([this, &message] {
    if (String(message, "t") == "session.open") {
      if (session_) session_->Close();
      session_.reset();
      auto sid = String(message, "sid");
      if (sid.empty()) return;
      session_ = std::make_shared<Session>(*this, sid);
      session_->Start(message);
    } else if (session_) {
      session_->Signal(message);
    }
  });
}
void Peer::SendControl(base::DictValue message) {
  signaling_thread_->BlockingCall([this, &message] {
    if (session_) session_->SendControl(std::move(message));
  });
}
void Peer::RequestKeyFrame() {
  signaling_thread_->BlockingCall([this] {
    if (session_) session_->RequestKeyFrame();
  });
}
void Peer::Close() {
  signaling_thread_->BlockingCall([this] {
    if (session_) session_->Close();
    session_.reset();
  });
}
}  // namespace prts

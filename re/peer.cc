#ifdef UNSAFE_BUFFERS_BUILD
#pragma allow_unsafe_buffers
#pragma allow_unsafe_libc_calls
#endif

#include "re/peer.h"

#include "re/encoder_factory.h"
#include "re/vpl_encoder.h"
#include <algorithm>
#include <atomic>
#include <span>
#include <string>
#include <string_view>
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
#include "re/experiment_options.h"
#include "re/latency_stats.h"
#include "third_party/webrtc/api/audio_codecs/audio_decoder_factory_template.h"
#include "third_party/webrtc/api/audio_codecs/audio_encoder_factory_template.h"
#include "third_party/webrtc/api/audio_codecs/opus/audio_decoder_opus.h"
#include "third_party/webrtc/api/audio_codecs/opus/audio_encoder_opus.h"
#include "third_party/webrtc/api/create_modular_peer_connection_factory.h"
#include "third_party/webrtc/api/enable_media.h"
#include "third_party/webrtc/api/make_ref_counted.h"
#include "third_party/webrtc/api/set_local_description_observer_interface.h"
#include "third_party/webrtc/api/set_remote_description_observer_interface.h"
#include "third_party/webrtc/api/stats/rtc_stats_collector_callback.h"
#include "third_party/webrtc/api/video/adapted_video_track_source.h"
#include "third_party/webrtc/api/video/encoded_image.h"
#include "third_party/webrtc/api/video_codecs/builtin_video_decoder_factory.h"
#include "third_party/webrtc/api/video_codecs/video_encoder_factory.h"
#include "third_party/webrtc/media/engine/internal_encoder_factory.h"
// Header-only default ADM; no standalone audio_device dependency is permitted
// by Chromium WebRTC visibility. Its API dependencies come from the component.
#include "third_party/webrtc/modules/audio_device/include/audio_device_default.h"  // nogncheck
#include "third_party/webrtc_overrides/environment.h"

namespace re {
namespace {
class SenderStats : public webrtc::RTCStatsCollectorCallback {
 public:
  explicit SenderStats(std::function<void(base::DictValue)> deliver)
      : deliver_(std::move(deliver)) {}
  void OnStatsDelivered(
      const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report)
      override {
    for (const auto& stats : *report) {
      if (std::string_view(stats.type()) == "outbound-rtp") {
        auto value = base::JSONReader::ReadDict(stats.ToJson(),
                                               base::JSON_PARSE_RFC);
        if (value)
          deliver_(std::move(*value));
      }
    }
  }
 private:
  std::function<void(base::DictValue)> deliver_;
};
std::string String(const base::DictValue& m, const char* key) {
  const auto* s = m.FindString(key);
  return s ? *s : std::string();
}
webrtc::SdpVideoFormat H264Format() {
  return {"H264", {{"profile-level-id", "42e01f"},
                   {"packetization-mode", "1"},
                   {"level-asymmetry-allowed", "1"}}};
}

class TrackSource : public webrtc::AdaptedVideoTrackSource {
 public:
  SourceState state() const override { return kLive; }
  bool remote() const override { return false; }
  bool is_screencast() const override { return true; }
  std::optional<bool> needs_denoising() const override { return false; }
  void Push(const webrtc::VideoFrame& frame) { ++frames_; OnFrame(frame); }
  uint64_t frames() const { return frames_.load(); }
 private:
  std::atomic<uint64_t> frames_{0};
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
    const auto* viewer = message.FindDict("viewer");
    const auto* caps = viewer ? viewer->FindDict("caps") : nullptr;
    target_fps_ = std::min(owner_.fps_,
        std::clamp(caps ? caps->FindInt("refreshFps").value_or(60) : 60,
                   1, 120));
    const auto& experiments = GetExperimentOptions();
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
    PollStats();
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
    init.stream_ids = {"rb"};
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
           codec.parameters == H264Format().parameters) ||
          codec.name == "VP8" || codec.name == "rtx" ||
          (experiments.fec == "ulpfec" &&
           (codec.name == "red" || codec.name == "ulpfec")) ||
          (experiments.fec == "flexfec" && codec.name == "flexfec-03"))
        codecs.push_back(codec);
    }
    // Sender capabilities need not follow our factory's advertised order.
    // Keep the tuned H.264 path preferred, with VP8 as the primary fallback.
    auto rank = [](const auto& codec) {
      return codec.name == "H264" ? 0 : codec.name == "VP8" ? 1 : 2;
    };
    std::stable_sort(codecs.begin(), codecs.end(),
                     [&](const auto& a, const auto& b) {
                       return rank(a) < rank(b);
                     });
    auto codec_error = transceiver_->SetCodecPreferences(codecs);
    if (!codec_error.ok()) {
      Fail(codec_error.message());
      return;
    }
    auto extensions = transceiver_->GetHeaderExtensionsToNegotiate();
    bool found = false;
    for (auto& ext : extensions) {
      if (ext.uri == webrtc::RtpExtension::kPlayoutDelayUri) {
        ext.direction = experiments.playout_extension
                            ? webrtc::RtpTransceiverDirection::kSendRecv
                            : webrtc::RtpTransceiverDirection::kStopped;
        found = true;
      }
    }
    if (!found && experiments.playout_extension) {
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
    parameters.encodings[0].min_bitrate_bps = experiments.min_kbps * 1000;
    parameters.encodings[0].max_bitrate_bps = experiments.max_kbps * 1000;
    owner_.encoder_state_->min_bitrate_bps.store(
        *parameters.encodings[0].min_bitrate_bps);
    owner_.encoder_state_->max_bitrate_bps.store(
        *parameters.encodings[0].max_bitrate_bps);
    parameters.encodings[0].max_framerate = target_fps_;
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
    owner_.encoder_state_->target_fps.store(target_fps_);
    owner_.input_.Control(base::DictValue().Set("t", "video.rate")
                             .Set("fps", target_fps_));
    webrtc::BitrateSettings bitrate;
    bitrate.min_bitrate_bps = experiments.min_kbps * 1000;
    bitrate.start_bitrate_bps = experiments.start_kbps * 1000;
    bitrate.max_bitrate_bps = experiments.max_kbps * 1000;
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
              if (!delay && GetExperimentOptions().playout_extension) {
                s.Fail("answer omitted playout-delay");
                return;
              }
              std::string codecs;
              bool red = false;
              bool ulpfec = false;
              bool flexfec = false;
              for (const auto& codec :
                   s.transceiver_->sender()->GetParameters().codecs) {
                codecs += codec.name + ',';
                red |= codec.name == "red";
                ulpfec |= codec.name == "ulpfec";
                flexfec |= codec.name == "flexfec-03";
              }
              const auto& experiments = GetExperimentOptions();
              const bool fec_negotiated =
                  experiments.fec == "off" ||
                  (experiments.fec == "ulpfec" && red && ulpfec) ||
                  (experiments.fec == "flexfec" && flexfec);
              LOG(INFO) << "PRTS peer negotiated fec=" << experiments.fec
                        << " fec_negotiated=" << fec_negotiated
                        << " codecs=" << codecs
                        << " playout_extension=" << delay;
              if (!fec_negotiated) {
                s.Fail("answer omitted requested FEC codec(s)");
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
      const auto rx_ticks = base::TimeTicks::Now();  // m3-trace
      session_.Post([id = id_, data, received, rx_ticks](Session& s) {
        s.rx_ticks_ = rx_ticks;
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
                if (GetExperimentOptions().fec == "ulpfec") {
                  // RB: H264 + generic NACK disables ULPFEC in
                  // ShouldDisableRedAndUlpfec(). Codec preference feedback is
                  // ignored at this pin; remove only generic NACK from the
                  // actual local offer, keeping nack pli and ccm fir for IDR.
                  std::string original;
                  if (!description->ToString(&original)) {
                    s.Fail("empty ULPFEC offer");
                    return;
                  }
                  std::string filtered;
                  for (const auto& line :
                       base::SplitString(original, "\n", base::TRIM_WHITESPACE,
                                         base::SPLIT_WANT_NONEMPTY)) {
                    if (line.starts_with("a=rtcp-fb:") &&
                        line.ends_with(" nack"))
                      continue;
                    filtered += line + "\r\n";
                  }
                  description = webrtc::CreateSessionDescription(
                      webrtc::SdpType::kOffer, filtered);
                  if (!description) {
                    s.Fail("invalid ULPFEC offer");
                    return;
                  }
                  LOG(INFO) << "PRTS ULPFEC generic_nack=off pli_fir=preserved";
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
                                VLOG(1) << "PRTS offer SDP sid=" << s.sid_
                                        << "\n" << sdp;
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
  void SetViewerRate(int requested) {
    if (requested < 1 || requested > 120)
      return;
    const int fps = std::min(owner_.fps_, requested);
    if (fps != target_fps_) {
      auto parameters = transceiver_->sender()->GetParameters();
      if (parameters.encodings.empty())
        return;
      parameters.encodings[0].max_framerate = fps;
      const auto error = transceiver_->sender()->SetParameters(parameters);
      if (!error.ok()) {
        LOG(WARNING) << "PRTS viewer rate rejected: " << error.message();
        return;
      }
      target_fps_ = fps;
      owner_.encoder_state_->target_fps.store(fps);
      owner_.input_.Control(base::DictValue().Set("t", "video.rate")
                               .Set("fps", fps));
      LOG(INFO) << "PRTS viewer target fps=" << fps
                << " requested=" << requested;
    }
    channels_[1]->Send(base::DictValue().Set("t", "video.rate")
                          .Set("fps", fps).Set("maxFps", owner_.fps_));
  }
  void OnData(int id, const webrtc::DataBuffer& data, double received) {
    if (id == 0) {
      if (!handshaken_ || !data.binary)
        return;
      InputMetadata metadata;
      if (parser_.Parse(std::span(data.data.cdata(), data.size()),
                        source_->geometry(), &metadata)) {
        if (TraceEnabled())  // m3-trace
          LOG(INFO) << "PRTS TR I seq=" << metadata.seq
                    << " rx=" << TraceUs(rx_ticks_)
                    << " parsed=" << TraceUs(base::TimeTicks::Now());
        source_->SetInputSequence(metadata.seq);
      }
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
          .Set("dsf", g.dsf).Set("name", "rb-endpoint")
          .Set("maxFps", owner_.fps_).Set("targetFps", target_fps_));
      const auto* caps = message->FindDict("caps");
      SetViewerRate(caps ? caps->FindInt("refreshFps").value_or(60) : 60);
      RequestKeyFrame();
      owner_.input_.Control(*message);
      owner_.input_.Control(base::DictValue().Set("t", "keyframe"));
    } else if (id == 1 && handshaken_ && t == "video.rate") {
      if (auto fps = message->FindInt("fps"))
        SetViewerRate(*fps);
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
  void PollStats() {
    pc_->GetStats(webrtc::make_ref_counted<SenderStats>(
        [weak = weak_from_this()](base::DictValue stats) {
          if (auto s = weak.lock()) {
            s->Post([stats = std::move(stats)](Session& s) {
              s.ReportSenderStats(stats);
            });
          }
        }).get());
    owner_.signaling_thread_->PostDelayedTask(
        [weak = weak_from_this()] {
          if (auto s = weak.lock(); s && !s->closed_)
            s->PollStats();
        },
        webrtc::TimeDelta::Seconds(1));
  }
  void ReportSenderStats(const base::DictValue& stats) {
    if (String(stats, "kind") != "video")
      return;
    // RTCStats::ToJson uses microseconds (unlike browser getStats' ms).
    const double now = stats.FindDouble("timestamp").value_or(0) / 1000000;
    const double frames = stats.FindDouble("framesEncoded").value_or(0);
    const double encoded = stats.FindDouble("totalEncodeTime").value_or(0);
    const double bytes = stats.FindDouble("bytesSent").value_or(0);
    const double source_frames = track_source_ ? track_source_->frames() : 0;
    const double dt = now - sender_stats_time_;
    const double df = frames - sender_frames_;
    if (sender_stats_time_ > 0 && dt > 0 && df >= 0 && bytes >= sender_bytes_ &&
        handshaken_) {
      auto message = base::DictValue().Set("t", "stats")
          .Set("fps", df / dt)
          .Set("sourceFps", (source_frames - sender_source_frames_) / dt)
          .Set("kbps", (bytes - sender_bytes_) * 8 / dt / 1000)
          .Set("targetFps", target_fps_)
          .Set("backend", String(stats, "encoderImplementation"))
          .Set("limit", String(stats, "qualityLimitationReason"));
      if (df > 0 && encoded >= sender_encode_time_)
        message.Set("encMs", base::DictValue().Set(
            "mean", (encoded - sender_encode_time_) * 1000 / df));
      channels_[1]->Send(std::move(message));
      LOG(INFO) << "PRTS sender fps=" << df / dt
                << " target=" << target_fps_;
    }
    sender_stats_time_ = now;
    sender_frames_ = frames;
    sender_encode_time_ = encoded;
    sender_bytes_ = bytes;
    sender_source_frames_ = source_frames;
  }
  Peer& owner_;
  const std::string sid_;
  InputParser parser_;
  base::TimeTicks rx_ticks_;  // m3-trace
  std::unique_ptr<FrameSource> source_;
  webrtc::scoped_refptr<webrtc::PeerConnectionInterface> pc_;
  webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver_;
  webrtc::scoped_refptr<TrackSource> track_source_;
  std::unique_ptr<Channel> channels_[3];
  std::vector<base::Value> incoming_ice_;
  std::vector<base::DictValue> outgoing_ice_;
  uint64_t invalid_inputs_ = 0;
  int target_fps_ = 60;
  double sender_stats_time_ = 0, sender_frames_ = 0;
  double sender_encode_time_ = 0, sender_bytes_ = 0;
  double sender_source_frames_ = 0;
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
           SourceFactory source_factory, int fps,
           std::shared_ptr<VplEncoderState> encoder_state, EncoderChoice encoder)
    : signaling_(signaling), input_(input), clock_(clock),
      source_factory_(std::move(source_factory)), fps_(fps),
      encoder_state_(std::move(encoder_state)), encoder_(encoder) {}
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
    auto encoder = std::make_unique<EncoderFactory>(encoder_state_, encoder_);
    if (encoder->GetSupportedFormats().empty()) {
      LOG(ERROR) << "No supported video encoder (H264 or VP8) available";
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
}  // namespace re

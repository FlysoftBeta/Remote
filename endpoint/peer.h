#ifndef PRTS_ENDPOINT_PEER_H_
#define PRTS_ENDPOINT_PEER_H_

#include <functional>
#include <memory>

#include "base/values.h"
#include "prts_endpoint/frame_source.h"
#include "prts_endpoint/input_parser.h"
#include "prts_endpoint/signaling.h"
#include "third_party/webrtc/api/peer_connection_interface.h"
#include "third_party/webrtc/rtc_base/thread.h"

namespace prts {
// All session state lives on signaling_thread_. Main/reader call Handle/Close;
// neither is a WebRTC callback. Inject a fresh source for each session.
class Peer {
 public:
  using SourceFactory = std::function<std::unique_ptr<FrameSource>()>;
  Peer(Signaling& signaling, InputSink& input, const Clock& clock,
       SourceFactory source_factory, int fps);
  ~Peer();
  bool Initialize();
  void Handle(base::DictValue message);
  void Close();
  // Main/owner sequence only, outside RTC callbacks; marshals to signaling.
  void SendControl(base::DictValue message);
  void RequestKeyFrame();

 private:
  class Session;
  Signaling& signaling_;
  InputSink& input_;
  const Clock& clock_;
  SourceFactory source_factory_;
  const int fps_;
  std::unique_ptr<webrtc::Thread> network_thread_;
  std::unique_ptr<webrtc::Thread> signaling_thread_;
  webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory_;
  std::shared_ptr<Session> session_;
};
}  // namespace prts
#endif  // PRTS_ENDPOINT_PEER_H_

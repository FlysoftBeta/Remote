// Standalone policy checks, no Chromium runtime or GPU required.
#include "re/frame_refresh.h"
#include <cassert>
#include <iostream>

namespace {
constexpr int64_t kStart = 1000000;
void Init(re::FrameRefresh& policy, uint64_t bps = 8000000) {
  policy.Start(60);
  policy.Rates(bps);
  policy.Source(kStart);
  policy.Encode(kStart + 1000);
  policy.Encoded(kStart + 2000, 1000, 1000, 1920 * 1080, false, 35, true);
}
void Converge(re::FrameRefresh& policy, bool trusted) {
  for (int i = 1; i <= 5; ++i) {
    const int64_t now = kStart + i * 50000;
    assert(policy.Request(now));
    policy.Source(now + 1000);
    policy.Encode(now + 2000);
    policy.Encoded(now + 3000, 1000, trusted ? 300 : 60, 1920 * 1080,
                   false, trusted ? 20 : -1, trusted);
  }
}
}  // namespace
int main() {
  {
    re::FrameRefresh p;
    Init(p);
    assert(!p.Request(kStart + 30000));  // Wait for real capture first.
    p.Source(kStart + 30000);
    assert(!p.Request(kStart + 50000));  // Moving content wins.
    assert(p.Request(kStart + 65000));
    assert(!p.Request(kStart + 70000));  // One outstanding capture only.
    p.Source(kStart + 71000);
    p.Encode(kStart + 72000);
    assert(!p.Request(kStart + 200000));  // Don't queue behind encode.
    assert(p.Request(kStart + 600000));  // Stalled encode can recover.
    p.Stop();
    assert(!p.Request(kStart + 2000000));
  }
  {
    re::FrameRefresh p;
    Init(p);
    p.Rates(0);
    assert(!p.Request(kStart + 1000000));  // GCC suspended the stream.
    p.Rates(300000);
    assert(p.Request(kStart + 1000001));
  }
  {
    re::FrameRefresh p;
    Init(p, 300000);
    p.Encoded(kStart + 2000, 1000, 80000, 1920 * 1080, true, 40, true);
    assert(!p.Request(kStart + 1000000));  // Large IDR must drain first.
    assert(p.Request(kStart + 2700000));
  }
  {
    re::FrameRefresh p;
    Init(p);
    p.SendDelay(100000);
    assert(!p.Request(kStart + 150000));  // Pacer backlog reduces repair.
    assert(p.Request(kStart + 210000));
  }
  {
    re::FrameRefresh p;
    Init(p);
    p.Dropped(kStart + 10000);
    assert(!p.Request(kStart + 100000));
    assert(p.Request(kStart + 120000));
  }
  {
    re::FrameRefresh p;
    Init(p);
    p.Encode(kStart + 10000);
    p.EncodeFinished(kStart + 10000, kStart + 11000);
    assert(p.Request(kStart + 40000));  // Successful rate-control skip.
    assert(p.Request(kStart + 600000));  // Lost capture response retries.
  }
  for (bool trusted : {false, true}) {
    re::FrameRefresh p;
    Init(p);
    Converge(p, trusted);
    assert(!p.Request(kStart + 500000));  // Settled: low-frequency retry.
    p.Rates(10000000);  // More bandwidth must immediately reopen refinement.
    assert(p.Request(kStart + 500001));
  }
  {
    re::FrameRefresh p;
    Init(p);
    Converge(p, true);
    assert(p.Request(kStart + 1300000));  // Convergence never stops forever.
  }
  {
    re::FrameRefresh p;
    Init(p);
    Converge(p, true);
    p.Rates(8500000);
    p.Rates(9000000);
    assert(!p.Request(kStart + 500000));
    p.Rates(9700000);  // Gradual recovery is measured from convergence.
    assert(p.Request(kStart + 500001));
  }
  {
    re::FrameRefresh p;
    Init(p);
    Converge(p, true);
    p.Source(kStart + 500000);  // A new isolated edit resets convergence.
    assert(p.Request(kStart + 540000));
    p.Start(15);  // Reconnection must discard the previous session's state.
    assert(!p.Request(kStart + 2000000));
  }
  {
    re::FrameRefresh p;
    Init(p);
    p.SetFps(15);
    assert(!p.Request(kStart + 100000));
    assert(p.Request(kStart + 140000));
  }
  std::cout << "PASS frame refresh policy (motion, budgets, QoS, convergence, recovery)\n";
}

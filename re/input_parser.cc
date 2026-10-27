#ifdef UNSAFE_BUFFERS_BUILD
#pragma allow_unsafe_buffers
#pragma allow_unsafe_libc_calls
#endif

#include "re/input_parser.h"

#include <bit>

#include "base/logging.h"
#include "base/strings/string_util.h"

namespace re {
namespace {
uint16_t U16(std::span<const uint8_t> b, size_t i) {
  return uint16_t{b[i]} | (uint16_t{b[i + 1]} << 8);
}
int16_t I16(std::span<const uint8_t> b, size_t i) {
  return std::bit_cast<int16_t>(U16(b, i));
}
}  // namespace
void InputParser::Reset() {
  mods_ = 0;
  sink_.ResetInput();
}
bool InputParser::Parse(std::span<const uint8_t> b, FrameGeometry g,
                        InputMetadata* metadata) {
  if (b.size() < 8 || g.dsf <= 0 || g.width <= 0 || g.height <= 0)
    return false;
  auto now = base::TimeTicks::Now();
  switch (b[0]) {
    case 1: {
      if (b.size() < 20 || b[1] > 4 || b[12] > 2 || b[13] > 15)
        return false;
      mods_ = b[16];
      sink_.Pointer({static_cast<PointerAction>(b[1]),
          static_cast<PointerType>(b[12]), b[13], U16(b, 8) / 65535.f,
          U16(b, 10) / 65535.f, b[14], std::bit_cast<int8_t>(b[15]), mods_,
          b[17] / 255.f, static_cast<float>(b[18] / g.dsf),
          static_cast<float>(b[19] / g.dsf), now});
      break;
    }
    case 2: {
      if (b.size() < 16 || b[1] > 2)
        return false;
      float sx = static_cast<float>(b[1] == 0 ? 1 / g.dsf :
          b[1] == 1 ? 40 : g.width / g.dsf);
      float sy = static_cast<float>(b[1] == 0 ? 1 / g.dsf :
          b[1] == 1 ? 40 : g.height / g.dsf);
      sink_.Wheel({U16(b, 8) / 65535.f, U16(b, 10) / 65535.f,
                   I16(b, 12) / 4.f * sx, I16(b, 14) / 4.f * sy, mods_, now});
      break;
    }
    case 3: {
      if (b.size() < 14 || b[1] > 1 || b[12] > 32 || b[13] > 32 ||
          b.size() < size_t{14} + b[12] + b[13])
        return false;
      std::string code(reinterpret_cast<const char*>(b.data() + 14), b[12]);
      std::string key(reinterpret_cast<const char*>(b.data() + 14 + b[12]), b[13]);
      if (!base::IsStringUTF8AllowingNoncharacters(code) ||
          !base::IsStringUTF8AllowingNoncharacters(key))
        return false;
      mods_ = b[8];
      sink_.Key({b[1] == 0, mods_, (b[9] & 1) != 0, U16(b, 10),
                 code, key, now});
      break;
    }
    case 4:
      Reset();
      break;
    default:
      return false;
  }
  metadata->seq = U16(b, 2);
  metadata->viewer_ts = uint32_t{b[4]} | (uint32_t{b[5]} << 8) |
      (uint32_t{b[6]} << 16) | (uint32_t{b[7]} << 24);
  return true;
}
void LoggingInputSink::Pointer(const PointerInput& i) {
  VLOG(1) << "pointer " << static_cast<int>(i.action) << " pid=" << int{i.pid}
          << " at=" << i.x << "," << i.y << " buttons=" << int{i.buttons};
}
void LoggingInputSink::Wheel(const WheelInput& i) {
  VLOG(1) << "wheel " << i.dx << "," << i.dy << " mods=" << int{i.mods};
}
void LoggingInputSink::Key(const KeyInput& i) {
  VLOG(1) << "key " << i.code << " down=" << i.down << " repeat=" << i.repeat;
}
void LoggingInputSink::ResetInput() { LOG(INFO) << "input reset"; }
void LoggingInputSink::Control(const base::DictValue& message) {
  LOG(INFO) << "control " << *message.FindString("t");
}
}  // namespace re

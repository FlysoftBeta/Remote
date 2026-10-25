#ifndef PRTS_ENDPOINT_INPUT_PARSER_H_
#define PRTS_ENDPOINT_INPUT_PARSER_H_

#include <cstdint>
#include <span>
#include <string>

#include "base/time/time.h"
#include "base/values.h"
#include "prts_endpoint/frame_source.h"

namespace prts {
// Plain mirrors of contract/prts.mojom; no Mojo dependency in this skeleton.
enum class PointerAction { kDown, kMove, kUp, kCancel, kLeave };
enum class PointerType { kMouse, kTouch, kPen };
struct PointerInput {
  PointerAction action;
  PointerType type;
  uint8_t pid;
  float x, y;
  uint8_t buttons;
  int8_t button;
  uint8_t mods;
  float pressure, radius_x, radius_y;
  base::TimeTicks timestamp;
};
struct WheelInput {
  float x, y, dx, dy;
  uint8_t mods;
  base::TimeTicks timestamp;
};
struct KeyInput {
  bool down;
  uint8_t mods;
  bool repeat;
  uint16_t key_code;
  std::string code, key;
  base::TimeTicks timestamp;
};
struct InputMetadata {
  uint16_t seq;
  uint32_t viewer_ts;  // Raw viewer wall-clock 0.1 ms, low 32 bits.
};
class InputSink {
 public:
  virtual ~InputSink() = default;
  virtual void Pointer(const PointerInput& input) = 0;
  virtual void Wheel(const WheelInput& input) = 0;
  virtual void Key(const KeyInput& input) = 0;
  virtual void ResetInput() = 0;
  // ime.*, focus, keyframe and resize, unchanged JSON, on signaling thread.
  // Browser adapter owns pressed-key/touch tracking and RESET release logic.
  virtual void Control(const base::DictValue& message) = 0;
};
class LoggingInputSink : public InputSink {
 public:
  void Pointer(const PointerInput& input) override;
  void Wheel(const WheelInput& input) override;
  void Key(const KeyInput& input) override;
  void ResetInput() override;
  void Control(const base::DictValue& message) override;
};
class InputParser {
 public:
  explicit InputParser(InputSink& sink) : sink_(sink) {}
  bool Parse(std::span<const uint8_t> bytes, FrameGeometry geometry,
             InputMetadata* metadata);
  void Reset();

 private:
  InputSink& sink_;
  uint8_t mods_ = 0;
};
}  // namespace prts
#endif  // PRTS_ENDPOINT_INPUT_PARSER_H_

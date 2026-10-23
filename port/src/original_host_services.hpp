#pragma once

// Host hooks that both shared host interfaces need: the idle pass
// (OriginalIdlePassHost, original_idle_pass.hpp) and world input
// (OriginalWorldInputHost, original_world_input.hpp). The idle-pass host
// implements them; the world-input host returns that same object from
// services(), so each frontend implements these hooks once.

#include <cstdint>

namespace simtower {

class OriginalHostServices {
 public:
  virtual ~OriginalHostServices() = default;

  // Clock. Each call is one host clock read (GetTickCount), at the
  // original read point.
  virtual std::uint32_t now_ms() = 0;
  // DS:b3ae: 10f0's isolated Elevator Control simulation owns the world.
  virtual bool elevator_isolation_active() = 0;
  // 1118:0ad5 notification line on Info.
  virtual void notification_status(std::uint16_t code) = 0;
  // InvalidateRect on Command (1090:064d, and every Command state change).
  virtual void invalidate_command() = 0;
};

}  // namespace simtower

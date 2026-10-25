#include "original_dtmp_runtime.hpp"

#include "original_dib.hpp"

#include <cstdlib>

namespace simtower {

OriginalDtmpWindowSize original_dtmp_window_size(
    const OriginalDtmp& dtmp,
    const OriginalResources& resources) {
  if (dtmp.bitmap_resource_id >= 0) {
    const auto dib = original_dib_view(
        resources.find("BITMAP", dtmp.bitmap_resource_id));
    return {static_cast<int>(dib.width), std::abs(static_cast<int>(dib.height))};
  }
  return {static_cast<int>(dtmp.width_or_header),
          static_cast<int>(dtmp.height_or_header)};
}

}  // namespace simtower

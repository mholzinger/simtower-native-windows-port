#include "original_info_win32.hpp"

#include "original_draw_context_win32.hpp"

namespace simtower {

void draw_original_info(HDC destination,
                        const OriginalResources& resources,
                        const OriginalTdtDocument* document,
                        std::string_view status) {
  if (!destination) return;
  OriginalGdiDrawContext context(destination);
  draw_original_info(context, resources, document, status);
}

}  // namespace simtower

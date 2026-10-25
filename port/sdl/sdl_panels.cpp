#include "sdl_panels.hpp"

#include "sdl_dialog.hpp"
#include "sdl_menu.hpp"
#include "sdl_style.hpp"

#include "original_command_palette.hpp"
#include "original_info.hpp"
#include "original_map.hpp"
#include "original_palette_frame.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace simtower::sdl {
namespace {

// TEMP: desktop and Main nonclient colors until the Win16 frame and
// scrollbars are drawn (the caption and menu bar are, in sdl_menu.cpp).
// The shared TEMP colours (sdl_style.hpp); the active caption is navy by
// owner decision 2026-10-23.
constexpr std::uint32_t kDesktopRgb = style::kDesktop;
constexpr std::uint32_t kNonclientRgb = style::kButtonFace;
constexpr std::uint32_t kBorderRgb = style::kFrame;
constexpr std::uint32_t kWhiteRgb = style::kWhite;
constexpr std::uint32_t kActiveCaptionRgb = style::kCaption;

class Canvas {
 public:
  Canvas(int width, int height) {
    raster_.width = std::max(0, width);
    raster_.height = std::max(0, height);
    raster_.pixels.assign(
        static_cast<std::size_t>(raster_.width) * raster_.height, 0U);
  }

  void fill(const PanelRect& rect, std::uint32_t rgb) {
    const auto clipped = clip(rect);
    for (int y = clipped.y; y < clipped.y + clipped.height; ++y) {
      std::fill_n(raster_.pixels.begin() + offset(clipped.x, y),
                  clipped.width, rgb);
    }
  }

  // A one-pixel outline just inside rect (WS_BORDER).
  void outline(const PanelRect& rect, std::uint32_t rgb) {
    fill({rect.x, rect.y, rect.width, 1}, rgb);
    fill({rect.x, rect.y + rect.height - 1, rect.width, 1}, rgb);
    fill({rect.x, rect.y, 1, rect.height}, rgb);
    fill({rect.x + rect.width - 1, rect.y, 1, rect.height}, rgb);
  }

  // Copies source pixels to (x,y), clipped to the window and to clip.
  template <typename Raster>
  void blit(const Raster& source, int x, int y, const PanelRect& clip_to) {
    const auto target = clip(intersect(clip_to, {x, y, source.width,
                                                 source.height}));
    for (int row = target.y; row < target.y + target.height; ++row) {
      const auto* from = source.pixels.data() +
                         static_cast<std::size_t>(row - y) * source.width +
                         (target.x - x);
      std::copy_n(from, target.width,
                  raster_.pixels.begin() + offset(target.x, row));
    }
  }

  // TEMP: DrawFocusRect as an XOR of every other perimeter pixel, until the
  // Win16 dotted-pen pattern is verified.
  void focus_rect(const PanelRect& rect, const PanelRect& clip_to) {
    const auto toggle = [&](int px, int py) {
      if (!clip_to.contains(px, py) || px < 0 || py < 0 ||
          px >= raster_.width || py >= raster_.height ||
          ((px + py) & 1) != 0) {
        return;
      }
      raster_.pixels[offset(px, py)] ^= 0x00FFFFFFU;
    };
    if (rect.width <= 0 || rect.height <= 0) return;
    for (int px = rect.x; px < rect.x + rect.width; ++px) {
      toggle(px, rect.y);
      if (rect.height > 1) toggle(px, rect.y + rect.height - 1);
    }
    for (int py = rect.y + 1; py < rect.y + rect.height - 1; ++py) {
      toggle(rect.x, py);
      if (rect.width > 1) toggle(rect.x + rect.width - 1, py);
    }
  }

  [[nodiscard]] OriginalWorldRaster take() { return std::move(raster_); }
  [[nodiscard]] OriginalWorldRaster& raster() { return raster_; }

 private:
  [[nodiscard]] std::size_t offset(int x, int y) const {
    return static_cast<std::size_t>(y) * raster_.width + x;
  }
  static PanelRect intersect(const PanelRect& a, const PanelRect& b) {
    const int left = std::max(a.x, b.x);
    const int top = std::max(a.y, b.y);
    const int right = std::min(a.x + a.width, b.x + b.width);
    const int bottom = std::min(a.y + a.height, b.y + b.height);
    return {left, top, std::max(0, right - left), std::max(0, bottom - top)};
  }
  [[nodiscard]] PanelRect clip(const PanelRect& rect) const {
    return intersect(rect, {0, 0, raster_.width, raster_.height});
  }

  OriginalWorldRaster raster_{};
};

void draw_frame_row(Canvas& canvas, const PanelRect& client, int surface_width,
                    bool active) {
  // 1078:00c6 paints the 8-pixel bar at the palette's surface width; the
  // client clips it.
  canvas.blit(render_original_palette_frame(surface_width, active,
                                            kActiveCaptionRgb),
              client.x, client.y, client);
}

}  // namespace

OriginalWorldRaster compose_window(const Session& session) {
  const auto& layout = session.layout();
  const auto& resources = session.resources();
  const auto* document = session.document();
  const auto& input = session.world_input();
  Canvas canvas(layout.window_width, layout.window_height);
  canvas.fill({0, 0, layout.window_width, layout.window_height}, kDesktopRgb);

  const auto draw_main = [&] {
    // TEMP nonclient strips (frame, caption, menu, scrollbars), then the
    // retained backing at the client origin.
    const auto& main = layout.main_client;
    canvas.fill(layout.main_outer, kNonclientRgb);
    canvas.outline(layout.main_outer, kBorderRgb);
    // The caption and TOWER_MENU's bar, with the open title highlighted.
    draw_main_caption_and_menu_bar(canvas.raster(), session.menu(), layout,
                                   session.menu_view(),
                                   session.window_title());
    canvas.blit(session.main_raster(), main.x, main.y, main);
  };
  const auto* palette = session.palette_runtime().initialized
                            ? &session.palette_runtime().colors
                            : nullptr;
  const auto active = session.active_panel();

  const auto draw_map = [&] {
    // MAPWNDPROC 1168:02be: white client, frame, Map backing at y 8, then
    // 1058:094c's focus rectangle (1080:038e of the live view).
    const auto& client = layout.map_client;
    canvas.outline(layout.map_outer, kBorderRgb);
    canvas.fill(client, kWhiteRgb);
    draw_frame_row(canvas, client, kOriginalMapWidth, active == Panel::map);
    // 1168:02be: the rating check reads the live document, the raster
    // the presentation copy (every shaft drawn while Simulate isolates).
    const bool disabled = document && document->header.rating == 1U;
    std::optional<OriginalTdtDocument> storage{};
    canvas.blit(render_original_map(resources,
                                    session.presentation_document(storage),
                                    input.map_mode, disabled, palette),
                client.x, client.y + kOriginalMapClientTop, client);
    const auto& viewport = session.viewport();
    const auto focus = original_map_view_rect(viewport.x, viewport.y,
                                              viewport.width, viewport.height);
    canvas.focus_rect({client.x + focus.left,
                       client.y + kOriginalMapClientTop + focus.top,
                       focus.right - focus.left, focus.bottom - focus.top},
                      client);
  };
  const auto draw_info = [&] {
    // INFOWNDPROC 1120:0215: white client, frame at the 431-pixel surface
    // width, then the Info content as the session last painted it
    // (Session::info_raster, rows 8.. only).
    const auto& client = layout.info_client;
    canvas.outline(layout.info_outer, kBorderRgb);
    canvas.fill(client, kWhiteRgb);
    draw_frame_row(canvas, client, kOriginalInfoWidth, active == Panel::info);
    const auto& info = session.info_raster();
    canvas.blit(info, client.x, client.y,
                {client.x, client.y + kOriginalInfoClientTop, client.width,
                 client.height - kOriginalInfoClientTop});
  };
  const auto draw_command = [&] {
    // CMDBTNWNDPROC 1050:010d: white client, frame at the 64-pixel surface
    // width, then 1080:05a1's palette at y 8, clipped to the client.
    const auto& client = layout.command_client;
    canvas.outline(layout.command_outer, kBorderRgb);
    canvas.fill(client, kWhiteRgb);
    draw_frame_row(canvas, client, kOriginalCommandSurfaceWidth,
                   active == Panel::command);
    canvas.blit(render_original_command_palette(
                    resources, session.active_rating_state(),
                    input.build_mode_enabled, input.command_mode,
                    client.width, client.height,
                    input.command_toggle_pressed),
                client.x, client.y + kOriginalCommandPaintOffsetY, client);
  };

  // Bottom to top, the order Session::pointer_event hit-tests in reverse.
  for (const auto panel : kPanelZOrder) {
    if (!session.panel_visible(panel)) continue;
    switch (panel) {
      case Panel::main: draw_main(); break;
      case Panel::info: draw_info(); break;
      case Panel::map: draw_map(); break;
      case Panel::command: draw_command(); break;
      case Panel::elevator_control:
      case Panel::none: break;
    }
  }

  // Elevator Control: TOPMOST above the palettes (1098:08c9; TEMP, owner
  // decision D2: also while the window is inactive), below the modals.
  if (const auto* control = session.elevator_control()) {
    draw_dialog(canvas.raster(), *control, resources);
  }
  if (const auto* selector = session.active_selector()) {
    // 1050:0978 at the 1050:05d6 placement; the window bottom stands in for
    // the desktop bottom.
    const PanelRect rect{selector->anchor_left(),
                         selector->top(layout.window_height),
                         selector->width(), selector->height()};
    canvas.blit(render_original_command_selector(
                    resources, selector->group(), selector->highlight(),
                    input.build_mode_enabled),
                rect.x, rect.y, rect);
  }
  // Open dialogs, bottom to top (sdl_dialog.hpp), above the panels.
  for (const auto* dialog : session.dialogs()) {
    draw_dialog(canvas.raster(), *dialog, resources);
  }
  // An open drop-down (and cascade) is above every window, as USER's
  // popup menu windows are.
  draw_menu_popups(canvas.raster(), session.menu(), layout,
                   session.menu_view());
  return canvas.take();
}

}  // namespace simtower::sdl

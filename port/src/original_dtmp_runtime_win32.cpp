#include "original_dtmp_runtime_win32.hpp"

#include "original_dialog_paint.hpp"
#include "original_dib_win32.hpp"
#include "original_draw_context_win32.hpp"

#include <cstdint>

namespace simtower {
void configure_original_dtmp_window(HWND dialog,
                                    const OriginalDtmp& dtmp,
                                    const OriginalResources& resources,
                                    HPALETTE logical_palette) {
  const auto size = original_dtmp_window_size(dtmp, resources);
  const auto plan = original_dtmp_configuration_plan(dtmp);
  const auto resize_window = [&] {
    if (size.width == 0) return;
    RECT current{};
    GetWindowRect(dialog, &current);
    MoveWindow(dialog, current.left, current.top, size.width, size.height, FALSE);
  };

  if (plan.resize_before_dc) resize_window();

  HDC dc = GetDC(dialog);
  if (dc) {
    if (plan.realize_palette && logical_palette) {
      // 1070:0185-019b selects DS:795e, runs 11e0:0e84's obsolete WAVMIX
      // message pump, then realizes the palette. waveOut needs no pump.
      SelectPalette(dc, logical_palette, FALSE);
      RealizePalette(dc);
    }
    if (plan.set_update_current_position) {
      SetTextAlign(dc, GetTextAlign(dc) | TA_UPDATECP);
    }
    ReleaseDC(dialog, dc);
  }

  if (plan.resize_after_dc) resize_window();
}

void render_original_dtmp(HWND dialog,
                          HDC destination,
                          const OriginalDtmp& dtmp,
                          const OriginalResources& resources) {
  // The 1070:0231 walk is the shared op list (original_dialog_paint.hpp);
  // only the GDI and window calls stay here.
  for (const auto& op : original_dtmp_render_ops(dtmp)) {
    if (op.kind == OriginalDtmpRenderOp::Kind::bitmap) {
      if (destination) {
        draw_original_dib(destination,
                          resources.find("BITMAP", op.resource_id), op.x,
                          op.y);
      }
      continue;
    }
    HWND child = GetDlgItem(dialog, static_cast<int>(op.child_id));
    if (!IsWindow(child)) {
      continue;
    }
    MoveWindow(child, op.rect.left, op.rect.top,
               op.rect.right - op.rect.left, op.rect.bottom - op.rect.top,
               TRUE);
    ShowWindow(child, SW_SHOW);
  }
}

void paint_original_dialog_chrome(HWND dialog,
                                  HDC destination,
                                  const OriginalDtmp& dtmp) {
  // The drawing is the shared 1068:0567 painter (original_dialog_paint.cpp),
  // issued through GDI; only the two window queries stay here.
  RECT client{};
  GetClientRect(dialog, &client);
  {
    OriginalGdiDrawContext context(destination,
                                   kOriginalDialogGdiFailureText);
    paint_original_dtmp_chrome(
        context, {client.left, client.top, client.right, client.bottom}, dtmp,
        [dialog](std::uint16_t id) {
          return IsWindow(GetDlgItem(dialog, static_cast<int>(id))) != FALSE;
        });
  }
  // Exact result of the palette-aware 11e0:0633 SetBkColor wrapper used by
  // the 1068:0567 DTMP renderer: the dialog drawing background is white.
  SetBkColor(destination, RGB(255, 255, 255));
}

}  // namespace simtower

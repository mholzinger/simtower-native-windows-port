#pragma once

// Portable painters of the Information dialogs over OriginalDrawContext:
// the text and meter helpers TENANTINFODLOGFILTER's painter (1100:1716) and
// the Person Information painter share, and the whole Facility Information
// client (DIALOG/DTMP 748-760). They draw what the Win32 GDI painter
// paint_original_facility_dialog in native_main.cpp draws, in its order
// (chrome, preview, meters, portraits, texts, advisories); the Win32 host
// keeps its own GDI painter (its meters use PALETTEINDEX brushes, which the
// portable context does not issue). The SDL host paints with these. No
// windows.h here.

#include "original_dialog_paint.hpp"
#include "original_draw_context.hpp"
#include "original_dtmp.hpp"
#include "original_information.hpp"
#include "original_resources.hpp"
#include "original_world.hpp"

#include <cstddef>
#include <string_view>

namespace simtower {

// The one-based DTMP rectangle, or null when it does not exist.
[[nodiscard]] const OriginalDtmpRect* original_information_rectangle(
    const OriginalDtmp& dtmp,
    std::size_t one_based_index) noexcept;

// paint_original_person_text: MoveToEx(left + offset_x, top + offset_y) and
// TextOut under the caller's TA_UPDATECP; nothing for a missing or unset
// (left or bottom zero) rectangle or an empty text.
void paint_original_information_text(OriginalDrawContext& context,
                                     const OriginalDtmpRect* rectangle,
                                     int offset_x,
                                     int offset_y,
                                     std::string_view text);

// paint_original_person_meter (1100:1fad / 11e0:02bd): the rectangle
// deflated by two, filled to the classified value (maximum 300), and with
// thresholds the two black ticks.
void paint_original_information_meter(OriginalDrawContext& context,
                                      const OriginalDtmpRect* rectangle,
                                      const OriginalInformationMeter& meter,
                                      bool reverse,
                                      bool thresholds);

// draw_original_facility_preview (1100:4439): the item-2 container filled
// with 0xCCCCCC, then the retained world snapshot stretched (COLORONCOLOR)
// into 4d1d's destination.
void paint_original_facility_preview(OriginalDrawContext& context,
                                     const OriginalDtmp& dtmp,
                                     const OriginalFacilityPreview& preview,
                                     const OriginalWorldRaster& raster);

// The whole Facility Information client: the 1068:0567 chrome over client,
// then (for valid information) the preview, the meters, the portraits
// (BITMAP sprite.bitmap_id frames, 8 pixels apart, 24 tall) and the texts
// in the 13-pixel dialog font.
void paint_original_facility_information(
    OriginalDrawContext& context,
    const OriginalResources& resources,
    const OriginalDtmp& dtmp,
    const OriginalFacilityInformation& information,
    const OriginalWorldRaster& preview_raster,
    const OriginalRect& client,
    const OriginalDialogHasChild& has_child);

// 1100:364a's portrait (paint_original_person_dialog in native_main.cpp):
// frame portrait_frame of BITMAP 700 (702 named, 703 VIP), 8 pixels wide
// (16 from frame 6) and 24 tall at column frame * 8, stretched to twice its
// width and 47 pixels tall at item 2's (left + 20, top + 2), 8 further left
// for the wide frames. Nothing for an unset item 2 or no frame.
void paint_original_person_portrait(
    OriginalDrawContext& context,
    const OriginalResources& resources,
    const OriginalDtmp& dtmp,
    const OriginalPersonInformation& information);

// The whole Person Information client (DIALOG/DTMP 763 / 764, 1100:151b):
// the 1068:0567 chrome over client, then (for valid information) the
// portrait, the name (item 3), origin (item 5) and activity (item 7) in
// the 13-pixel dialog font under TA_UPDATECP, and the evaluation (item 6,
// reverse with its thresholds) and stress (item 8) meters. DTMP 764 zeroes
// items 6, 8, 10 and 12, so it has no meters.
void paint_original_person_information(
    OriginalDrawContext& context,
    const OriginalResources& resources,
    const OriginalDtmp& dtmp,
    const OriginalPersonInformation& information,
    const OriginalRect& client,
    const OriginalDialogHasChild& has_child);

// The whole Transport Information client (DIALOG/DTMP 761 / 762,
// ELVINFODLOGFILTER's 1100:1b53 / ESCINFODLOGFILTER's 1100:1cbb after the
// 1070:0231 chrome): the chrome over client, then (for valid text) the
// first and second lines at items 3 and 6 + (8, 1) (11e0:008d, 1208:0cf5)
// in the 13-pixel dialog font under TA_UPDATECP, then the portrait strip
// (1100:327f / 3431: BITMAP sprite.bitmap_id columns frame * 8, 24 tall,
// copied unscaled). The order and arguments of the Win32 GDI painters
// paint_original_information_text and paint_original_information_sprites
// in native_main.cpp (over OriginalTransportDialogContext).
void paint_original_transport_information(
    OriginalDrawContext& context,
    const OriginalResources& resources,
    const OriginalDtmp& dtmp,
    const OriginalTransportInformationText& text,
    const OriginalRect& client,
    const OriginalDialogHasChild& has_child);

// The 1100:03ac world snapshot the preview stretches: the world at the
// preview's view, at least as large as the 1100:4514 backing of a Main
// client of main_width x main_height (an empty raster without a preview).
// The backing's floor and tenant are information.floor / tenant_index,
// which a valid original_facility_information sets from its arguments
// (facility_information_controller_tests checks it for every tenant).
[[nodiscard]] OriginalWorldRaster render_original_facility_preview(
    const OriginalResources& resources,
    const OriginalTdtDocument& document,
    const OriginalFacilityInformation& information,
    int main_width,
    int main_height,
    const OriginalWorldPalette* palette);

}  // namespace simtower

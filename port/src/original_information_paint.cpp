#include "original_information_paint.hpp"

#include "original_ui.hpp"

#include <algorithm>
#include <cstdint>

namespace simtower {
namespace {

// A COLORREF (0x00BBGGRR) as an OriginalRgb (0x00RRGGBB).
constexpr OriginalRgb colorref_to_rgb(std::uint32_t colorref) noexcept {
  return ((colorref & 0xffU) << 16U) | (colorref & 0xff00U) |
         ((colorref >> 16U) & 0xffU);
}

OriginalRect deflated_by_two(const OriginalDtmpRect& rectangle) noexcept {
  return {rectangle.left + 2, rectangle.top + 2, rectangle.right - 2,
          rectangle.bottom - 2};
}

bool empty(const OriginalRect& rect) noexcept {
  return rect.right <= rect.left || rect.bottom <= rect.top;
}

}  // namespace

const OriginalDtmpRect* original_information_rectangle(
    const OriginalDtmp& dtmp, std::size_t one_based_index) noexcept {
  if (one_based_index == 0U || one_based_index > dtmp.rectangles.size()) {
    return nullptr;
  }
  return &dtmp.rectangles[one_based_index - 1U];
}

void paint_original_information_text(OriginalDrawContext& context,
                                     const OriginalDtmpRect* rectangle,
                                     int offset_x, int offset_y,
                                     std::string_view text) {
  if (!rectangle || rectangle->left == 0 || rectangle->bottom == 0 ||
      text.empty()) {
    return;
  }
  context.move_to(static_cast<int>(rectangle->left) + offset_x,
                  static_cast<int>(rectangle->top) + offset_y);
  context.text_out(text);
}

void paint_original_information_meter(OriginalDrawContext& context,
                                      const OriginalDtmpRect* rectangle,
                                      const OriginalInformationMeter& meter,
                                      bool reverse, bool thresholds) {
  if (!rectangle || rectangle->left == 0 || rectangle->bottom == 0 ||
      !meter.visible) {
    return;
  }
  auto fill = deflated_by_two(*rectangle);
  if (empty(fill)) return;
  fill.right = original_information_meter_fill_right(
      fill.left, fill.right, meter.value, meter.maximum, reverse);
  if (fill.right > fill.left) {
    const auto colorref = original_information_meter_colorref(meter.band);
    context.fill_rect(fill, colorref_to_rgb(colorref));
  }
  if (!thresholds) return;
  const auto inner = deflated_by_two(*rectangle);
  context.push_solid_pen(0x000000U);
  for (const auto threshold : {meter.lower, meter.upper}) {
    const int x = original_information_meter_fill_right(
        inner.left, inner.right, threshold, meter.maximum, true);
    context.move_to(x, inner.top);
    context.line_to(x, inner.bottom - 1);
  }
  context.pop_pen();
}

void paint_original_facility_preview(OriginalDrawContext& context,
                                     const OriginalDtmp& dtmp,
                                     const OriginalFacilityPreview& preview,
                                     const OriginalWorldRaster& raster) {
  const auto* source = original_information_rectangle(dtmp, 2U);
  if (!source || !preview.valid()) return;
  const OriginalRect container{source->left, source->top, source->right,
                               source->bottom};
  context.fill_rect(container, 0xccccccU);
  if (raster.width <= 0 || raster.height <= 0 || raster.pixels.empty()) {
    return;
  }
  const auto draw = original_facility_preview_destination(
      preview, container.left, container.top, container.right,
      container.bottom);
  if (draw.width() <= 0 || draw.height() <= 0) return;
  context.stretch_pixels(raster.pixels, raster.width, raster.height,
                         {0, 0, preview.width, preview.height},
                         {draw.left, draw.top, draw.right, draw.bottom});
}

void paint_original_facility_information(
    OriginalDrawContext& context, const OriginalResources& resources,
    const OriginalDtmp& dtmp, const OriginalFacilityInformation& information,
    const OriginalWorldRaster& preview_raster, const OriginalRect& client,
    const OriginalDialogHasChild& has_child) {
  paint_original_dtmp_chrome(context, client, dtmp, has_child);
  if (!information.valid) return;
  paint_original_facility_preview(context, dtmp, information.preview,
                                  preview_raster);
  const auto rect = [&](std::size_t index) {
    return original_information_rectangle(dtmp, index);
  };
  paint_original_information_meter(context, rect(6U), information.evaluation,
                                   true, true);
  paint_original_information_meter(context, rect(6U),
                                   information.commercial_meter, false, false);
  for (const auto& sprite : information.person_sprites) {
    // draw_original_dib_region_scaled with a 24-pixel source band equal to
    // the destination height: an unscaled copy of the frame's columns.
    draw_original_clipped_dib(
        context, resources.find("BITMAP", sprite.bitmap_id),
        sprite.destination_x - sprite.frame * 8, sprite.destination_y,
        {sprite.destination_x, sprite.destination_y,
         sprite.destination_x + sprite.width,
         sprite.destination_y + std::min(sprite.height, 24)});
  }
  OriginalSavedDrawState saved(context);
  context.set_text_align(kOriginalTaUpdateCp);
  context.set_bk_transparent();
  context.set_text_color(0x000000U);
  OriginalSelectedFont font(context, 13);
  paint_original_information_text(context, rect(3U), 4, 4,
                                  information.display_name);
  paint_original_information_text(context, rect(5U), 4, 3,
                                  information.occupancy_text);
  paint_original_information_text(context, rect(9U), 4, 3,
                                  information.age_text);
  paint_original_information_text(context, rect(5U), 4, 3,
                                  information.movie_length_text);
  paint_original_information_text(context, rect(6U), 4, 3,
                                  information.movie_title);
  paint_original_information_text(context, rect(10U), 4, 3,
                                  information.movie_income_text);
  paint_original_information_text(context, rect(6U), 4, 3,
                                  information.commercial_value_text);
  paint_original_information_text(context, rect(9U), 4, 3,
                                  information.yesterday_profit_text);
  for (std::size_t line = 0U; line < information.advisory_line_count;
       ++line) {
    const auto offset = original_facility_advisory_text_offset(line);
    paint_original_information_text(context, rect(8U), offset.x, offset.y,
                                    information.advisory_lines[line]);
  }
}

void paint_original_person_portrait(
    OriginalDrawContext& context, const OriginalResources& resources,
    const OriginalDtmp& dtmp, const OriginalPersonInformation& information) {
  const auto* portrait = original_information_rectangle(dtmp, 2U);
  if (!portrait || portrait->left == 0 || portrait->bottom == 0 ||
      information.portrait_frame < 0) {
    return;
  }
  int bitmap_id = 700;
  if (information.portrait_variant == OriginalPersonPortraitVariant::named) {
    bitmap_id = 702;
  } else if (information.portrait_variant ==
             OriginalPersonPortraitVariant::vip) {
    bitmap_id = 703;
  }
  const int source_width = information.portrait_frame >= 6 ? 16 : 8;
  int x = static_cast<int>(portrait->left) + 20;
  if (source_width == 16) x -= 8;
  const int y = static_cast<int>(portrait->top) + 2;
  const int source_x = information.portrait_frame * 8;
  context.draw_dib_region_scaled(resources.find("BITMAP", bitmap_id),
                                 {source_x, 0, source_x + source_width, 24},
                                 {x, y, x + source_width * 2, y + 47});
}

void paint_original_person_information(
    OriginalDrawContext& context, const OriginalResources& resources,
    const OriginalDtmp& dtmp, const OriginalPersonInformation& information,
    const OriginalRect& client, const OriginalDialogHasChild& has_child) {
  paint_original_dtmp_chrome(context, client, dtmp, has_child);
  if (!information.valid) return;
  paint_original_person_portrait(context, resources, dtmp, information);
  const auto rect = [&](std::size_t index) {
    return original_information_rectangle(dtmp, index);
  };
  {
    OriginalSavedDrawState saved(context);
    context.set_text_align(kOriginalTaUpdateCp);
    context.set_bk_transparent();
    context.set_text_color(0x000000U);
    OriginalSelectedFont font(context, 13);
    paint_original_information_text(context, rect(3U), 4, 4,
                                    information.display_name);
    paint_original_information_text(context, rect(5U), 4, 3,
                                    information.origin_text);
    paint_original_information_text(context, rect(7U), 4, 3,
                                    information.activity_text);
  }
  paint_original_information_meter(context, rect(6U), information.evaluation,
                                   true, true);
  paint_original_information_meter(context, rect(8U), information.stress,
                                   false, false);
}

void paint_original_transport_information(
    OriginalDrawContext& context, const OriginalResources& resources,
    const OriginalDtmp& dtmp, const OriginalTransportInformationText& text,
    const OriginalRect& client, const OriginalDialogHasChild& has_child) {
  paint_original_dtmp_chrome(context, client, dtmp, has_child);
  if (!text.valid) return;
  {
    OriginalSavedDrawState saved(context);
    context.set_text_align(kOriginalTaUpdateCp);
    context.set_bk_transparent();
    context.set_text_color(0x000000U);
    OriginalSelectedFont font(context, 13);
    // As the Win32 painter: any existing rectangle, set or not.
    const auto line = [&](std::size_t index, const std::string& value) {
      const auto* rectangle = original_information_rectangle(dtmp, index);
      if (!rectangle) return;
      const auto origin = original_relative_gdi_position(
          static_cast<std::int16_t>(rectangle->left),
          static_cast<std::int16_t>(rectangle->top), 8, 1);
      context.move_to(origin.x, origin.y);
      context.text_out(value);
    };
    line(3U, text.primary);
    line(6U, text.secondary);
  }
  for (const auto& sprite : text.person_sprites) {
    const int source_x = sprite.frame * 8;
    context.draw_dib_region_scaled(
        resources.find("BITMAP", sprite.bitmap_id),
        {source_x, 0, source_x + sprite.width, 24},
        {sprite.destination_x, sprite.destination_y,
         sprite.destination_x + sprite.width,
         sprite.destination_y + sprite.height});
  }
}

OriginalWorldRaster render_original_facility_preview(
    const OriginalResources& resources, const OriginalTdtDocument& document,
    const OriginalFacilityInformation& information, int main_width,
    int main_height, const OriginalWorldPalette* palette) {
  const auto& preview = information.preview;
  if (!preview.valid()) return {};
  // run_original_facility_information_dialog: the 1100:4514 temporary grid
  // of the Main client (eight-pixel cells, 36-pixel floors, one spare).
  const auto visible_cells =
      static_cast<std::int16_t>((std::max(0, main_width) + 7) / 8 + 1);
  const auto visible_floors =
      static_cast<std::int16_t>((std::max(0, main_height) + 35) / 36 + 1);
  const auto backing = original_facility_preview_backing_counts(
      document, information.floor, information.tenant_index, visible_cells,
      visible_floors);
  return render_original_world(
      resources, &document, preview.view_x, preview.view_y,
      std::max(preview.width, static_cast<int>(backing.visible_cells) * 8),
      std::max(preview.height, static_cast<int>(backing.visible_floors) * 36),
      std::span<const OriginalElevatorTransferVisual>{}, palette);
}

}  // namespace simtower

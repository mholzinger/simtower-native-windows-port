#include "original_dialog_win32.hpp"

#include <stdexcept>
#include <string>
#include <string_view>

namespace simtower {

static_assert(kOriginalDialogStyleSetFont == DS_SETFONT);

namespace {

std::wstring cp1252_to_wide(std::string_view text) {
  if (text.empty()) {
    return {};
  }
  const int count = MultiByteToWideChar(
      1252, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (count <= 0) {
    throw std::runtime_error("Could not decode original dialog CP1252 text");
  }
  std::wstring result(static_cast<std::size_t>(count), L'\0');
  MultiByteToWideChar(1252, 0, text.data(), static_cast<int>(text.size()),
                      result.data(), count);
  return result;
}

class TemplateWriter {
 public:
  void align4() {
    while (data_.size() % 4U) {
      data_.push_back(std::byte{0});
    }
  }
  void u16(std::uint16_t value) {
    data_.push_back(static_cast<std::byte>(value & 0xFFU));
    data_.push_back(static_cast<std::byte>(value >> 8U));
  }
  void i16(std::int16_t value) { u16(static_cast<std::uint16_t>(value)); }
  void u32(std::uint32_t value) {
    u16(static_cast<std::uint16_t>(value & 0xFFFFU));
    u16(static_cast<std::uint16_t>(value >> 16U));
  }
  void wide(std::string_view text) {
    for (const wchar_t character : cp1252_to_wide(text)) {
      u16(static_cast<std::uint16_t>(character));
    }
    u16(0);
  }
  void value(const OriginalDialogValue& value) {
    switch (value.kind) {
      case OriginalDialogValue::Kind::none:
        u16(0);
        break;
      case OriginalDialogValue::Kind::text:
        wide(value.text);
        break;
      case OriginalDialogValue::Kind::ordinal:
        u16(0xFFFFU);
        u16(value.ordinal);
        break;
    }
  }
  [[nodiscard]] std::vector<std::byte> finish() && { return std::move(data_); }

 private:
  std::vector<std::byte> data_;
};

}  // namespace

std::vector<std::byte> build_native_dialog_template(
    const OriginalDialog& dialog) {
  if (dialog.items.size() > 0xFFFFU) {
    throw std::runtime_error("Original dialog contains too many items");
  }

  TemplateWriter writer;
  writer.u32(dialog.style);
  writer.u32(0);  // no Win16 extended-style field exists
  writer.u16(static_cast<std::uint16_t>(dialog.items.size()));
  writer.i16(dialog.x);
  writer.i16(dialog.y);
  writer.i16(dialog.width);
  writer.i16(dialog.height);
  writer.value(dialog.menu);
  writer.value(dialog.window_class);
  writer.value(dialog.caption);
  if ((dialog.style & kOriginalDialogStyleSetFont) != 0U) {
    writer.u16(dialog.font_point_size);
    writer.wide(dialog.font_face);
  }

  for (const auto& item : dialog.items) {
    writer.align4();
    writer.u32(item.style);
    writer.u32(0);
    writer.i16(item.x);
    writer.i16(item.y);
    writer.i16(item.width);
    writer.i16(item.height);
    writer.u16(item.id);
    writer.value(item.window_class);
    writer.value(item.text);
    writer.u16(0);  // every supplied Win16 item has zero creation bytes
  }
  return std::move(writer).finish();
}

}  // namespace simtower

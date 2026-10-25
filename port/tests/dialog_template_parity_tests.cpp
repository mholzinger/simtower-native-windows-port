// Win32-only parity of the shared dialog models with the inline preparation
// run_original_event_dialog and run_original_about_dialog did before it moved
// to original_dialog_model.cpp: the DLGTEMPLATE bytes the Win32 host passes
// to DialogBoxIndirectParamW must be identical. The legacy_* functions are
// frozen copies of the old inline code; do not change them.
//
// Also: Facility Information's rent / price control 13 in DIALOG 748-753
// (groups 0..5) is a real ComboBox once the native template is created,
// the class Win32DialogOps (native_main.cpp) looks up to send CB_* rather
// than LB_* to it (the controller's tests record ops, not messages).
//
// Exit 77 (skipped) without the resource pack.

#include "original_about.hpp"
#include "original_dialog.hpp"
#include "original_dialog_model.hpp"
#include "original_dialog_win32.hpp"
#include "original_dtmp.hpp"
#include "original_resources.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace {

std::optional<std::vector<std::byte>> read_file(
    const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return std::nullopt;
  const std::vector<char> chars((std::istreambuf_iterator<char>(stream)),
                                std::istreambuf_iterator<char>());
  std::vector<std::byte> bytes(chars.size());
  for (std::size_t index = 0; index < chars.size(); ++index) {
    bytes[index] =
        static_cast<std::byte>(static_cast<unsigned char>(chars[index]));
  }
  return bytes;
}

// Frozen copy of run_original_event_dialog's inline preparation (native_main
// before the dialog-model extraction).
std::vector<std::byte> legacy_event_template(
    const simtower::OriginalResources& g_resources,
    const simtower::OriginalEventDialogRequest& request) {
  auto original = simtower::parse_original_dialog(
      g_resources.find("DIALOG", request.dialog_id));
  simtower::apply_original_dialog_font_point_size(original, 8U);
  for (auto& item : original.items) {
    if (item.text.kind != simtower::OriginalDialogValue::Kind::text) continue;
    item.text.text = simtower::format_original_dialog_caret_arguments(
        std::move(item.text.text), request.argument);
  }
  const auto item_three = std::ranges::find_if(
      original.items, [](const auto& item) { return item.id == 3U; });
  if (item_three != original.items.end() &&
      item_three->text.kind == simtower::OriginalDialogValue::Kind::text) {
    item_three->text.text = simtower::format_original_dialog_argument(
        std::move(item_three->text.text), request.argument);
  }
  return simtower::build_native_dialog_template(original);
}

// Frozen copy of run_original_about_dialog's inline preparation.
struct LegacyAbout {
  std::vector<std::byte> native{};
  simtower::OriginalAboutLayout layout{};
  std::vector<std::string> lines{};
  simtower::OriginalDtmp chrome{};
};

LegacyAbout legacy_about(const simtower::OriginalResources& g_resources) {
  const auto original = simtower::parse_original_dialog(
      g_resources.find("DIALOG", "TOWER_TITLE"));
  LegacyAbout result{};
  result.native = simtower::build_native_dialog_template(original);
  result.layout = simtower::derive_original_about_layout(g_resources);
  result.lines = simtower::original_about_credit_lines(g_resources);
  result.chrome.rectangles.push_back(result.layout.credits_outer);
  return result;
}

bool same_dtmp(const simtower::OriginalDtmp& a,
               const simtower::OriginalDtmp& b) {
  return a.bitmap_reference == b.bitmap_reference &&
         a.bitmap_resource_id == b.bitmap_resource_id &&
         a.width_or_header == b.width_or_header &&
         a.height_or_header == b.height_or_header &&
         a.rectangles == b.rectangles;
}

bool same_bytes(const std::vector<std::byte>& a,
                const std::vector<std::byte>& b) {
  return a.size() == b.size() &&
         (a.empty() || std::memcmp(a.data(), b.data(), a.size()) == 0);
}

INT_PTR CALLBACK inert_dialog_proc(HWND, UINT, WPARAM, LPARAM) {
  return FALSE;
}

// Creates DIALOG dialog_id hidden (WS_VISIBLE cleared: no window is shown)
// and returns the window class of its item `id` ("" when missing).
std::wstring created_item_class(const simtower::OriginalResources& resources,
                                std::uint16_t dialog_id, int id) {
  auto native = simtower::build_native_dialog_template(
      simtower::parse_original_dialog(resources.find("DIALOG", dialog_id)));
  DWORD style{};
  std::memcpy(&style, native.data(), sizeof(style));
  style &= ~static_cast<DWORD>(WS_VISIBLE);
  std::memcpy(native.data(), &style, sizeof(style));
  HWND dialog = CreateDialogIndirectParamW(
      GetModuleHandleW(nullptr),
      reinterpret_cast<const DLGTEMPLATE*>(native.data()), nullptr,
      inert_dialog_proc, 0);
  assert(dialog != nullptr);
  assert(!IsWindowVisible(dialog));
  std::wstring name;
  if (HWND child = GetDlgItem(dialog, id)) {
    wchar_t buffer[32]{};
    const int length = GetClassNameW(child, buffer, 32);
    name.assign(buffer, length > 0 ? static_cast<std::size_t>(length) : 0U);
  }
  DestroyWindow(dialog);
  return name;
}

}  // namespace

int main(int argc, char** argv) {
  assert(argc == 2);
  const auto pack = read_file(argv[1]);
  if (!pack) {
    std::printf("skipped: no resource pack at %s\n", argv[1]);
    return 77;
  }
  const simtower::OriginalResources resources(*pack);

  constexpr std::int32_t kArguments[] = {
      0, 7, -150000, std::numeric_limits<std::int32_t>::min(), 123456};
  int compared = 0;
  for (std::uint16_t id = 3000U; id <= 3040U; ++id) {
    for (const auto argument : kArguments) {
      const simtower::OriginalEventDialogRequest request{id, argument, 0};
      std::optional<std::vector<std::byte>> legacy;
      try {
        legacy = legacy_event_template(resources, request);
      } catch (const std::exception&) {
      }
      std::optional<simtower::OriginalEventDialogModel> model;
      try {
        model = simtower::prepare_original_event_dialog(resources, request);
      } catch (const std::exception&) {
      }
      // An id without a DIALOG resource fails in both (no dialog opens).
      assert(legacy.has_value() == model.has_value());
      if (!legacy) continue;
      assert(same_bytes(
          *legacy, simtower::build_native_dialog_template(model->dialog)));
      assert(same_dtmp(model->dtmp,
                       simtower::parse_original_dtmp(
                           resources.find("DTMP", id))));
      assert(model->argument == argument);
      ++compared;
    }
  }
  // 20 event dialogs (3000-3003, 3010-3014, 3020-3024, 3030-3034, 3040).
  assert(compared == 20 * 5);

  const auto legacy = legacy_about(resources);
  const auto about = simtower::prepare_original_about_dialog(resources);
  assert(same_bytes(legacy.native,
                    simtower::build_native_dialog_template(about.dialog)));
  assert(legacy.layout == about.layout);
  assert(legacy.lines == about.lines);
  assert(same_dtmp(legacy.chrome, about.chrome));

  // Facility Information rent groups 0..5 (DIALOG 0x2ec + group).
  for (std::uint16_t group = 0U; group <= 5U; ++group) {
    const auto dialog_id = static_cast<std::uint16_t>(0x2ecU + group);
    assert(lstrcmpiW(created_item_class(resources, dialog_id, 13).c_str(),
                     L"ComboBox") == 0);
  }

  std::printf("dialog template parity: %d event templates and About; "
              "facility combo 13 is a ComboBox in DIALOG 748-753\n",
              compared);
  return 0;
}

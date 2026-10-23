#include "original_tower_file.hpp"

#include <system_error>
#include <utility>

namespace simtower {

std::uint16_t original_tdt_load_error_string(
    OriginalTdtStatus status) noexcept {
  // 10d0:06b3-06c9 selects only the explicit version alerts; every other
  // parser failure uses STRL/1002 entry 2.
  if (status == OriginalTdtStatus::version_too_new) return 4;
  if (status == OriginalTdtStatus::version_too_old) return 5;
  return 2;
}

bool original_tower_untitled(const std::filesystem::path& path,
                             const std::filesystem::path& title) {
  return path.empty() ||
         title.native() == std::filesystem::path("untitled").native();
}

bool save_original_tower_to(OriginalTowerFileHost& host,
                            const std::filesystem::path& path,
                            bool ask_before_replace) {
  if (!host.document()) return false;
  if (ask_before_replace && std::filesystem::exists(path)) {
    if (!host.confirm_replace(path)) return false;
  }
  try {
    host.store_view_in_header();
    save_original_tdt_file(path, *host.document());
  } catch (const OriginalTdtFileError& error) {
    // 10d0:07e0/080e maps create and transfer failures to STRL/1002 entry 1.
    // Only a post-create transfer failure enters 1000:2140 and deletes the
    // partial target; an initial LCREAT failure has no owned file to remove.
    if (original_failed_save_deletes_target(error.operation())) {
      std::error_code ignored{};
      (void)std::filesystem::remove(path, ignored);
    }
    host.file_error(1);
    return false;
  } catch (const OriginalTdtError&) {
    // Native serialization precedes the file creation, while the original
    // creates first and then lets 0b3a report the transfer error. Keep the
    // externally visible deletion of its 080e failure branch.
    if (original_failed_save_deletes_target(
            OriginalTdtFileOperation::write)) {
      std::error_code ignored{};
      (void)std::filesystem::remove(path, ignored);
    }
    host.file_error(1);
    return false;
  }
  host.commit_tower_identity(path, path.filename());
  return true;
}

bool save_original_tower_as(OriginalTowerFileHost& host) {
  for (;;) {
    // 10d0:0401/0409 demotes the enabled palettes and restores Arrow before
    // every sheet, including retries after an invalid name.
    host.begin_file_dialog();
    const auto selected = host.pick_save_file();
    if (!selected) {
      host.end_file_dialog();
      return false;
    }
    const auto normalized = original_tdt_normalized_path(*selected);
    if (!original_tdt_basename_is_valid(normalized,
                                        host.path_separators())) {
      host.invalid_filename();
      continue;
    }
    const bool saved = save_original_tower_to(
        host, normalized, original_tdt_save_overwrite_prompt(true));
    host.end_file_dialog();
    return saved;
  }
}

bool save_original_tower(OriginalTowerFileHost& host) {
  const auto path = host.tower_path();
  if (original_tower_untitled(path, host.tower_title())) {
    return save_original_tower_as(host);
  }
  // 10d0:0305 clears the overwrite-prompt flag for a normal Save.
  return save_original_tower_to(host, path,
                                original_tdt_save_overwrite_prompt(false));
}

bool confirm_original_tower_transition(OriginalTowerFileHost& host,
                                       std::uint16_t action_index) {
  if (!original_tower_transition_requires_confirmation(host.document())) {
    return true;
  }
  const int result = host.transition_alert(action_index);
  if (result == 1) return save_original_tower(host);
  return result == 2;
}

bool load_original_tower_from_path(OriginalTowerFileHost& host,
                                   const std::filesystem::path& path,
                                   const std::filesystem::path& file_title) {
  host.remove_recent_file_items();
  host.install_empty_document();
  try {
    auto document = load_original_tdt_file(path);
    host.install_loaded_document(std::move(document));
    host.commit_tower_identity(
        path, file_title.empty() ? path.filename() : file_title);
    host.store_view_in_header();
    return true;
  } catch (const OriginalTdtError& error) {
    host.file_error(original_tdt_load_error_string(error.status()));
  } catch (const OriginalTdtFileError&) {
    // 10d0:0664 uses STRL/1002 entry 2 for an open failure.
    host.file_error(2);
  }
  host.begin_new_tower();
  return false;
}

bool open_original_tower(OriginalTowerFileHost& host) {
  if (!confirm_original_tower_transition(host, 2)) return false;
  // 10d0:0143/014b demotes the palettes and restores Arrow before COMMDLG;
  // 0205 restores them on cancellation, load failure/fallback and success.
  host.begin_file_dialog();
  const auto picked = host.pick_open_file();
  if (!picked) {
    host.end_file_dialog();
    return false;
  }
  const bool loaded =
      load_original_tower_from_path(host, picked->path, picked->file_title);
  host.end_file_dialog();
  return loaded;
}

}  // namespace simtower

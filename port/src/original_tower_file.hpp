#pragma once

// Portable New/Open/Save/Save As transactions of the original File menu
// (10d0:001d, 10d0:0122/0225/062a, 10d0:0305, 10d0:03f1 and 10d0:0777).
// The flow, the TDT codec calls, the overwrite prompt, the failed-save
// cleanup, the STRL/1002 error mapping and the identity commit are shared;
// the host supplies the pickers, prompts, palette demotion and the document
// installation it already owns. The Win32 host keeps its common-dialog
// setup (OPENFILENAMEW, hooks, Save As through GetOpenFileNameW) behind the
// picker hooks; the SDL host uses native pickers or scripted results.

#include "original_tdt.hpp"
#include "original_tdt_file.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace simtower {

// An accepted Open sheet: the full path and COMMDLG's lpstrFileTitle (empty
// when the picker reports none; the identity then uses the path's name).
struct OriginalPickedTowerFile {
  std::filesystem::path path{};
  std::filesystem::path file_title{};
};

class OriginalTowerFileHost {
 public:
  virtual ~OriginalTowerFileHost() = default;

  // The live tower, or null.
  virtual OriginalTdtDocument* document() = 0;
  // The document path and file title (DS:31c8); see original_tower_untitled.
  virtual std::filesystem::path tower_path() = 0;
  virtual std::filesystem::path tower_title() = 0;
  // Path, file title, clean state and the "SimTower - <title>" caption.
  virtual void commit_tower_identity(const std::filesystem::path& path,
                                     const std::filesystem::path& title) = 0;
  // The current Main view origin into the document header.
  virtual void store_view_in_header() = 0;

  // 10d0:0143/014b and 0401/0409: the palettes are demoted and Arrow is
  // restored before each COMMDLG sheet; 0205 restores the palettes after.
  virtual void begin_file_dialog() = 0;
  virtual void end_file_dialog() = 0;
  // The Open and Save As sheets; nullopt is a cancel.
  virtual std::optional<OriginalPickedTowerFile> pick_open_file() = 0;
  virtual std::optional<std::filesystem::path> pick_save_file() = 0;

  // MessageBeep and the YES/NO "replace" box; true for Yes.
  virtual bool confirm_replace(const std::filesystem::path& path) = 0;
  // "That is not a valid filename." (the Save As sheet then reopens).
  virtual void invalid_filename() = 0;
  // The directory separators of the basename rule
  // (original_tdt_basename_is_valid): the original's backslash by default.
  [[nodiscard]] virtual std::wstring_view path_separators() const {
    return L"\\";
  }
  // Alert 1000 with STRL/1002 entry `strl_index`.
  virtual void file_error(std::uint16_t strl_index) = 0;
  // Stops every mixer channel (11c8:0135(1)) when audio exists, then shows
  // alert 1001 with the file title and STRL/1001 entry `action_index`.
  // Returns 1 (save, then proceed), 2 (proceed) or anything else (cancel).
  virtual int transition_alert(std::uint16_t action_index) = 0;

  // 10d0:003e/0238 (original_recent_file_removal_ops).
  virtual void remove_recent_file_items() = 0;
  // 10d0:0225 before disk I/O: a fresh tower (carrying the process RNG) is
  // installed and the simulation and command state are reset.
  virtual void install_empty_document() = 0;
  // 10d0:062a success: install, reset, rating command, view restore and the
  // synchronous document transition.
  virtual void install_loaded_document(OriginalTdtDocument document) = 0;
  // 10d0:001d's New path (also the fallback after a failed load).
  virtual void begin_new_tower() = 0;
};

// Whether Save must fall back to Save As (10d0:0305): no path yet, or the
// "untitled" title of a New tower. Compares the title's native string.
[[nodiscard]] bool original_tower_untitled(
    const std::filesystem::path& path,
    const std::filesystem::path& title);

// STRL/1002 entry for a parser failure (10d0:06b3-06c9): 4 too new,
// 5 too old, 2 for everything else.
[[nodiscard]] std::uint16_t original_tdt_load_error_string(
    OriginalTdtStatus status) noexcept;

// 10d0:0777: optional replace prompt, write, cleanup and identity commit.
bool save_original_tower_to(OriginalTowerFileHost& host,
                            const std::filesystem::path& path,
                            bool ask_before_replace);
// 10d0:03f1: the Save As sheet, retried after an invalid name.
bool save_original_tower_as(OriginalTowerFileHost& host);
// 10d0:0305: Save, or Save As for an untitled tower.
bool save_original_tower(OriginalTowerFileHost& host);
// 10d0:2a8e: the STRL/1001 confirmation before New, Open and Exit.
bool confirm_original_tower_transition(OriginalTowerFileHost& host,
                                       std::uint16_t action_index);
// 10d0:0225/062a: load an accepted path; any failure shows its alert and
// falls back to New. Returns whether the file was loaded.
bool load_original_tower_from_path(OriginalTowerFileHost& host,
                                   const std::filesystem::path& path,
                                   const std::filesystem::path& file_title);
// 10d0:0122: confirmation, the Open sheet, then the load.
bool open_original_tower(OriginalTowerFileHost& host);

}  // namespace simtower

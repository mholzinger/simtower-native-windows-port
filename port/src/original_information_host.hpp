#pragma once

// What the three Information dialogs (Facility 1100:085b, Person
// 1100:0116, Transport 1100:0f10 / 1248) reach in common outside their own
// controls: the live tower, its tables, the resources, the dirty flag and
// the cursor. Each dialog's host interface extends it with its own nested
// dialogs; each host implements it once (Session::InformationHost in
// sdl_session.cpp, Win32InformationHost in native_main.cpp). No windows.h.

#include "original_resources.hpp"
#include "original_tables.hpp"
#include "original_tdt.hpp"

#include <cstdint>

namespace simtower {

// LoadCursor id of the portrait-panel cursor 1100:4fba / 5043 select over
// items 4 (and 9); 0 is the arrow.
inline constexpr std::uint16_t kOriginalInformationPortraitCursor = 1003U;

class OriginalInformationHostBase {
 public:
  virtual ~OriginalInformationHostBase() = default;
  // The live tower (null: none; every action then does nothing).
  virtual OriginalTdtDocument* document() = 0;
  virtual const OriginalPartTable& part() = 0;
  virtual const OriginalResources& resources() = 0;
  // A rename, rent or movie change: the document is dirty (the title).
  virtual void mark_document_dirty() = 0;
  // SetCursor(LoadCursor(id)); 0 is the arrow.
  virtual void set_cursor(std::uint16_t cursor_id) = 0;
};

}  // namespace simtower

#pragma once

// Composes the single SDL window raster: the panels in kPanelZOrder (Main
// with its caption and menu bar, Info, Map, Command), then a running Command
// selector and an open menu drop-down, at the positions of sdl_layout.hpp.
// Pure pixels (no SDL), so headless runs can write the same image
// (--headless-window).

#include "sdl_session.hpp"

#include "original_world.hpp"

namespace simtower::sdl {

[[nodiscard]] OriginalWorldRaster compose_window(const Session& session);

}  // namespace simtower::sdl

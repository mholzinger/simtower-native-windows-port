#pragma once

// Win32 GDI host for the portable DTMP runtime in original_dtmp_runtime.hpp.
#include "original_dtmp_runtime.hpp"

#include <windows.h>

namespace simtower {

void configure_original_dtmp_window(HWND dialog,
                                    const OriginalDtmp& dtmp,
                                    const OriginalResources& resources,
                                    HPALETTE logical_palette);

void render_original_dtmp(HWND dialog,
                          HDC destination,
                          const OriginalDtmp& dtmp,
                          const OriginalResources& resources);

// Exact native GDI translation of the dialog-frame path at 1068:0567 and
// its two recovered bevel routines at 11e0:06d9/0950.
void paint_original_dialog_chrome(HWND dialog,
                                  HDC destination,
                                  const OriginalDtmp& dtmp);

}  // namespace simtower

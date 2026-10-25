# Win32 inventory (step 4a)

Exact Win32 surface of the native port, taken from the MinGW import tables of
`simtower.exe` and of each object file (`__imp_` symbols), not from text search.
Generated 2026-10-21 from `development` plus the audio header split.

## By system DLL (whole app)

| DLL | Functions | Role |
| --- | ---: | --- |
| USER32.DLL | 99 | windows, messages, menus, dialogs, input, cursors, scrolling |
| GDI32.DLL | 37 | drawing, fonts, palettes, DIB blits |
| WINMM.DLL | 8 | waveOut audio |
| COMDLG32.DLL | 1 | Open dialog |
| KERNEL32.DLL | 83 | mostly C runtime and process plumbing; the game itself uses GetTickCount, resources and GetPrivateProfileInt |

## By source file

Count of distinct imported Win32 functions per object file. Counts are from
the 2026-10-21 import scan; file names are updated to the `*_win32.cpp` that
holds each call after the step 4e-2 split (`toupper` is CRT and stays in the
portable `original_resources_index.cpp`).

| File | Count | Functions |
| --- | ---: | --- |
| `native_main.cpp` | 143 | AdjustWindowRectEx, AnimatePalette, BeginPaint, CallWindowProcW, CheckMenuItem, ClientToScreen, ClipCursor, CreateDialogIndirectParamW, CreateFontW, CreatePen, CreateSolidBrush, CreateWindowExW, ... (+131) |
| `original_dtmp_runtime_win32.cpp` | 22 | CreatePen, CreateSolidBrush, DeleteObject, FillRect, GetClientRect, GetDC, GetDlgItem, GetTextAlign, GetWindowRect, InflateRect, IsRectEmpty, IsWindow, ... (+10) |
| `original_info_win32.cpp` | 16 | CreatePen, CreateSolidBrush, DeleteObject, DrawTextA, FillRect, GetTextExtentPoint32A, IntersectClipRect, LineTo, MoveToEx, RestoreDC, SaveDC, SelectObject, ... (+4) |
| `original_ui_win32.cpp` | 10 | AppendMenuW, CreateAcceleratorTableW, CreateIconFromResourceEx, CreateMenu, CreatePopupMenu, CreateSolidBrush, DeleteObject, DestroyMenu, FillRect, MultiByteToWideChar |
| `original_audio.cpp` | 7 | waveOutClose, waveOutGetNumDevs, waveOutOpen, waveOutPrepareHeader, waveOutReset, waveOutUnprepareHeader, waveOutWrite |
| `original_resources_win32.cpp` | 5 | FindResourceW, GetModuleHandleW, LoadResource, LockResource, SizeofResource |
| `original_palette_frame_win32.cpp` | 6 | CreateSolidBrush, DeleteObject, FillRect, FrameRect, GetStockObject, GetSysColor |
| `original_information_win32.cpp` | 5 | CreateSolidBrush, DeleteObject, FillRect, SetStretchBltMode, StretchDIBits |
| `original_font_win32.cpp` | 5 | CreateFontA, DeleteObject, EnumFontsA, GetDC, ReleaseDC |
| `original_dib_win32.cpp` | 3 | SetDIBitsToDevice, SetStretchBltMode, StretchDIBits |
| `original_world_win32.cpp` | 2 | CreatePalette, SetDIBitsToDevice |
| `original_dialog_win32.cpp` | 1 | MultiByteToWideChar |
| `original_command_palette_win32.cpp` | 1 | SetDIBitsToDevice |
| `original_alert_win32.cpp` | 1 | MessageBoxA |

## Portable today (no Win32 headers)

These compile with Apple clang (`-std=c++20`) after the step 4e-2 splits:
about, alert, audio_rules, command_palette, construction, dialog, dib, dtmp, dtmp_runtime,
elevator_control, finance, find, info, information, map, people, resources_index, simulation,
startup, tables, tdt, tdt_file, ui, wave, world.

Win32 host files (intentionally Win32-bound): native_main, audio (waveOut device), and every
`*_win32.cpp` (alert, command_palette, dialog, dib, dtmp_runtime, font, info, information,
palette_frame, resources, ui, world).

## Full function lists by DLL

### USER32.DLL

AdjustWindowRectEx, AppendMenuW, BeginPaint, CallWindowProcW, CheckMenuItem, ClientToScreen, ClipCursor, CreateAcceleratorTableW, CreateDialogIndirectParamW, CreateIconFromResourceEx, CreateMenu, CreatePopupMenu, CreateWindowExW, DefWindowProcW, DeleteMenu, DestroyAcceleratorTable, DestroyCursor, DestroyIcon, DestroyMenu, DestroyWindow, DialogBoxIndirectParamW, DispatchMessageW, DrawFocusRect, DrawTextA, DrawTextW, EnableMenuItem, EnableWindow, EndDialog, EndPaint, EnumChildWindows, FillRect, FrameRect, GetActiveWindow, GetAsyncKeyState, GetCapture, GetClientRect, GetCursorPos, GetDC, GetDesktopWindow, GetDlgItem, GetDlgItemTextW, GetMenu, GetMenuItemCount, GetPropW, GetScrollInfo, GetSubMenu, GetSysColor, GetSystemMetrics, GetTopWindow, GetWindowDC, GetWindowLongPtrW, GetWindowRect, GetWindowTextLengthW, GetWindowTextW, InflateRect, InvalidateRect, IsDialogMessageW, IsIconic, IsRectEmpty, IsWindow, KillTimer, LoadCursorW, LoadIconW, MapWindowPoints, MessageBeep, MessageBoxA, MoveWindow, OffsetRect, PeekMessageW, PostMessageW, PostQuitMessage, PtInRect, RegisterClassW, ReleaseCapture, ReleaseDC, RemovePropW, ScreenToClient, SendDlgItemMessageW, SendMessageW, SetActiveWindow, SetCapture, SetClassLongPtrW, SetCursor, SetDlgItemTextW, SetFocus, SetPropW, SetScrollInfo, SetScrollPos, SetScrollRange, SetTimer, SetWindowLongPtrW, SetWindowPos, SetWindowTextW, ShowWindow, TranslateAcceleratorW, TranslateMessage, UpdateWindow, ValidateRect, WinHelpW

### GDI32.DLL

AnimatePalette, CreateFontA, CreateFontW, CreatePalette, CreatePen, CreateSolidBrush, DeleteObject, EnumFontsA, GetBkColor, GetDeviceCaps, GetNearestPaletteIndex, GetRasterizerCaps, GetStockObject, GetTextAlign, GetTextColor, GetTextExtentPoint32A, GetTextExtentPoint32W, GetTextMetricsA, IntersectClipRect, LineTo, MoveToEx, Polygon, RealizePalette, RestoreDC, SaveDC, SelectObject, SelectPalette, SetBkColor, SetBkMode, SetDIBitsToDevice, SetStretchBltMode, SetTextAlign, SetTextColor, StretchDIBits, TextOutA, TextOutW, UpdateColors

### WINMM.DLL

waveOutClose, waveOutGetDevCapsW, waveOutGetNumDevs, waveOutOpen, waveOutPrepareHeader, waveOutReset, waveOutUnprepareHeader, waveOutWrite

### COMDLG32.DLL

GetOpenFileNameW


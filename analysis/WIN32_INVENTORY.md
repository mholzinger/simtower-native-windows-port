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

Count of distinct imported Win32 functions per object file.

| File | Count | Functions |
| --- | ---: | --- |
| `native_main.cpp` | 143 | AdjustWindowRectEx, AnimatePalette, BeginPaint, CallWindowProcW, CheckMenuItem, ClientToScreen, ClipCursor, CreateDialogIndirectParamW, CreateFontW, CreatePen, CreateSolidBrush, CreateWindowExW, ... (+131) |
| `original_dtmp_runtime.cpp` | 22 | CreatePen, CreateSolidBrush, DeleteObject, FillRect, GetClientRect, GetDC, GetDlgItem, GetTextAlign, GetWindowRect, InflateRect, IsRectEmpty, IsWindow, ... (+10) |
| `original_info.cpp` | 16 | CreatePen, CreateSolidBrush, DeleteObject, DrawTextA, FillRect, GetTextExtentPoint32A, IntersectClipRect, LineTo, MoveToEx, RestoreDC, SaveDC, SelectObject, ... (+4) |
| `original_ui.cpp` | 10 | AppendMenuW, CreateAcceleratorTableW, CreateIconFromResourceEx, CreateMenu, CreatePopupMenu, CreateSolidBrush, DeleteObject, DestroyMenu, FillRect, MultiByteToWideChar |
| `original_audio.cpp` | 7 | waveOutClose, waveOutGetNumDevs, waveOutOpen, waveOutPrepareHeader, waveOutReset, waveOutUnprepareHeader, waveOutWrite |
| `original_resources.cpp` | 6 | FindResourceW, GetModuleHandleW, LoadResource, LockResource, SizeofResource, toupper |
| `original_palette_frame.cpp` | 6 | CreateSolidBrush, DeleteObject, FillRect, FrameRect, GetStockObject, GetSysColor |
| `original_information.cpp` | 5 | CreateSolidBrush, DeleteObject, FillRect, SetStretchBltMode, StretchDIBits |
| `original_font.cpp` | 5 | CreateFontA, DeleteObject, EnumFontsA, GetDC, ReleaseDC |
| `original_dib.cpp` | 3 | SetDIBitsToDevice, SetStretchBltMode, StretchDIBits |
| `original_world.cpp` | 2 | CreatePalette, SetDIBitsToDevice |
| `original_dialog.cpp` | 1 | MultiByteToWideChar |
| `original_command_palette.cpp` | 1 | SetDIBitsToDevice |
| `original_alert.cpp` | 1 | MessageBoxA |

## Portable today (no Win32 headers)

These compile with Apple clang (`-std=c++20`) after the audio header split:
construction, dtmp, elevator_control, finance, people, simulation, tables, tdt, tdt_file, wave.

Still Win32-bound: native_main, about, alert, audio (device), command_palette, dialog, dib,
dtmp_runtime, find, font, info, information, map, palette_frame, resources, startup, ui, world.

## Full function lists by DLL

### USER32.DLL

AdjustWindowRectEx, AppendMenuW, BeginPaint, CallWindowProcW, CheckMenuItem, ClientToScreen, ClipCursor, CreateAcceleratorTableW, CreateDialogIndirectParamW, CreateIconFromResourceEx, CreateMenu, CreatePopupMenu, CreateWindowExW, DefWindowProcW, DeleteMenu, DestroyAcceleratorTable, DestroyCursor, DestroyIcon, DestroyMenu, DestroyWindow, DialogBoxIndirectParamW, DispatchMessageW, DrawFocusRect, DrawTextA, DrawTextW, EnableMenuItem, EnableWindow, EndDialog, EndPaint, EnumChildWindows, FillRect, FrameRect, GetActiveWindow, GetAsyncKeyState, GetCapture, GetClientRect, GetCursorPos, GetDC, GetDesktopWindow, GetDlgItem, GetDlgItemTextW, GetMenu, GetMenuItemCount, GetPropW, GetScrollInfo, GetSubMenu, GetSysColor, GetSystemMetrics, GetTopWindow, GetWindowDC, GetWindowLongPtrW, GetWindowRect, GetWindowTextLengthW, GetWindowTextW, InflateRect, InvalidateRect, IsDialogMessageW, IsIconic, IsRectEmpty, IsWindow, KillTimer, LoadCursorW, LoadIconW, MapWindowPoints, MessageBeep, MessageBoxA, MoveWindow, OffsetRect, PeekMessageW, PostMessageW, PostQuitMessage, PtInRect, RegisterClassW, ReleaseCapture, ReleaseDC, RemovePropW, ScreenToClient, SendDlgItemMessageW, SendMessageW, SetActiveWindow, SetCapture, SetClassLongPtrW, SetCursor, SetDlgItemTextW, SetFocus, SetPropW, SetScrollInfo, SetScrollPos, SetScrollRange, SetTimer, SetWindowLongPtrW, SetWindowPos, SetWindowTextW, ShowWindow, TranslateAcceleratorW, TranslateMessage, UpdateWindow, ValidateRect, WinHelpW

### GDI32.DLL

AnimatePalette, CreateFontA, CreateFontW, CreatePalette, CreatePen, CreateSolidBrush, DeleteObject, EnumFontsA, GetBkColor, GetDeviceCaps, GetNearestPaletteIndex, GetRasterizerCaps, GetStockObject, GetTextAlign, GetTextColor, GetTextExtentPoint32A, GetTextExtentPoint32W, GetTextMetricsA, IntersectClipRect, LineTo, MoveToEx, Polygon, RealizePalette, RestoreDC, SaveDC, SelectObject, SelectPalette, SetBkColor, SetBkMode, SetDIBitsToDevice, SetStretchBltMode, SetTextAlign, SetTextColor, StretchDIBits, TextOutA, TextOutW, UpdateColors

### WINMM.DLL

waveOutClose, waveOutGetDevCapsW, waveOutGetNumDevs, waveOutOpen, waveOutPrepareHeader, waveOutReset, waveOutUnprepareHeader, waveOutWrite

### COMDLG32.DLL

GetOpenFileNameW


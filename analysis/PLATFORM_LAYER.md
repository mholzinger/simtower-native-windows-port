# SDL3 platform layer design (step 4b-4d)

Status: design draft, 2026-10-22. Nothing here is implemented yet beyond the
header splits in `chore/portable-core`. Line references are to `development`
plus that branch (`N` = `port/src/native_main.cpp`).

## Decisions this design assumes

Recorded 2026-10-22:

| Topic | Decision |
| --- | --- |
| Windows | One SDL window. Command palette, Info, Map and Elevator Control become panels inside it; separate OS windows can come later. |
| Dialogs and menus | Drawn from the original Win16 DIALOG/MENU resources, identical on every platform. No native OS widgets. |
| Help | SIMTOWER.HLP converted to HTML, opened in the browser. |

## Goal and non-goals

- Goal: the game core and all presentation logic call a small `Platform`
  interface; Win32 and SDL3 are two implementations of it. The Win32 build
  keeps working during the migration, so its test suites stay green.
- Non-goal: pixel-identical reproduction of Windows' own controls (title
  bars, system menus). The game's own surfaces stay pixel-exact.

## What exists today (from the host map)

- Four window classes (Main, Command, Info, Map) plus two modeless dialogs
  (splash, Elevator Control) and about 13 modal dialogs, all built from the
  original DIALOG resources through `CreateDialogIndirectParamW` /
  `DialogBoxIndirectParamW` (N:481, N:4308, N:5766).
- Main, Map and Command already render into 32-bit rasters
  (`OriginalWorldRaster`) and only blit at the end (`draw_original_world_raster`,
  `original_command_palette.cpp:363`). Palette animation is applied while
  composing pixels, so the `HPALETTE`/`AnimatePalette` path is vestigial on a
  32-bit display.
- Info, the dialogs' custom painting and the Elevator Control grid draw with
  GDI directly into window DCs (FillRect, TextOut, DrawText, MoveTo/LineTo,
  8-bit DIB blits). These are the main rewrite targets.
- The main loop busy-spins `PeekMessage` (N:10016-10057), which pins one CPU
  core; the game uses no timers except two dialogs.
- The viewport (camera) is stored in the Main window's scrollbars
  (N:1863, N:1925, N:8259).
- Exact Win32 surface: see `WIN32_INVENTORY.md` (USER32 99, GDI32 37,
  WINMM 8, COMDLG32 1).

## Architecture

```
game core (portable today)        presentation logic (to make portable)
  simulation, people,               world/command/map renderers -> rasters
  construction, tdt, tables         info, dialogs, elevator control -> Canvas
            \                                   /
             +------------ Platform -----------+
                   |                       |
              Win32 backend           SDL3 backend
          (today's native_main)     (new, one window)
```

### The `Platform` interface (seams, in priority order)

1. **Present.** `present(SurfaceId, const Raster&, Rect dirty)`. Every game
   surface (Main view, Map, Command, Info, dialog bodies) is a 32-bit
   0x00RRGGBB raster. Win32 keeps `SetDIBitsToDevice`; SDL3 updates a
   streaming texture and composites panels.
2. **Event pump.** `wait_events(timeout_ms)` returning mouse, keyboard,
   resize, focus and quit events, replacing the busy spin. The idle step runs
   when the wait times out, which keeps the 96 ms simulation cadence without
   100% CPU.
3. **Clock.** `now_ms()`, a monotonic millisecond counter, replacing about
   25 direct `GetTickCount` calls. `original_time.hpp`'s coarse tick
   (ms >> 4) stays as is.
4. **Viewport.** Move the camera out of the scrollbars into a plain
   `OriginalViewport` struct owned by the game; the platform only draws and
   hit-tests scrollbar widgets.
5. **Canvas.** A software 2D drawing context over a raster: fill rect,
   frame/bevel, line, focus rect, 8-bit DIB blit (exact, scaled, clipped),
   and text with a given font and alignment. All GDI drawing in Info,
   dialogs and Elevator Control moves onto it, so both backends share one
   renderer.
6. **Input semantics.** Left button down/up/double-click/move with Ctrl and
   Shift, capture while dragging, cursor confinement for the elevator popup,
   and an async "button still down" query for the command selector.
7. **Cursors.** Load the original CURSOR resources into platform cursors;
   `resolve_original_cursor` (N:635) stays the mapping.
8. **Audio voices.** Two PCM voices: `play(format, samples, loop_count)`,
   `stop()`, `done()`. The portable `OriginalAudioArbiter` above it already
   exists; waveOut opens one device per voice, SDL3 uses one audio stream per
   voice.
9. **Services.** Open/save file pickers, message boxes, settings reader,
   open URL (help), asset pack loading from a file next to the executable
   (instead of a PE RCDATA resource).
10. **Window manager (later).** Create/show/move/z-order for panels. With
    decision 1c this is in-window layout only; real OS windows are a later
    extension behind the same panel API.

## UI chrome (decision 2a)

- **Menus.** Build a menu model from the TOWER_MENU resource (already parsed
  in `original_ui.cpp:80-150`), with command ids, checked and grayed state,
  and keep the existing command dispatch (N:9378-9541). Draw a Win3.1-style
  menu bar and drop-downs with the Canvas inside the SDL window. F1 is the
  only accelerator.
- **Dialogs.** A small widget runtime instantiated from the parsed
  `OriginalDialog` plus DTMP layouts. All 48 DIALOG resources use only seven
  control kinds: push button, default push button, static, combobox (6),
  listbox (2), edit (2), scrollbar (1). There are no checkboxes, radio
  buttons or owner-draw controls. Modal dialogs become a nested event loop
  run by the same pump, preserving the existing "modal locks Main input"
  rule (N:497).
- **Fonts.** The port probes for Arial then MS Sans Serif (`original_font_win32.cpp`).
  The SDL3 build needs a font source; options: (a) rasterize a bundled
  metric-compatible free font, (b) use the platform's installed fonts, or
  (c) extract bitmap fonts from a user-supplied Windows install. Open
  question; (a) is the most predictable.
- **File dialogs.** The original used the system COMMDLG Open dialog, so a
  native OS picker (SDL3 file dialog) is faithful here even under decision
  2a. Open question if you prefer a Win16-style picker.

## Help (decision 3a)

- Convert SIMTOWER.HLP to HTML once, at asset-extraction time, and open the
  index page with the platform's open-URL service. Output is derived game
  content and stays gitignored.
- Upstream's tools assume HelpDeco, LibreOffice and splitmrb
  (`tools/extract_help_topics.py`, `decode_shg.py`). Replacing that chain
  with a self-contained converter is a separate work item.

## Migration plan (each step keeps the Win32 build green)

| Step | Work | Proof |
| --- | --- | --- |
| 4e-2 | Finish header splits: `original_dib.hpp`, `original_font.hpp`, `original_ui.hpp`, `original_dtmp_runtime.hpp` | people/construction/world tests build natively on macOS |
| 5a | `Platform` interface + Win32 backend wrapping today's calls; clock, event pump without busy spin | all Windows suites pass; CPU no longer pinned |
| 5b | Viewport out of scrollbars | world/map tests |
| 5c | Canvas; port Info painter onto it | info tests compare Canvas output to the GDI path |
| 5d | SDL3 backend: window, present, input, clock, audio | first macOS build that shows the tower and runs the simulation |
| 5e | Menu bar and dialog runtime on the Canvas | dialogs open on macOS |
| 5f | Help conversion, file pickers, settings | feature parity |
| 6 | CI: macOS, Windows, Linux; asset-dependent tests gated | green CI |

## Risks and open questions

- Fonts (above): text metrics drive several layouts (Info fields, dialog
  text); a mismatch shifts pixels.
- Dialog runtime size: it is the largest single piece; the seven control
  kinds keep it bounded.
- Possible upstream bug spotted while mapping: `remove_original_recent_file_items`
  (N:3284) says it edits the File menu but calls `GetSubMenu(menu, 1)`
  (Options). Not yet verified against the binary.
- The original's 96 ms cadence must survive the switch from busy-spin to
  timed waits; measure with the existing simulation timing tests.

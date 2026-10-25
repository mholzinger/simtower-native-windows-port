cmake_minimum_required(VERSION 3.20)
project(SimTowerPort LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Portable core: builds on every platform. The Win32 host modules
# (*_win32, including the waveOut audio device) are added below on Windows
# only.
add_library(simtower_port_core STATIC
  src/original_about.cpp
  src/original_alert.cpp
  src/original_audio.cpp
  src/original_audio_capture.cpp
  src/original_audio_rules.cpp
  src/original_command_palette.cpp
  src/original_construction.cpp
  src/original_dialog.cpp
  src/original_dialog_controllers.cpp
  src/original_dialog_model.cpp
  src/original_dialog_paint.cpp
  src/original_dib.cpp
  src/original_dtmp.cpp
  src/original_dtmp_runtime.cpp
  src/original_elevator_control.cpp
  src/original_elevator_control_controller.cpp
  src/original_facility_information_controller.cpp
  src/original_finance.cpp
  src/original_find.cpp
  src/original_idle_pass.cpp
  src/original_info.cpp
  src/original_information.cpp
  src/original_information_paint.cpp
  src/original_main_command.cpp
  src/original_map.cpp
  src/original_menu.cpp
  src/original_palette_frame.cpp
  src/original_people.cpp
  src/original_person_information_controller.cpp
  src/original_resources_index.cpp
  src/original_simulation.cpp
  src/original_startup.cpp
  src/original_startup_data.cpp
  src/original_tables.cpp
  src/original_tdt.cpp
  src/original_tdt_file.cpp
  src/original_tower_file.cpp
  src/original_transport_information_controller.cpp
  src/original_ui.cpp
  src/original_wave.cpp
  src/original_world.cpp
  src/original_world_input.cpp
  src/original_palette_input.cpp
)
target_include_directories(simtower_port_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/generated
)
if(WIN32)
  target_compile_definitions(simtower_port_core PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_sources(simtower_port_core PRIVATE
    src/original_alert_win32.cpp
    src/original_audio_waveout_win32.cpp
    src/original_command_palette_win32.cpp
    src/original_dialog_win32.cpp
    src/original_dib_win32.cpp
    src/original_draw_context_win32.cpp
    src/original_dtmp_runtime_win32.cpp
    src/original_font_win32.cpp
    src/original_info_win32.cpp
    src/original_information_win32.cpp
    src/original_palette_frame_win32.cpp
    src/original_resources_win32.cpp
    src/original_ui_win32.cpp
    src/original_world_win32.cpp
  )
  target_link_libraries(simtower_port_core PUBLIC user32 gdi32 winmm)

  find_program(SIMTOWER_WINDRES NAMES windres HINTS C:/Strawberry/c/bin REQUIRED)
  set(SIMTOWER_RESOURCE_OBJECT ${CMAKE_CURRENT_BINARY_DIR}/original_resources.res.o)
  set(SIMTOWER_RESOURCE_PACK
      ${CMAKE_CURRENT_SOURCE_DIR}/generated/original_resources.pack)
  set(SIMTOWER_HELP_FILE
      ${CMAKE_CURRENT_SOURCE_DIR}/../original/extracted/MAXIS/SIMTOWER/SIMTOWER.HLP)
  file(TO_CMAKE_PATH "${SIMTOWER_RESOURCE_PACK}" SIMTOWER_RESOURCE_PACK_RC)
  file(TO_CMAKE_PATH "${SIMTOWER_HELP_FILE}" SIMTOWER_HELP_FILE_RC)
  configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/original_resources.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/original_resources.rc
    @ONLY
  )
  add_custom_command(
    OUTPUT ${SIMTOWER_RESOURCE_OBJECT}
    COMMAND ${SIMTOWER_WINDRES} -O coff
            ${CMAKE_CURRENT_BINARY_DIR}/original_resources.rc
            ${SIMTOWER_RESOURCE_OBJECT}
    DEPENDS
      ${CMAKE_CURRENT_SOURCE_DIR}/generated/original_resources.rc.in
      ${SIMTOWER_RESOURCE_PACK}
      ${SIMTOWER_HELP_FILE}
    VERBATIM
  )
  set_source_files_properties(${SIMTOWER_RESOURCE_OBJECT} PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)

  add_executable(simtower WIN32 src/native_main.cpp ${SIMTOWER_RESOURCE_OBJECT})
  target_link_libraries(simtower PRIVATE simtower_port_core comdlg32)
  target_compile_definitions(simtower PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  if(MINGW)
    target_link_options(simtower PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
endif()

# Software drawing canvas: the portable OriginalDrawContext over a raster,
# with TrueType text through the vendored stb_truetype and the bundled
# Liberation Sans Regular (port/third_party, licenses in
# ../THIRD_PARTY_LICENSES). Built on every platform for the portable tests;
# only the SDL frontend links it into a game executable. The Win32 host keeps
# GDI and the system Arial / MS Sans Serif.
set(SIMTOWER_CANVAS_FONT
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/liberation-fonts/LiberationSans-Regular.ttf)
set(SIMTOWER_CANVAS_FONT_SOURCE
    ${CMAKE_CURRENT_BINARY_DIR}/generated_canvas_font.cpp)
add_custom_command(
  OUTPUT ${SIMTOWER_CANVAS_FONT_SOURCE}
  COMMAND ${CMAKE_COMMAND} -DINPUT=${SIMTOWER_CANVAS_FONT}
          -DOUTPUT=${SIMTOWER_CANVAS_FONT_SOURCE}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_font.cmake
  DEPENDS ${SIMTOWER_CANVAS_FONT}
          ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_font.cmake
  VERBATIM
)
add_library(simtower_canvas STATIC
  src/original_canvas_font.cpp
  src/original_draw_canvas.cpp
  src/original_text_encoding.cpp
  ${SIMTOWER_CANVAS_FONT_SOURCE}
)
target_include_directories(simtower_canvas SYSTEM PRIVATE
                           ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)
target_link_libraries(simtower_canvas PUBLIC simtower_port_core)
if(NOT MSVC)
  # Keep stb_truetype's float rasterizer free of fused multiply-adds, so
  # glyph masks (and the canvas test hashes) match across compilers and
  # architectures.
  target_compile_options(simtower_canvas PRIVATE -ffp-contract=off)
endif()

# SDL3 frontend (port/sdl). On by default off Windows; the Windows build
# keeps the Win32 host and can opt in once an SDL3 package is available.
# A non-Windows configure therefore requires an SDL3 CMake package: CI jobs
# for Linux and macOS must install SDL3 or pass -DSIMTOWER_BUILD_SDL=OFF.
if(WIN32)
  set(SIMTOWER_BUILD_SDL_DEFAULT OFF)
else()
  set(SIMTOWER_BUILD_SDL_DEFAULT ON)
endif()
option(SIMTOWER_BUILD_SDL "Build the SDL3 frontend simtower_sdl"
       ${SIMTOWER_BUILD_SDL_DEFAULT})
# Test support (staged scenario towers and the --direct-build reference
# constructor). Portable: it depends only on the core, never on SDL or
# Win32. Used by simtower_sdl's test-only flags and the portable tests.
add_library(simtower_test_support STATIC support/test_scenario.cpp)
target_include_directories(simtower_test_support PUBLIC
                           ${CMAKE_CURRENT_SOURCE_DIR}/support)
target_link_libraries(simtower_test_support PUBLIC simtower_port_core)

# The SDL host's portable dialog manager and its dialog descriptors
# (sdl/sdl_dialog*.hpp). No SDL in it, so it is built on every platform:
# simtower_sdl and the dialog tests (native and under Wine) link it.
add_library(simtower_sdl_runtime STATIC sdl/sdl_dialog.cpp
                                        sdl/sdl_dialog_widgets.cpp
                                        sdl/sdl_dialog_descriptors.cpp)
target_include_directories(simtower_sdl_runtime PUBLIC
                           ${CMAKE_CURRENT_SOURCE_DIR}/sdl)
target_link_libraries(simtower_sdl_runtime PUBLIC simtower_canvas)

if(SIMTOWER_BUILD_SDL)
  find_package(SDL3 REQUIRED CONFIG)
  add_executable(simtower_sdl sdl/sdl_main.cpp sdl/sdl_session.cpp
                              sdl/sdl_audio.cpp sdl/sdl_layout.cpp
                              sdl/sdl_panels.cpp sdl/sdl_input_script.cpp
                              sdl/sdl_menu.cpp)
  target_link_libraries(simtower_sdl PRIVATE simtower_test_support
                                             simtower_sdl_runtime SDL3::SDL3)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(simtower_native_save_fixture tests/native_save_fixture.cpp)
  target_link_libraries(simtower_native_save_fixture PRIVATE simtower_port_core)

  # Win32-only suites: waveOut, the PE resource loader and the GDI paths.
  if(WIN32)
    # Manual hardware smoke: it uses the production waveOut transaction but
    # submits a same-format digital-silence buffer so validation is inaudible.
    # It is intentionally not registered in CTest because a PCM endpoint is a
    # machine capability, not a deterministic headless prerequisite.
    add_executable(simtower_native_pcm_smoke tests/native_pcm_smoke.cpp)
    target_link_libraries(simtower_native_pcm_smoke PRIVATE simtower_port_core)

    add_executable(simtower_resource_tests tests/resource_tests.cpp)
    target_link_libraries(simtower_resource_tests PRIVATE simtower_port_core)
    add_test(
      NAME simtower_resource_tests
      COMMAND simtower_resource_tests ${CMAKE_CURRENT_SOURCE_DIR}/generated/original_resources.pack
    )

    # The DLGTEMPLATE bytes the Win32 host builds from the shared dialog
    # models must equal the old inline preparation (frozen copy).
    add_executable(simtower_dialog_template_parity_tests
                   tests/dialog_template_parity_tests.cpp)
    target_link_libraries(simtower_dialog_template_parity_tests PRIVATE
                          simtower_port_core)
    target_compile_definitions(simtower_dialog_template_parity_tests PRIVATE
                               UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_test(
      NAME simtower_dialog_template_parity_tests
      COMMAND simtower_dialog_template_parity_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/generated/original_resources.pack
    )
    set_tests_properties(simtower_dialog_template_parity_tests PROPERTIES
                         SKIP_RETURN_CODE 77)

    # The shared DTMP chrome painter through GDI (and the canvas) must paint
    # the same pixels as the frozen direct-GDI painter.
    add_executable(simtower_dialog_chrome_parity_tests
                   tests/dialog_chrome_parity_tests.cpp)
    target_link_libraries(simtower_dialog_chrome_parity_tests PRIVATE
                          simtower_canvas)
    target_compile_definitions(simtower_dialog_chrome_parity_tests PRIVATE
                               UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_test(
      NAME simtower_dialog_chrome_parity_tests
      COMMAND simtower_dialog_chrome_parity_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/generated/original_resources.pack
    )
    set_tests_properties(simtower_dialog_chrome_parity_tests PROPERTIES
                         SKIP_RETURN_CODE 77)

    # The shared Elevator Control and ELVPOPUP painters through GDI (and
    # the canvas, outside its text) must paint the same pixels as the
    # frozen direct-GDI painters.
    add_executable(simtower_elevator_control_painter_parity_tests
                   tests/elevator_control_painter_parity_tests.cpp)
    target_link_libraries(simtower_elevator_control_painter_parity_tests
                          PRIVATE simtower_test_support simtower_canvas)
    target_compile_definitions(simtower_elevator_control_painter_parity_tests
                               PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN
                                       NOMINMAX)
    if(MINGW)
      # Complete tower documents live on the stack.
      target_link_options(simtower_elevator_control_painter_parity_tests
                          PRIVATE -Wl,--stack,16777216)
    endif()
    add_test(
      NAME simtower_elevator_control_painter_parity_tests
      COMMAND simtower_elevator_control_painter_parity_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/generated/original_resources.pack
    )
    set_tests_properties(simtower_elevator_control_painter_parity_tests
                         PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(
      simtower_embedded_resource_tests
      tests/embedded_resource_tests.cpp
      ${SIMTOWER_RESOURCE_OBJECT}
    )
    target_link_libraries(simtower_embedded_resource_tests PRIVATE simtower_port_core)
    add_test(NAME simtower_embedded_resource_tests COMMAND simtower_embedded_resource_tests)
  endif()

  add_executable(simtower_tdt_tests tests/tdt_tests.cpp)
  target_link_libraries(simtower_tdt_tests PRIVATE simtower_port_core)
  if(MINGW)
    # normalize_for_original_game_save intentionally copies a complete tower
    # value; the unoptimized test build needs more than MinGW's default stack.
    target_link_options(simtower_tdt_tests PRIVATE -Wl,--stack,16777216)
  endif()
  add_test(
    NAME simtower_tdt_tests
    COMMAND simtower_tdt_tests
      ${CMAKE_CURRENT_SOURCE_DIR}/../references/tdt-samples/s2b-exported.TDT
      ${CMAKE_CURRENT_SOURCE_DIR}/../references/tdt-samples/ElDlux/ElDlux.TDT
      ${CMAKE_CURRENT_SOURCE_DIR}/../references/tdt-samples/RoyalAbode/RoyalA.TDT
      ${CMAKE_CURRENT_SOURCE_DIR}/../references/tdt-samples/SimEmpire/SimEmpire.TDT
      ${CMAKE_CURRENT_BINARY_DIR}/tdt-roundtrip.TDT
  )

  add_executable(simtower_simulation_tests tests/simulation_tests.cpp)
  target_link_libraries(simtower_simulation_tests PRIVATE simtower_port_core)
  if(MINGW)
    # The debug compiler reserves multiple full OriginalTdtDocument return
    # slots in a single immediately-invoked test lambda. Keep that headless
    # harness from exhausting MinGW's default 2-MiB stack before its first
    # assertion; this option applies only to the test executable.
    target_link_options(simtower_simulation_tests PRIVATE -Wl,--stack,16777216)
  endif()
  add_test(NAME simtower_simulation_tests COMMAND simtower_simulation_tests)

  add_executable(simtower_people_tests tests/people_tests.cpp)
  target_link_libraries(simtower_people_tests PRIVATE simtower_port_core)
  if(MINGW)
    # This deliberately broad, disassembly-table-driven harness keeps many
    # full OriginalTdtDocument fixtures in one main function. Its test-only
    # stack must accommodate the compiler's aggregate return slots.
    target_link_options(simtower_people_tests PRIVATE -Wl,--stack,16777216)
  endif()
  add_test(NAME simtower_people_tests COMMAND simtower_people_tests)

  add_executable(simtower_construction_tests tests/construction_tests.cpp)
  target_link_libraries(simtower_construction_tests PRIVATE simtower_port_core)
  add_test(NAME simtower_construction_tests COMMAND simtower_construction_tests)

  add_executable(simtower_world_tests tests/world_tests.cpp)
  target_link_libraries(simtower_world_tests PRIVATE simtower_port_core)
  add_test(
    NAME simtower_world_tests
    COMMAND simtower_world_tests ${CMAKE_CURRENT_SOURCE_DIR}/generated/original_resources.pack
  )

  add_executable(simtower_command_palette_tests tests/command_palette_tests.cpp)
  target_link_libraries(simtower_command_palette_tests PRIVATE simtower_port_core)
  add_test(
    NAME simtower_command_palette_tests
    COMMAND simtower_command_palette_tests
      ${CMAKE_CURRENT_SOURCE_DIR}/generated/original_resources.pack
  )

  add_executable(simtower_map_tests tests/map_tests.cpp)
  target_link_libraries(simtower_map_tests PRIVATE simtower_port_core)
  add_test(
    NAME simtower_map_tests
    COMMAND simtower_map_tests
      ${CMAKE_CURRENT_SOURCE_DIR}/generated/original_resources.pack
  )

  add_executable(simtower_info_tests tests/info_tests.cpp)
  target_link_libraries(simtower_info_tests PRIVATE simtower_canvas)
  add_test(
    NAME simtower_info_tests
    COMMAND simtower_info_tests
      ${CMAKE_CURRENT_SOURCE_DIR}/generated/original_resources.pack
  )

  # The shared DTMP chrome painter through the canvas on every platform
  # (synthetic probes; the pack half is skipped, exit 77, without assets).
  add_executable(simtower_dialog_paint_tests tests/dialog_paint_tests.cpp)
  target_link_libraries(simtower_dialog_paint_tests PRIVATE simtower_canvas)
  add_test(
    NAME simtower_dialog_paint_tests
    COMMAND simtower_dialog_paint_tests
      ${CMAKE_CURRENT_SOURCE_DIR}/generated/original_resources.pack
  )
  set_tests_properties(simtower_dialog_paint_tests PROPERTIES
                       SKIP_RETURN_CODE 77)

  # OriginalCanvasDrawContext primitives on synthetic input (no assets).
  add_executable(simtower_draw_canvas_tests tests/draw_canvas_tests.cpp)
  target_link_libraries(simtower_draw_canvas_tests PRIVATE simtower_canvas)
  add_test(NAME simtower_draw_canvas_tests COMMAND simtower_draw_canvas_tests)

  add_executable(simtower_information_tests tests/information_tests.cpp)
  target_link_libraries(simtower_information_tests PRIVATE simtower_port_core)
  if(MINGW)
    # The large single-function information fixture reserves several complete
    # tower return slots in Debug. This affects only the headless harness.
    target_link_options(simtower_information_tests PRIVATE -Wl,--stack,16777216)
  endif()
  add_test(
    NAME simtower_information_tests
    COMMAND simtower_information_tests
      ${CMAKE_CURRENT_SOURCE_DIR}/generated/original_resources.pack
  )

  add_executable(simtower_find_tests tests/find_tests.cpp)
  target_link_libraries(simtower_find_tests PRIVATE simtower_port_core)
  add_test(
    NAME simtower_find_tests
    COMMAND simtower_find_tests
      ${CMAKE_CURRENT_SOURCE_DIR}/../references/tdt-samples/ElDlux/ElDlux.TDT
      ${CMAKE_CURRENT_SOURCE_DIR}/../references/tdt-samples/SimEmpire/SimEmpire.TDT
      ${CMAKE_CURRENT_SOURCE_DIR}/generated/original_resources.pack
  )

  add_executable(simtower_about_tests tests/about_tests.cpp)
  target_link_libraries(simtower_about_tests PRIVATE simtower_port_core)
  add_test(
    NAME simtower_about_tests
    COMMAND simtower_about_tests
      ${CMAKE_CURRENT_SOURCE_DIR}/generated/original_resources.pack
  )

  # The shared dialog models (original_dialog_model.hpp): event templates
  # and texts, About, and the message-box requests. Synthetic checks need no
  # assets; the resource half is skipped (exit 77) without the pack.
  add_executable(simtower_dialog_model_tests tests/dialog_model_tests.cpp)
  target_link_libraries(simtower_dialog_model_tests PRIVATE simtower_port_core)
  add_test(
    NAME simtower_dialog_model_tests
    COMMAND simtower_dialog_model_tests
      ${CMAKE_CURRENT_SOURCE_DIR}/generated/original_resources.pack
  )
  set_tests_properties(simtower_dialog_model_tests PROPERTIES
                       SKIP_RETURN_CODE 77)

  # The SDL host's dialog runtime (sdl/sdl_dialog.hpp has no SDL in it, so
  # this also builds and runs under Wine): layout, focus, keys, pointer
  # capture, timers and every event dialog's answer, the alerts and About.
  # The resource half is skipped (exit 77) without the pack.
  add_executable(simtower_sdl_dialog_tests tests/sdl_dialog_tests.cpp)
  target_link_libraries(simtower_sdl_dialog_tests PRIVATE
                        simtower_sdl_runtime)
  add_test(
    NAME simtower_sdl_dialog_tests
    COMMAND simtower_sdl_dialog_tests
      ${CMAKE_CURRENT_SOURCE_DIR}/generated/original_resources.pack
  )
  set_tests_properties(simtower_sdl_dialog_tests PROPERTIES
                       SKIP_RETURN_CODE 77)

  # The shared Find / Rename / New Movie / Finance controllers against
  # frozen copies of the Win32 transactions (also run under Wine). The
  # staged tower needs the pack: skipped (exit 77) without it.
  add_executable(simtower_dialog_controller_tests
                 tests/dialog_controller_tests.cpp)
  target_link_libraries(simtower_dialog_controller_tests PRIVATE
                        simtower_test_support)
  add_test(
    NAME simtower_dialog_controller_tests
    COMMAND simtower_dialog_controller_tests
      ${CMAKE_CURRENT_SOURCE_DIR}/generated/original_resources.pack
  )
  set_tests_properties(simtower_dialog_controller_tests PROPERTIES
                       SKIP_RETURN_CODE 77)

  # The shared Facility Information controller against a frozen copy of
  # the Win32 TENANTINFODLOGFILTER branches, and its portable painter (also
  # run under Wine). The staged tower needs the pack: skipped (exit 77).
  add_executable(simtower_facility_information_controller_tests
                 tests/facility_information_controller_tests.cpp)
  target_link_libraries(simtower_facility_information_controller_tests
                        PRIVATE simtower_test_support simtower_canvas)
  if(MINGW)
    target_link_options(simtower_facility_information_controller_tests
                        PRIVATE -Wl,--stack,16777216)
  endif()
  add_test(
    NAME simtower_facility_information_controller_tests
    COMMAND simtower_facility_information_controller_tests
      ${CMAKE_CURRENT_SOURCE_DIR}/generated/original_resources.pack
  )
  set_tests_properties(simtower_facility_information_controller_tests
                       PROPERTIES SKIP_RETURN_CODE 77)

  # The shared Person Information controller against a frozen copy of the
  # Win32 PEPLEINFODLOGFILTER branches, the Find exit latch and its portable
  # painter (also run under Wine). The staged tower needs the pack:
  # skipped (exit 77).
  add_executable(simtower_person_information_controller_tests
                 tests/person_information_controller_tests.cpp)
  target_link_libraries(simtower_person_information_controller_tests
                        PRIVATE simtower_test_support simtower_canvas)
  if(MINGW)
    target_link_options(simtower_person_information_controller_tests
                        PRIVATE -Wl,--stack,16777216)
  endif()
  add_test(
    NAME simtower_person_information_controller_tests
    COMMAND simtower_person_information_controller_tests
      ${CMAKE_CURRENT_SOURCE_DIR}/generated/original_resources.pack
  )
  set_tests_properties(simtower_person_information_controller_tests
                       PROPERTIES SKIP_RETURN_CODE 77)

  # The shared Transport Information controller (elevator car, stairs /
  # escalator) against a frozen copy of the Win32 ELVINFODLOGFILTER /
  # ESCINFODLOGFILTER branches, and its portable painter (also run under
  # Wine). The staged tower needs the pack: skipped (exit 77).
  add_executable(simtower_transport_information_controller_tests
                 tests/transport_information_controller_tests.cpp)
  target_link_libraries(simtower_transport_information_controller_tests
                        PRIVATE simtower_test_support simtower_canvas)
  if(MINGW)
    target_link_options(simtower_transport_information_controller_tests
                        PRIVATE -Wl,--stack,16777216)
  endif()
  add_test(
    NAME simtower_transport_information_controller_tests
    COMMAND simtower_transport_information_controller_tests
      ${CMAKE_CURRENT_SOURCE_DIR}/generated/original_resources.pack
  )
  set_tests_properties(simtower_transport_information_controller_tests
                       PROPERTIES SKIP_RETURN_CODE 77)

  # The SDL dialog manager's controls (edit, list box, combo box, scroll
  # bar), the arrow-key groups and the WM_PAINT event: synthetic, no assets.
  add_executable(simtower_sdl_widget_tests tests/sdl_widget_tests.cpp)
  target_link_libraries(simtower_sdl_widget_tests PRIVATE
                        simtower_sdl_runtime)
  add_test(NAME simtower_sdl_widget_tests COMMAND simtower_sdl_widget_tests)

  # The shared ELVDLOGMAIN / ELVPOPUP controllers against frozen copies of
  # the Win32 procedure branches, and the simulation oracle through the
  # shared idle pass (also run under Wine). Needs the pack (exit 77).
  add_executable(simtower_elevator_control_controller_tests
                 tests/elevator_control_controller_tests.cpp)
  target_link_libraries(simtower_elevator_control_controller_tests PRIVATE
                        simtower_test_support simtower_canvas)
  if(MINGW)
    # Several complete tower documents live on the stack.
    target_link_options(simtower_elevator_control_controller_tests PRIVATE
                        -Wl,--stack,16777216)
  endif()
  add_test(
    NAME simtower_elevator_control_controller_tests
    COMMAND simtower_elevator_control_controller_tests
      ${CMAKE_CURRENT_SOURCE_DIR}/generated/original_resources.pack
  )
  set_tests_properties(simtower_elevator_control_controller_tests PROPERTIES
                       SKIP_RETURN_CODE 77)

  add_executable(simtower_elevator_control_tests tests/elevator_control_tests.cpp)
  target_link_libraries(simtower_elevator_control_tests PRIVATE simtower_port_core)
  add_test(
    NAME simtower_elevator_control_tests
    COMMAND simtower_elevator_control_tests
      ${CMAKE_CURRENT_SOURCE_DIR}/generated/original_resources.pack
  )

  # The TOWER_MENU model and the menu op lists (original_menu.hpp). The
  # resource half reads the pack at run time and is skipped (exit 77)
  # without it; the synthetic half needs no assets.
  add_executable(simtower_menu_tests tests/menu_tests.cpp)
  target_link_libraries(simtower_menu_tests PRIVATE simtower_port_core)
  add_test(
    NAME simtower_menu_tests
    COMMAND simtower_menu_tests
      ${CMAKE_CURRENT_SOURCE_DIR}/generated/original_resources.pack
  )
  set_tests_properties(simtower_menu_tests PROPERTIES SKIP_RETURN_CODE 77)

  # The shared WM_COMMAND dispatcher against a recording host (no assets).
  add_executable(simtower_main_command_tests tests/main_command_tests.cpp)
  target_link_libraries(simtower_main_command_tests PRIVATE
                        simtower_port_core)
  add_test(NAME simtower_main_command_tests
           COMMAND simtower_main_command_tests)

  # The shared File transactions against a recording host and temporary
  # files (no assets).
  add_executable(simtower_tower_file_tests tests/tower_file_tests.cpp)
  target_link_libraries(simtower_tower_file_tests PRIVATE simtower_port_core)
  if(MINGW)
    # Several complete tower documents live on the stack.
    target_link_options(simtower_tower_file_tests PRIVATE
                        -Wl,--stack,16777216)
  endif()
  add_test(NAME simtower_tower_file_tests COMMAND simtower_tower_file_tests)

  # Portable audio runtime rules and the device transaction, on a fake
  # device and the capture device. No audio device is opened. The parts that
  # need WAVE resources read the pack at run time and are skipped (exit 77)
  # when it is missing.
  add_executable(simtower_audio_tests tests/audio_tests.cpp)
  target_link_libraries(simtower_audio_tests PRIVATE simtower_port_core)
  add_test(
    NAME simtower_audio_tests
    COMMAND simtower_audio_tests
      ${CMAKE_CURRENT_SOURCE_DIR}/generated/original_resources.pack
  )
  set_tests_properties(simtower_audio_tests PROPERTIES SKIP_RETURN_CODE 77)

  # Locks the shared idle pass's host-hook order on a staged tower. The
  # scenario builder is portable (no SDL), so this runs on every platform.
  add_executable(simtower_idle_pass_tests tests/idle_pass_tests.cpp)
  target_link_libraries(simtower_idle_pass_tests PRIVATE
                        simtower_test_support)
  add_test(
    NAME simtower_idle_pass_tests
    COMMAND simtower_idle_pass_tests
      ${CMAKE_CURRENT_SOURCE_DIR}/generated/original_resources.pack
  )

  # The shared world input module (Main construction, edit tools, Command
  # and Map pointer input) against direct constructor calls, through a
  # recording host. Portable (no SDL or Win32), so it also runs under Wine.
  add_executable(simtower_world_input_tests tests/world_input_tests.cpp)
  target_link_libraries(simtower_world_input_tests PRIVATE
                        simtower_test_support)
  if(MINGW)
    # Each case keeps several complete tower copies on the stack.
    target_link_options(simtower_world_input_tests PRIVATE
                        -Wl,--stack,16777216)
  endif()
  add_test(
    NAME simtower_world_input_tests
    COMMAND simtower_world_input_tests
      ${CMAKE_CURRENT_SOURCE_DIR}/generated/original_resources.pack
  )

  # SdlAudioDevice on SDL's dummy audio driver with synthetic buffers (no
  # assets): U8/S16 playback, loop passes, partial frames, stop and teardown.
  # The test also forces the dummy driver itself, so it is never audible.
  if(SIMTOWER_BUILD_SDL)
    add_executable(simtower_sdl_audio_tests tests/sdl_audio_tests.cpp
                                            sdl/sdl_audio.cpp)
    target_include_directories(simtower_sdl_audio_tests PRIVATE
                               ${CMAKE_CURRENT_SOURCE_DIR}/sdl)
    target_link_libraries(simtower_sdl_audio_tests PRIVATE
                          simtower_port_core SDL3::SDL3)
    add_test(NAME simtower_sdl_audio_tests COMMAND simtower_sdl_audio_tests)
    set_tests_properties(simtower_sdl_audio_tests PROPERTIES
                         ENVIRONMENT "SDL_AUDIO_DRIVER=dummy;SDL_VIDEODRIVER=dummy")

    # Scripted input end to end: window coordinates through the panel
    # router into the shared module must match --direct-build's tower (or,
    # for "same-digest-as" scripts, a reference script's whole digest).
    foreach(script place_lobby selector_floor place_office
                   palette_nonclient selector_inner_click)
      add_test(
        NAME simtower_sdl_input_${script}
        COMMAND ${CMAKE_COMMAND}
          -DBIN=$<TARGET_FILE:simtower_sdl>
          -DPACK=${CMAKE_CURRENT_SOURCE_DIR}/generated/original_resources.pack
          -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts/${script}.txt
          -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_input_test.cmake)
      set_tests_properties(simtower_sdl_input_${script} PROPERTIES
                           ENVIRONMENT
                           "SDL_AUDIO_DRIVER=dummy;SDL_VIDEODRIVER=dummy")
    endforeach()

    # The dialog runtime end to end through scripts: expect-dialog,
    # dialog-click/key/wait/hold, the logged answers, the held frames and
    # the script errors (cli_dialog_test.cmake reads each header).
    foreach(script dialog_bomb_pay dialog_bomb_esc dialog_bomb_enter
                   dialog_timer_accept dialog_bomb_short
                   dialog_bomb_short_timer dialog_default dialog_mismatch
                   dialog_unbound dialog_click_static dialog_none_open
                   alert_cancel_1001 alert_no_1001 alert_answer_3
                   alert_answer_3_invalid
                   about_dialog dialog_frames dialog_names_list
                   dialog_tenant_focus dialog_finance dialog_movie
                   dialog_type_not_edit elevator_control_edit
                   elevator_control_menu_blocked elevator_control_noop
                   elevator_control_scroll elevator_control_simulate
                   elevator_control_popup elevator_control_frames
                   elevator_control_scroll_frame elevator_control_repeat
                   elevator_control_double elevator_control_double_scroll
                   elevator_control_tracking elevator_control_magnifier
                   elevator_control_magnifier_car
                   elevator_control_accelerator
                   elevator_control_phase_released
                   facility_magnifier facility_rent facility_rename
                   facility_movie facility_portrait facility_frames
                   facility_frames_full facility_lobby_finance
                   person_magnifier person_rename
                   person_exit_latch person_frames
                   transport_magnifier transport_portrait
                   transport_exit_latch transport_frames)
      add_test(
        NAME simtower_sdl_${script}
        COMMAND ${CMAKE_COMMAND}
          -DBIN=$<TARGET_FILE:simtower_sdl>
          -DPACK=${CMAKE_CURRENT_SOURCE_DIR}/generated/original_resources.pack
          -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts/${script}.txt
          -DWORK=${CMAKE_CURRENT_BINARY_DIR}/dialog-scripts
          -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_dialog_test.cmake)
      set_tests_properties(simtower_sdl_${script} PROPERTIES
                           ENVIRONMENT
                           "SDL_AUDIO_DRIVER=dummy;SDL_VIDEODRIVER=dummy"
                           SKIP_REGULAR_EXPRESSION "cli_dialog_test: skipped")
    endforeach()

    # The window's menu bar: layout, modal tracking and drawing on the
    # pack's TOWER_MENU (pure logic and pixels, no SDL calls).
    add_executable(simtower_sdl_menu_tests tests/sdl_menu_tests.cpp
                                           sdl/sdl_menu.cpp sdl/sdl_layout.cpp)
    target_include_directories(simtower_sdl_menu_tests PRIVATE
                               ${CMAKE_CURRENT_SOURCE_DIR}/sdl)
    target_link_libraries(simtower_sdl_menu_tests PRIVATE
                          simtower_test_support simtower_canvas)
    add_test(
      NAME simtower_sdl_menu_tests
      COMMAND simtower_sdl_menu_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/generated/original_resources.pack)
    set_tests_properties(simtower_sdl_menu_tests PROPERTIES
                         SKIP_RETURN_CODE 77)

    # Save As and Open through the scripted File menu and picker: a fresh
    # menu Open of the saved file must continue exactly like --open of it.
    add_test(
      NAME simtower_sdl_save_load
      COMMAND ${CMAKE_COMMAND}
        -DBIN=$<TARGET_FILE:simtower_sdl>
        -DPACK=${CMAKE_CURRENT_SOURCE_DIR}/generated/original_resources.pack
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/save-load
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_save_load_test.cmake)
    set_tests_properties(simtower_sdl_save_load PROPERTIES
                         ENVIRONMENT
                         "SDL_AUDIO_DRIVER=dummy;SDL_VIDEODRIVER=dummy")

    # The documented example scripts must keep running (exit 0) with the
    # --scenario and --passes their usage line names.
    file(GLOB SIMTOWER_SDL_EXAMPLE_SCRIPTS CONFIGURE_DEPENDS
         ${CMAKE_CURRENT_SOURCE_DIR}/sdl/scripts/*.txt)
    foreach(script_path IN LISTS SIMTOWER_SDL_EXAMPLE_SCRIPTS)
      get_filename_component(script ${script_path} NAME_WE)
      add_test(
        NAME simtower_sdl_example_${script}
        COMMAND ${CMAKE_COMMAND}
          -DBIN=$<TARGET_FILE:simtower_sdl>
          -DPACK=${CMAKE_CURRENT_SOURCE_DIR}/generated/original_resources.pack
          -DSCRIPT=${script_path}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_example_script_test.cmake)
      set_tests_properties(simtower_sdl_example_${script} PROPERTIES
                           ENVIRONMENT
                           "SDL_AUDIO_DRIVER=dummy;SDL_VIDEODRIVER=dummy")
    endforeach()
  endif()

  # These executables intentionally use assert() as their test primitive.
  # Release configurations define NDEBUG globally; removing it per test keeps
  # both the checks and any setup calls inside them active.
  set(SIMTOWER_ASSERT_TESTS
      simtower_tdt_tests
      simtower_simulation_tests
      simtower_people_tests
      simtower_construction_tests
      simtower_world_tests
      simtower_command_palette_tests
      simtower_map_tests
      simtower_info_tests
      simtower_draw_canvas_tests
      simtower_information_tests
      simtower_find_tests
      simtower_about_tests
      simtower_dialog_model_tests
      simtower_dialog_paint_tests
      simtower_sdl_dialog_tests
      simtower_sdl_widget_tests
      simtower_dialog_controller_tests
      simtower_facility_information_controller_tests
      simtower_person_information_controller_tests
      simtower_transport_information_controller_tests
      simtower_elevator_control_tests
      simtower_elevator_control_controller_tests
      simtower_audio_tests
      simtower_idle_pass_tests
      simtower_world_input_tests
      simtower_menu_tests
      simtower_main_command_tests
      simtower_tower_file_tests)
  if(SIMTOWER_BUILD_SDL)
    list(APPEND SIMTOWER_ASSERT_TESTS simtower_sdl_audio_tests
                                      simtower_sdl_menu_tests)
  endif()
  if(WIN32)
    list(APPEND SIMTOWER_ASSERT_TESTS
         simtower_resource_tests
         simtower_embedded_resource_tests
         simtower_dialog_template_parity_tests
         simtower_dialog_chrome_parity_tests
         simtower_elevator_control_painter_parity_tests)
  endif()
  foreach(test_target IN LISTS SIMTOWER_ASSERT_TESTS)
    if(MSVC)
      target_compile_options(${test_target} PRIVATE /UNDEBUG)
    else()
      target_compile_options(${test_target} PRIVATE -UNDEBUG)
    endif()
  endforeach()
endif()

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
  src/original_dib.cpp
  src/original_dtmp.cpp
  src/original_dtmp_runtime.cpp
  src/original_elevator_control.cpp
  src/original_finance.cpp
  src/original_find.cpp
  src/original_idle_pass.cpp
  src/original_info.cpp
  src/original_information.cpp
  src/original_map.cpp
  src/original_people.cpp
  src/original_resources_index.cpp
  src/original_simulation.cpp
  src/original_startup.cpp
  src/original_startup_data.cpp
  src/original_tables.cpp
  src/original_tdt.cpp
  src/original_tdt_file.cpp
  src/original_ui.cpp
  src/original_wave.cpp
  src/original_world.cpp
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
if(SIMTOWER_BUILD_SDL)
  find_package(SDL3 REQUIRED CONFIG)
  add_executable(simtower_sdl sdl/sdl_main.cpp sdl/sdl_session.cpp
                              sdl/sdl_scenario.cpp sdl/sdl_audio.cpp)
  target_link_libraries(simtower_sdl PRIVATE simtower_port_core SDL3::SDL3)
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
  target_link_libraries(simtower_info_tests PRIVATE simtower_port_core)
  add_test(
    NAME simtower_info_tests
    COMMAND simtower_info_tests
      ${CMAKE_CURRENT_SOURCE_DIR}/generated/original_resources.pack
  )

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

  add_executable(simtower_elevator_control_tests tests/elevator_control_tests.cpp)
  target_link_libraries(simtower_elevator_control_tests PRIVATE simtower_port_core)
  add_test(
    NAME simtower_elevator_control_tests
    COMMAND simtower_elevator_control_tests
      ${CMAKE_CURRENT_SOURCE_DIR}/generated/original_resources.pack
  )

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
  add_executable(simtower_idle_pass_tests tests/idle_pass_tests.cpp
                                          sdl/sdl_scenario.cpp)
  target_include_directories(simtower_idle_pass_tests PRIVATE
                             ${CMAKE_CURRENT_SOURCE_DIR}/sdl)
  target_link_libraries(simtower_idle_pass_tests PRIVATE simtower_port_core)
  add_test(
    NAME simtower_idle_pass_tests
    COMMAND simtower_idle_pass_tests
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
      simtower_information_tests
      simtower_find_tests
      simtower_about_tests
      simtower_elevator_control_tests
      simtower_audio_tests
      simtower_idle_pass_tests)
  if(SIMTOWER_BUILD_SDL)
    list(APPEND SIMTOWER_ASSERT_TESTS simtower_sdl_audio_tests)
  endif()
  if(WIN32)
    list(APPEND SIMTOWER_ASSERT_TESTS
         simtower_resource_tests
         simtower_embedded_resource_tests)
  endif()
  foreach(test_target IN LISTS SIMTOWER_ASSERT_TESTS)
    if(MSVC)
      target_compile_options(${test_target} PRIVATE /UNDEBUG)
    else()
      target_compile_options(${test_target} PRIVATE -UNDEBUG)
    endif()
  endforeach()
endif()

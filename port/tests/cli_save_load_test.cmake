# Save/load round trip through the scripted File menu (simtower_sdl).
#
#   cmake -DBIN=simtower_sdl -DPACK=original_resources.pack -DWORK=dir \
#         [-DN=300] [-DM=300] -P cli_save_load_test.cmake
#
# 1. save: --scenario busy, N passes, then "menu 40002" (Save As) answered
#    by "pick-file WORK/RT.TDT". The script checks the caption and that the
#    file holds exactly the game-save bytes of the live tower.
# 2. menu-open: a fresh run (New) opens the file through "menu 40004" +
#    "pick-file", then runs M passes.
# 3. reference: a fresh run that starts from the same saved state through
#    the established command-line loader (--open WORK/RT.TDT) and runs the
#    same M passes uninterrupted.
#
# The tower and process digests of 2 and 3 must be equal. Comparing with the
# save run continuing to N+M instead would be wrong by design: the TDT does
# not hold the process-local state (document RNG, the service variants, the
# Hotel checkout counters, the medical route index), and Open resets the
# host state and runs the 10d0:0ac2 rebuild (sky and visible people steps),
# exactly as the original does. So the comparison is made where the original
# preserves everything: two fresh processes that load the same file.
#
# 4. exit: "menu 40001" (File > Exit) after 5 of 20 passes on a fresh tower
#    (no confirmation) ends the run there, like WM_QUIT in the window: the
#    rest of the script and passes are skipped, so the tower and process
#    digests equal a plain 5-pass run.

foreach(variable BIN PACK WORK)
  if(NOT DEFINED ${variable})
    message(FATAL_ERROR "cli_save_load_test: ${variable} is not set")
  endif()
endforeach()
if(NOT DEFINED N)
  set(N 300)
endif()
if(NOT DEFINED M)
  set(M 300)
endif()
file(MAKE_DIRECTORY "${WORK}")
set(tower_file "${WORK}/RT.TDT")
file(REMOVE "${tower_file}")

file(WRITE "${WORK}/save.txt"
  "pass ${N}\n"
  "expect-title SimTower - untitled\n"
  "menu 40002\n"
  "pick-file ${tower_file}\n"
  "expect-title SimTower - RT\n"
  "expect-saved ${tower_file}\n"
  "expect-menu 40008 grayed\n")
file(WRITE "${WORK}/exit.txt"
  "pass 5\n"
  "menu 40001\n"
  "pass 10\n"
  "menu 40007\n")
file(WRITE "${WORK}/open.txt"
  "menu 40004\n"
  "pick-file ${tower_file}\n"
  "expect-title SimTower - RT\n"
  "pass ${M}\n")

# Sets <prefix>_tower and <prefix>_process from the last digest line.
function(run_digest prefix passes)
  execute_process(
    COMMAND "${BIN}" --pack "${PACK}" --passes ${passes} --state-digest
            ${ARGN}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR
            "cli_save_load_test: ${ARGN} exited ${result}\n"
            "${output}\n${errors}")
  endif()
  string(REGEX MATCHALL "tower [0-9a-f]+" towers "${output}")
  string(REGEX MATCHALL "process [0-9a-f]+" processes "${output}")
  if(NOT towers OR NOT processes)
    message(FATAL_ERROR "cli_save_load_test: no digest from ${ARGN}\n${output}")
  endif()
  list(GET towers -1 tower)
  list(GET processes -1 process)
  set(${prefix}_tower "${tower}" PARENT_SCOPE)
  set(${prefix}_process "${process}" PARENT_SCOPE)
  set(${prefix}_errors "${errors}" PARENT_SCOPE)
endfunction()

run_digest(save ${N} --scenario busy --input-script "${WORK}/save.txt")
if(NOT EXISTS "${tower_file}")
  message(FATAL_ERROR "cli_save_load_test: Save As wrote no ${tower_file}")
endif()
run_digest(opened ${M} --input-script "${WORK}/open.txt")
run_digest(reference ${M} --open "${tower_file}")

if(NOT opened_tower STREQUAL reference_tower OR
   NOT opened_process STREQUAL reference_process)
  message(FATAL_ERROR
          "cli_save_load_test: menu Open + ${M} passes (${opened_tower}, "
          "${opened_process}) differs from --open + ${M} passes "
          "(${reference_tower}, ${reference_process})")
endif()
if(opened_tower STREQUAL save_tower)
  message(FATAL_ERROR "cli_save_load_test: the loaded run did not advance")
endif()
message(STATUS
        "cli_save_load_test: saved at ${N} (${save_tower}); menu Open + ${M} "
        "== --open + ${M}: ${opened_tower} ${opened_process}")

run_digest(exited 20 --input-script "${WORK}/exit.txt")
run_digest(five 5)
if(NOT exited_errors MATCHES "Exit after 5 of 20 passes" OR
   NOT exited_tower STREQUAL five_tower OR
   NOT exited_process STREQUAL five_process)
  message(FATAL_ERROR
          "cli_save_load_test: Exit after pass 5 (${exited_tower}, "
          "${exited_process}) differs from a 5-pass run (${five_tower}, "
          "${five_process})\n${exited_errors}")
endif()

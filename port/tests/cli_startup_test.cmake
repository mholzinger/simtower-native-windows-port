# WinMain's startup through simtower_sdl --startup (Session::run_startup).
#
#   cmake -DBIN=simtower_sdl -DPACK=original_resources.pack -DWORK=dir \
#         [-DM=200] -P cli_startup_test.cmake
#
# A tower saved by the scripted Save As (WORK/ST.TDT) is the Load target.
# Every startup run is compared with the run that starts the same tower
# without --startup: the tower and process digests after M passes must be
# equal (the splash wait moves only the virtual clock, and the host and
# request logs differ by design).
# - New Tower by click, by Enter, and undriven (no script): a plain run.
# - Load Saved Tower + "pick-file WORK/ST.TDT": --open WORK/ST.TDT.
# - Load Saved Tower + "pick-file cancel": New (1128:01c6), a plain run.
# - --startup --open WORK/ST.TDT: the command-line tower bypasses the
#   dialog (1128:00e5), --open WORK/ST.TDT.
# - Quit by click and by Esc (IDCANCEL = 2): exit 0, no pass, no digest.
# The New run holds the startup dialog over the splash (WORK/startup.bmp)
# and writes the first frame of the new tower (WORK/first-window.bmp,
# WORK/first-main.bmp).

foreach(variable BIN PACK WORK)
  if(NOT DEFINED ${variable})
    message(FATAL_ERROR "cli_startup_test: ${variable} is not set")
  endif()
endforeach()
if(NOT EXISTS "${PACK}")
  message(STATUS "cli_startup_test: skipped, no resource pack at ${PACK}")
  return()
endif()
if(NOT DEFINED M)
  set(M 200)
endif()
file(MAKE_DIRECTORY "${WORK}")
set(tower_file "${WORK}/ST.TDT")
foreach(output ST.TDT startup.bmp first-window.bmp first-main.bmp)
  file(REMOVE "${WORK}/${output}")
endforeach()

file(WRITE "${WORK}/save.txt"
  "pass 50\n"
  "menu 40002\n"
  "pick-file ${tower_file}\n"
  "expect-saved ${tower_file}\n")
file(WRITE "${WORK}/new.txt"
  "expect-dialog startup\n"
  "dialog-hold ${WORK}/startup.bmp\n"
  "dialog-click 1\n"
  "expect-title SimTower - untitled\n"
  "pass ${M}\n")
file(WRITE "${WORK}/enter.txt"
  "expect-dialog startup\n"
  "dialog-key enter\n"
  "pass ${M}\n")
file(WRITE "${WORK}/load.txt"
  "expect-dialog startup\n"
  "dialog-click 3\n"
  "pick-file ${tower_file}\n"
  "expect-title SimTower - ST\n"
  "pass ${M}\n")
file(WRITE "${WORK}/cancel.txt"
  "expect-dialog startup\n"
  "dialog-click 3\n"
  "pick-file cancel\n"
  "expect-title SimTower - untitled\n"
  "pass ${M}\n")
file(WRITE "${WORK}/quit.txt"
  "expect-dialog startup\n"
  "dialog-click 2\n"
  "pass ${M}\n")
file(WRITE "${WORK}/esc.txt"
  "expect-dialog startup\n"
  "dialog-key esc\n"
  "pass ${M}\n")
file(WRITE "${WORK}/first.txt"
  "expect-dialog startup\n"
  "dialog-click 1\n")

# Sets <prefix>_tower, <prefix>_process (empty without a digest line) and
# <prefix>_errors; the run must exit 0.
function(run_digest prefix)
  execute_process(
    COMMAND "${BIN}" --pack "${PACK}" --state-digest ${ARGN}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR
            "cli_startup_test: ${ARGN} exited ${result}\n${output}\n"
            "${errors}")
  endif()
  string(REGEX MATCHALL "tower [0-9a-f]+" towers "${output}")
  string(REGEX MATCHALL "process [0-9a-f]+" processes "${output}")
  set(tower "")
  set(process "")
  if(towers)
    list(GET towers -1 tower)
    list(GET processes -1 process)
  endif()
  set(${prefix}_tower "${tower}" PARENT_SCOPE)
  set(${prefix}_process "${process}" PARENT_SCOPE)
  set(${prefix}_errors "${errors}" PARENT_SCOPE)
endfunction()

function(expect_same name prefix reference)
  if(NOT ${prefix}_tower OR
     NOT ${prefix}_tower STREQUAL ${reference}_tower OR
     NOT ${prefix}_process STREQUAL ${reference}_process)
    message(FATAL_ERROR
            "cli_startup_test: ${name} (${${prefix}_tower}, "
            "${${prefix}_process}) differs from ${reference} "
            "(${${reference}_tower}, ${${reference}_process})\n"
            "${${prefix}_errors}")
  endif()
endfunction()

function(expect_stderr name prefix pattern)
  if(NOT ${prefix}_errors MATCHES "${pattern}")
    message(FATAL_ERROR
            "cli_startup_test: ${name}: stderr does not match '${pattern}'\n"
            "${${prefix}_errors}")
  endif()
endfunction()

run_digest(save --scenario busy --passes 50 --input-script "${WORK}/save.txt")
if(NOT EXISTS "${tower_file}")
  message(FATAL_ERROR "cli_startup_test: Save As wrote no ${tower_file}")
endif()

run_digest(plain --passes ${M})
run_digest(opened --passes ${M} --open "${tower_file}")

run_digest(new --startup --passes ${M} --input-script "${WORK}/new.txt")
expect_same("New Tower" new plain)
expect_stderr("New Tower" new "startup choice 1")
expect_stderr("New Tower" new "dialog held")
run_digest(enter --startup --passes ${M} --input-script "${WORK}/enter.txt")
expect_same("Enter" enter plain)
run_digest(undriven --startup --passes ${M})
expect_same("undriven" undriven plain)
expect_stderr("undriven" undriven "startup choice 1")

run_digest(load --startup --passes ${M} --input-script "${WORK}/load.txt")
expect_same("Load Saved Tower" load opened)
expect_stderr("Load Saved Tower" load "startup choice 3")
expect_stderr("Load Saved Tower" load "picker open \\(scripted\\)")
run_digest(cancel --startup --passes ${M} --input-script "${WORK}/cancel.txt")
expect_same("cancelled Load" cancel plain)
expect_stderr("cancelled Load" cancel "-> cancel")

run_digest(command_line --startup --passes ${M} --open "${tower_file}")
expect_same("--startup --open" command_line opened)
if(command_line_errors MATCHES "startup choice")
  message(FATAL_ERROR
          "cli_startup_test: --startup --open ran the dialog\n"
          "${command_line_errors}")
endif()

foreach(quit quit esc)
  run_digest(${quit} --startup --passes ${M}
             --input-script "${WORK}/${quit}.txt")
  expect_stderr("${quit}" ${quit} "startup choice 2")
  expect_stderr("${quit}" ${quit} "startup Quit")
  if(${quit}_tower)
    message(FATAL_ERROR "cli_startup_test: ${quit} ran with a tower")
  endif()
endforeach()

run_digest(first --startup --passes 1 --input-script "${WORK}/first.txt"
           --headless-window "${WORK}/first-window.bmp"
           --headless-frame "${WORK}/first-main.bmp")
foreach(frame startup.bmp first-window.bmp first-main.bmp)
  if(NOT EXISTS "${WORK}/${frame}")
    message(FATAL_ERROR "cli_startup_test: no ${WORK}/${frame}")
  endif()
endforeach()
message(STATUS
        "cli_startup_test: New/Enter/undriven/cancel == plain (${plain_tower}),"
        " Load and --open == --open (${opened_tower}), Quit and Esc end")

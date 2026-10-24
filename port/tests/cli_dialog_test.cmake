# Runs one dialog input script (tests/scripts/dialog_*.txt) through
# simtower_sdl headless and checks its exit code, its stderr log and the
# frames it holds. Covers the script verbs that drive the SDL dialog runtime
# (expect-dialog, dialog-click, dialog-key, dialog-wait, dialog-hold) end to
# end: the session's dialog loop, the controllers and the logged answers.
#
#   cmake -DBIN=simtower_sdl -DPACK=original_resources.pack \
#         -DSCRIPT=tests/scripts/x.txt -DWORK=dir -P cli_dialog_test.cmake
#
# Script header lines read here:
#   "# args: ..."              the simtower_sdl options (scenario, passes,
#                              events); --pack, --input-script and
#                              --state-digest are added
#   "# expect-exit: N"         the exit code (default 0)
#   "# expect-stderr: REGEX"   (repeatable) must match the stderr log
#   "# reject-stderr: REGEX"   (repeatable) must not match it
#   "# held: FILE"             (repeatable) a BMP the script's dialog-hold
#                              wrote, relative to WORK (the working
#                              directory of the run)
#   "# same-digest-as: FILE"   the final digest line equals that of FILE
#                              (relative to the script) run with the same
#                              args

foreach(variable BIN PACK SCRIPT WORK)
  if(NOT DEFINED ${variable})
    message(FATAL_ERROR "cli_dialog_test: ${variable} is not set")
  endif()
endforeach()

# Without the game's resource pack the run cannot happen: report a skip
# (the test's SKIP_REGULAR_EXPRESSION).
if(NOT EXISTS "${PACK}")
  message(STATUS "cli_dialog_test: skipped, no resource pack at ${PACK}")
  return()
endif()

file(STRINGS "${SCRIPT}" script_lines)
set(arguments)
set(expect_exit 0)
set(expect_patterns)
set(reject_patterns)
set(held_files)
set(reference_script)
foreach(line IN LISTS script_lines)
  if(line MATCHES "^# args: (.+)$")
    separate_arguments(parsed UNIX_COMMAND "${CMAKE_MATCH_1}")
    list(APPEND arguments ${parsed})
  elseif(line MATCHES "^# expect-exit: ([0-9]+)$")
    set(expect_exit "${CMAKE_MATCH_1}")
  elseif(line MATCHES "^# expect-stderr: (.+)$")
    list(APPEND expect_patterns "${CMAKE_MATCH_1}")
  elseif(line MATCHES "^# reject-stderr: (.+)$")
    list(APPEND reject_patterns "${CMAKE_MATCH_1}")
  elseif(line MATCHES "^# held: (.+)$")
    list(APPEND held_files "${CMAKE_MATCH_1}")
  elseif(line MATCHES "^# same-digest-as: (.+)$")
    get_filename_component(script_directory "${SCRIPT}" DIRECTORY)
    set(reference_script "${script_directory}/${CMAKE_MATCH_1}")
  endif()
endforeach()
if(NOT arguments)
  message(FATAL_ERROR "cli_dialog_test: ${SCRIPT} has no '# args:' line")
endif()

file(MAKE_DIRECTORY "${WORK}")
foreach(held IN LISTS held_files)
  file(REMOVE "${WORK}/${held}")
endforeach()

# Sets <prefix>_result, <prefix>_digest and <prefix>_errors.
function(run_script prefix script)
  execute_process(
    COMMAND "${BIN}" --pack "${PACK}" --input-script "${script}"
            --state-digest ${arguments}
    WORKING_DIRECTORY "${WORK}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors)
  string(REGEX MATCHALL "digest [^\n]+" digests "${output}")
  set(digest "")
  if(digests)
    list(GET digests -1 digest)
  endif()
  set(${prefix}_result "${result}" PARENT_SCOPE)
  set(${prefix}_digest "${digest}" PARENT_SCOPE)
  set(${prefix}_errors "${errors}" PARENT_SCOPE)
endfunction()

run_script(scripted "${SCRIPT}")
if(NOT scripted_result EQUAL expect_exit)
  message(FATAL_ERROR
          "cli_dialog_test: ${SCRIPT} exited ${scripted_result}, expected "
          "${expect_exit}\n${scripted_errors}")
endif()
foreach(pattern IN LISTS expect_patterns)
  if(NOT scripted_errors MATCHES "${pattern}")
    message(FATAL_ERROR
            "cli_dialog_test: ${SCRIPT}: stderr does not match "
            "'${pattern}'\n${scripted_errors}")
  endif()
endforeach()
foreach(pattern IN LISTS reject_patterns)
  if(scripted_errors MATCHES "${pattern}")
    message(FATAL_ERROR
            "cli_dialog_test: ${SCRIPT}: stderr matches '${pattern}'\n"
            "${scripted_errors}")
  endif()
endforeach()
foreach(held IN LISTS held_files)
  set(path "${WORK}/${held}")
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "cli_dialog_test: ${SCRIPT} wrote no ${held}")
  endif()
  file(READ "${path}" magic LIMIT 2)
  if(NOT magic STREQUAL "BM")
    message(FATAL_ERROR "cli_dialog_test: ${path} is not a BMP")
  endif()
endforeach()

if(reference_script)
  run_script(reference "${reference_script}")
  if(NOT reference_result EQUAL 0 OR NOT reference_digest)
    message(FATAL_ERROR
            "cli_dialog_test: reference ${reference_script} exited "
            "${reference_result}\n${reference_errors}")
  endif()
  if(NOT scripted_digest STREQUAL reference_digest)
    message(FATAL_ERROR
            "cli_dialog_test: ${SCRIPT}:\n  ${scripted_digest}\n"
            "differs from ${reference_script}:\n  ${reference_digest}")
  endif()
endif()
message(STATUS "cli_dialog_test: ${SCRIPT}: exit ${scripted_result} "
               "${scripted_digest}")

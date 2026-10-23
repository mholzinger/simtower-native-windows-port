# Runs one input script through simtower_sdl and checks its final digest
# against a reference run. This covers window coordinates -> panel router ->
# shared world input module.
#
#   cmake -DBIN=simtower_sdl -DPACK=original_resources.pack \
#         -DSCRIPT=tests/scripts/x.txt -P cli_input_test.cmake
#
# Script header lines read here:
#   "# passes: N"
#   "# direct-build: SPEC" (repeatable): the reference is --direct-build with
#     every SPEC. Each SPEC must report "built" (a rejected step fails), the
#     tower hashes must be equal, and the script must build something.
#   "# same-digest-as: FILE": the reference is FILE (relative to the
#     script's directory) as another input script, and the whole final
#     digest line (tower, process, host, requests and input) must be equal.

foreach(variable BIN PACK SCRIPT)
  if(NOT DEFINED ${variable})
    message(FATAL_ERROR "cli_input_test: ${variable} is not set")
  endif()
endforeach()

file(STRINGS "${SCRIPT}" script_lines)
set(direct_arguments)
set(direct_count 0)
set(reference_script)
set(passes 3)
foreach(line IN LISTS script_lines)
  if(line MATCHES "^# direct-build: (.+)$")
    list(APPEND direct_arguments --direct-build "${CMAKE_MATCH_1}")
    math(EXPR direct_count "${direct_count} + 1")
  elseif(line MATCHES "^# same-digest-as: (.+)$")
    get_filename_component(script_directory "${SCRIPT}" DIRECTORY)
    set(reference_script "${script_directory}/${CMAKE_MATCH_1}")
  elseif(line MATCHES "^# passes: ([0-9]+)$")
    set(passes "${CMAKE_MATCH_1}")
  endif()
endforeach()
if(NOT direct_arguments AND NOT reference_script)
  message(FATAL_ERROR
          "cli_input_test: ${SCRIPT} has no direct-build or same-digest-as "
          "line")
endif()

# Sets <prefix>_digest (the last "digest ..." line), <prefix>_tower and
# <prefix>_errors (stderr) in the caller's scope.
function(run_digest prefix)
  execute_process(
    COMMAND "${BIN}" --pack "${PACK}" --passes ${passes} --state-digest
            ${ARGN}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR
            "cli_input_test: ${ARGN} exited ${result}\n${output}\n${errors}")
  endif()
  string(REGEX MATCHALL "digest [^\n]+" digests "${output}")
  string(REGEX MATCHALL "tower [0-9a-f]+" towers "${output}")
  if(NOT digests OR NOT towers)
    message(FATAL_ERROR "cli_input_test: no digest from ${ARGN}\n${output}")
  endif()
  list(GET digests -1 digest)
  list(GET towers -1 tower)
  set(${prefix}_digest "${digest}" PARENT_SCOPE)
  set(${prefix}_tower "${tower}" PARENT_SCOPE)
  set(${prefix}_errors "${errors}" PARENT_SCOPE)
endfunction()

run_digest(scripted --input-script "${SCRIPT}")

if(reference_script)
  run_digest(reference --input-script "${reference_script}")
  if(NOT scripted_digest STREQUAL reference_digest)
    message(FATAL_ERROR
            "cli_input_test: ${SCRIPT}:\n  ${scripted_digest}\n"
            "differs from ${reference_script}:\n  ${reference_digest}")
  endif()
  message(STATUS "cli_input_test: ${SCRIPT}: ${scripted_digest}")
  return()
endif()

run_digest(direct ${direct_arguments})
run_digest(untouched)
# Every reference step must succeed: a rejected step would leave the tower
# of the steps before it, which a script can match without doing its work.
if(direct_errors MATCHES "direct build [^\n]*: rejected")
  message(FATAL_ERROR
          "cli_input_test: ${SCRIPT}: a direct-build step was rejected\n"
          "${direct_errors}")
endif()
string(REGEX MATCHALL "direct build [^\n]*: built" built_steps
       "${direct_errors}")
list(LENGTH built_steps built_count)
if(NOT built_count EQUAL direct_count)
  message(FATAL_ERROR
          "cli_input_test: ${SCRIPT}: ${built_count} of ${direct_count} "
          "direct-build steps reported built\n${direct_errors}")
endif()
if(NOT scripted_tower STREQUAL direct_tower)
  message(FATAL_ERROR
          "cli_input_test: ${SCRIPT}: scripted ${scripted_tower} != direct "
          "${direct_tower}")
endif()
if(scripted_tower STREQUAL untouched_tower)
  message(FATAL_ERROR "cli_input_test: ${SCRIPT} built nothing")
endif()
message(STATUS "cli_input_test: ${SCRIPT}: ${scripted_tower}")

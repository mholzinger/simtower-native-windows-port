# Runs one documented example script (sdl/scripts/*.txt) the way its usage
# comment says and requires exit 0, so the examples cannot silently rot
# when coordinates or the layout change.
#
#   cmake -DBIN=simtower_sdl -DPACK=original_resources.pack \
#         -DSCRIPT=sdl/scripts/x.txt -P cli_example_script_test.cmake
#
# The "--passes N" and optional "--scenario NAME" options are read from the
# script's comment lines (its "simtower_sdl ..." usage example).

foreach(variable BIN PACK SCRIPT)
  if(NOT DEFINED ${variable})
    message(FATAL_ERROR "cli_example_script_test: ${variable} is not set")
  endif()
endforeach()

file(STRINGS "${SCRIPT}" comment_lines REGEX "^#")
string(JOIN "\n" comments ${comment_lines})
if(NOT comments MATCHES "--passes ([0-9]+)")
  message(FATAL_ERROR
          "cli_example_script_test: ${SCRIPT} names no --passes N")
endif()
set(arguments --passes "${CMAKE_MATCH_1}")
if(comments MATCHES "--scenario ([a-z]+)")
  list(APPEND arguments --scenario "${CMAKE_MATCH_1}")
endif()

execute_process(
  COMMAND "${BIN}" --pack "${PACK}" --input-script "${SCRIPT}" ${arguments}
          --state-digest
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE errors)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "cli_example_script_test: ${SCRIPT} ${arguments} exited "
          "${result}\n${output}\n${errors}")
endif()
string(REGEX MATCHALL "digest [^\n]+" digests "${output}")
if(NOT digests)
  message(FATAL_ERROR
          "cli_example_script_test: no digest from ${SCRIPT}\n${output}")
endif()
list(GET digests -1 digest)
message(STATUS "cli_example_script_test: ${SCRIPT} ${arguments}: ${digest}")

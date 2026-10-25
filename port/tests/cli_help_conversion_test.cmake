# Converts the user's SIMTOWER.HLP with tools/convert_winhelp.py --strict
# and checks that every topic became a page: the converter must skip
# nothing (exit 0), write one topic-NNN.html per topic it reports, the
# contents and index pages, and help-map.json. Reports a skip without the
# file (the test's SKIP_REGULAR_EXPRESSION). The output stays in WORK.
#
#   cmake -DPYTHON=python3 -DTOOL=tools/convert_winhelp.py \
#         -DHELP=SIMTOWER.HLP -DWORK=dir -P cli_help_conversion_test.cmake

foreach(variable PYTHON TOOL HELP WORK)
  if(NOT DEFINED ${variable})
    message(FATAL_ERROR "cli_help_conversion_test: ${variable} is not set")
  endif()
endforeach()

if(NOT EXISTS "${HELP}")
  message(STATUS "cli_help_conversion_test: skipped, no help file at "
                 "${HELP}")
  return()
endif()

file(REMOVE_RECURSE "${WORK}")
execute_process(
  COMMAND "${PYTHON}" "${TOOL}" --strict "${HELP}" "${WORK}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE errors)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "cli_help_conversion_test: exit ${result}\n"
                      "${output}${errors}")
endif()
if(NOT output MATCHES "([0-9]+) topics, ([0-9]+) bitmaps, 0 warnings")
  message(FATAL_ERROR "cli_help_conversion_test: no summary\n${output}")
endif()
set(topics ${CMAKE_MATCH_1})
set(bitmaps ${CMAKE_MATCH_2})
if(topics EQUAL 0)
  message(FATAL_ERROR "cli_help_conversion_test: no topics")
endif()
file(GLOB pages "${WORK}/topic-*.html")
list(LENGTH pages page_count)
if(NOT page_count EQUAL topics)
  message(FATAL_ERROR "cli_help_conversion_test: ${page_count} pages for "
                      "${topics} topics")
endif()
file(GLOB images "${WORK}/images/*.png")
list(LENGTH images image_count)
if(NOT image_count EQUAL bitmaps)
  message(FATAL_ERROR "cli_help_conversion_test: ${image_count} images for "
                      "${bitmaps} bitmaps")
endif()
foreach(name contents.html index.html style.css help-map.json)
  if(NOT EXISTS "${WORK}/${name}")
    message(FATAL_ERROR "cli_help_conversion_test: no ${name}")
  endif()
endforeach()
message(STATUS "cli_help_conversion_test: ${topics} topics, ${bitmaps} "
               "bitmaps")

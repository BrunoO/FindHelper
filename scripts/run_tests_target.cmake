# Invoked by the run_tests custom target.
# Ensures build_tests completes for the requested CONFIG, then runs ctest -C CONFIG.
#
# Required -D variables:
#   CONFIG            - Debug / Release / etc. (from $<CONFIG> at build time)
#   BINARY_DIR        - CMAKE_BINARY_DIR
# Optional BOOL (TRUE/FALSE/1/0):
#   CTEST_VERBOSE
#   CTEST_EXTRA_VERBOSE

if(NOT DEFINED CONFIG OR CONFIG STREQUAL "")
  message(FATAL_ERROR "run_tests_target.cmake: CONFIG is empty. Pass -DCONFIG=$<CONFIG> from the run_tests target, and build with e.g. cmake --build . --config Debug --target run_tests")
endif()

if(NOT DEFINED BINARY_DIR OR BINARY_DIR STREQUAL "")
  message(FATAL_ERROR "run_tests_target.cmake: BINARY_DIR is not set")
endif()

message(STATUS "run_tests: building test executables for CONFIG=${CONFIG}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${BINARY_DIR}" --config "${CONFIG}" --target build_tests
  RESULT_VARIABLE build_result
)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "run_tests: build_tests failed for CONFIG=${CONFIG} (exit ${build_result})")
endif()

set(_ctest_cmd "${CMAKE_CTEST_COMMAND}" -C "${CONFIG}" --test-dir "${BINARY_DIR}" --output-on-failure)
if(CTEST_EXTRA_VERBOSE)
  list(APPEND _ctest_cmd --extra-verbose)
elseif(CTEST_VERBOSE)
  list(APPEND _ctest_cmd --verbose)
endif()

message(STATUS "run_tests: running ctest -C ${CONFIG}")
execute_process(
  COMMAND ${_ctest_cmd}
  WORKING_DIRECTORY "${BINARY_DIR}"
  RESULT_VARIABLE test_result
)
if(NOT test_result EQUAL 0)
  message(FATAL_ERROR "run_tests: ctest failed for CONFIG=${CONFIG} (exit ${test_result})")
endif()

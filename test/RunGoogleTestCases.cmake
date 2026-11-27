# usage: cmake -DTEST_BINARY=<path> -P RunGoogleTestCases.cmake
#
# Run a GoogleTest binary for a CTest that qualifies a configuration:
# - the whole suite runs, whatever case selection the environment asks for;
# - the process status is kept;
# - the run fails unless GoogleTest's own summary reports a passed case.
# GoogleTest 1.15 has no option for the last point. The binary itself is
# unchanged, so filters and --gtest_list_tests keep working when it is run
# by hand.
if(NOT TEST_BINARY)
    message(FATAL_ERROR "TEST_BINARY is required")
endif()

# Selection that GoogleTest reads from the environment. Sharding has no
# command-line form, so drop the variables for the child. Death-test children
# inherit this environment and get their own flags from the parent.
foreach(_variable IN ITEMS GTEST_FILTER GTEST_TOTAL_SHARDS GTEST_SHARD_INDEX GTEST_SHARD_STATUS_FILE
        GTEST_REPEAT GTEST_COLOR)
    unset(ENV{${_variable}})
endforeach()

# The command line wins over anything left in the environment. Without colour
# the summary label and the count are adjacent on every platform.
execute_process(
    COMMAND "${TEST_BINARY}" "--gtest_filter=*" --gtest_color=no --gtest_repeat=1
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _output
    ECHO_OUTPUT_VARIABLE
    ECHO_ERROR_VARIABLE)
if(NOT _result STREQUAL "0")
    message(FATAL_ERROR "${TEST_BINARY} failed: ${_result}")
endif()
# A complete summary line, "[  PASSED  ] N tests." with N of at least one.
if(NOT _output MATCHES "(^|\n)\\[  PASSED  \\] [1-9][0-9]* tests?\\.\r?(\n|$)")
    message(FATAL_ERROR "${TEST_BINARY} passed without running a GoogleTest case")
endif()

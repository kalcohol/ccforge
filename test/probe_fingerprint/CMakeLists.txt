add_test(
    NAME probe_fingerprint_inputs
    COMMAND ${CMAKE_COMMAND}
        -DFORGE_FINGERPRINT_MODULE=${CMAKE_SOURCE_DIR}/cmake/ForgeProbeFingerprint.cmake
        -DFORGE_FINGERPRINT_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/inputs-work
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_probe_fingerprint_inputs.cmake)

add_test(
    NAME probe_fingerprint_reconfigure
    COMMAND ${CMAKE_COMMAND}
        -DFORGE_FINGERPRINT_MODULE=${CMAKE_SOURCE_DIR}/cmake/ForgeProbeFingerprint.cmake
        -DFORGE_FINGERPRINT_FIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/reconfigure
        -DFORGE_FINGERPRINT_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/reconfigure-work
        -DFORGE_TEST_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DFORGE_TEST_GENERATOR=${CMAKE_GENERATOR}
        -DFORGE_TEST_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}
        -DFORGE_TEST_GENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_probe_fingerprint_reconfigure.cmake)

set_tests_properties(
    probe_fingerprint_inputs
    probe_fingerprint_reconfigure
    PROPERTIES TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})

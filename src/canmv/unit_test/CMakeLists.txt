cmake_minimum_required(VERSION 3.16)
project(canmv_unit_test LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

execute_process(
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_test_suggestions.sh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE GEN_RESULT
)
if(NOT GEN_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to generate ALL_SOURCE_UNIT_TEST_SUGGESTIONS.md")
endif()

find_package(Threads REQUIRED)
find_package(GTest REQUIRED)
include(GoogleTest)

enable_testing()

set(CANMV_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_library(canmv_host_shims STATIC
    src/xalloc_host.c
    src/micropython_host_stubs.c
)
target_include_directories(canmv_host_shims PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs"
    "${CANMV_ROOT}/port/omv/alloc"
)

add_executable(canmv_unit_tests
    tests/test_all_sources_catalog.cpp
    tests/test_array.cpp
    tests/test_ringbuf.cpp
    tests/test_unaligned_memcpy.cpp
    tests/test_mutex.cpp
    tests/test_ff_wrapper.cpp
    tests/test_python_sources.cpp
    tests/test_recent_fixes_guardrails.cpp
    "${CANMV_ROOT}/port/omv/common/array.c"
    "${CANMV_ROOT}/port/omv/common/ringbuf.c"
    "${CANMV_ROOT}/port/omv/alloc/unaligned_memcpy.c"
    "${CANMV_ROOT}/port/omv/common/mutex.c"
    "${CANMV_ROOT}/port/omv/common/ff_wrapper.c"
)

target_compile_definitions(canmv_unit_tests PRIVATE
    CANMV_SOURCE_ROOT="${CANMV_ROOT}"
    CANMV_UNIT_TEST_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_include_directories(canmv_unit_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs"
    "${CANMV_ROOT}/port/omv/common"
    "${CANMV_ROOT}/port/omv/alloc"
)

target_link_libraries(canmv_unit_tests PRIVATE
    canmv_host_shims
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

gtest_discover_tests(canmv_unit_tests
    DISCOVERY_TIMEOUT 60
)

# Compile the production buffer manager against real SDK frame types; only the
# MicroPython GIL and hardware MPI calls are replaced by the host test model.
add_executable(canmv_vicap_buffer_tests
    tests/test_vicap_buffer.cpp
    "${CANMV_ROOT}/port/core/mpp_vb_mgmt.c"
)
target_compile_definitions(canmv_vicap_buffer_tests PRIVATE _DEFAULT_SOURCE)
target_include_directories(canmv_vicap_buffer_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs"
    "${CANMV_ROOT}/port/include/core"
    "${CANMV_ROOT}/../rtsmart/mpp/userapps/api"
    "${CANMV_ROOT}/../rtsmart/mpp/include"
    "${CANMV_ROOT}/../rtsmart/mpp/include/comm"
)
target_link_libraries(canmv_vicap_buffer_tests PRIVATE
    GTest::gtest GTest::gtest_main Threads::Threads
)
gtest_discover_tests(canmv_vicap_buffer_tests DISCOVERY_TIMEOUT 60)

# Exercise the production IDE encoder with frame ownership enforced by MPI mocks.
add_executable(canmv_wbc_buffer_tests
    tests/test_wbc_buffer.cpp
    "${CANMV_ROOT}/port/omv/ide_dbg_vo_wbc.c"
)
target_compile_definitions(canmv_wbc_buffer_tests PRIVATE _DEFAULT_SOURCE)
target_include_directories(canmv_wbc_buffer_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs"
    "${CANMV_ROOT}/../../include"
    "${CANMV_ROOT}/port/include"
    "${CANMV_ROOT}/../rtsmart/libs/rtsmart_hal/utils"
    "${CANMV_ROOT}/../rtsmart/mpp/userapps/api"
    "${CANMV_ROOT}/../rtsmart/mpp/userapps/api/framework"
    "${CANMV_ROOT}/../rtsmart/mpp/include"
    "${CANMV_ROOT}/../rtsmart/mpp/include/comm"
)
target_link_libraries(canmv_wbc_buffer_tests PRIVATE
    GTest::gtest GTest::gtest_main Threads::Threads
)
gtest_discover_tests(canmv_wbc_buffer_tests DISCOVERY_TIMEOUT 60)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME SensorBufferTest
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_sensor_buffer.py"
)

add_test(NAME DevfsBlockIoTest
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_devfs_block_io.py"
)

add_test(NAME DisplayBufferTest
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_display_buffer.py"
)

add_test(NAME SensorLifecycleTest
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_sensor_lifecycle.py"
)

add_test(NAME SensorSoftResetTest
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_sensor_soft_reset.py"
)

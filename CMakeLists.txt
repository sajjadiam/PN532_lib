cmake_minimum_required(VERSION 3.16)
project(PN532_lib LANGUAGES C)

add_library(pn532_frame STATIC
    pn532/pn532_frame.c
    pn532/pn532_rx.c
    pn532/pn532_hsu.c
    pn532/pn532.c
    pn532/pn532_registry.c
)
target_include_directories(pn532_frame PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/pn532"
)
target_compile_features(pn532_frame PUBLIC c_std_11)

include(CTest)
if(BUILD_TESTING)
    add_executable(pn532_frame_tests tests/test_pn532_frame.c)
    target_link_libraries(pn532_frame_tests PRIVATE pn532_frame)
    add_test(NAME pn532_frame_tests COMMAND pn532_frame_tests)

    add_executable(pn532_rx_tests tests/test_pn532_rx.c)
    target_link_libraries(pn532_rx_tests PRIVATE pn532_frame)
    add_test(NAME pn532_rx_tests COMMAND pn532_rx_tests)

    add_executable(pn532_hsu_tests tests/test_pn532_hsu.c)
    target_link_libraries(pn532_hsu_tests PRIVATE pn532_frame)
    add_test(NAME pn532_hsu_tests COMMAND pn532_hsu_tests)

    add_executable(pn532_core_tests tests/test_pn532.c)
    target_link_libraries(pn532_core_tests PRIVATE pn532_frame)
    add_test(NAME pn532_core_tests COMMAND pn532_core_tests)

    add_executable(pn532_integration_tests tests/test_pn532_integration.c)
    target_link_libraries(pn532_integration_tests PRIVATE pn532_frame)
    add_test(NAME pn532_integration_tests COMMAND pn532_integration_tests)

    add_executable(pn532_registry_tests tests/test_pn532_registry.c)
    target_link_libraries(pn532_registry_tests PRIVATE pn532_frame)
    add_test(NAME pn532_registry_tests COMMAND pn532_registry_tests)
endif()

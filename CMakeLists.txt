cmake_minimum_required(VERSION 3.16)
project(PN532_lib LANGUAGES C)

add_library(pn532_frame STATIC pn532/pn532_frame.c)
target_include_directories(pn532_frame PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/pn532"
)
target_compile_features(pn532_frame PUBLIC c_std_11)

include(CTest)
if(BUILD_TESTING)
    add_executable(pn532_frame_tests tests/test_pn532_frame.c)
    target_link_libraries(pn532_frame_tests PRIVATE pn532_frame)
    add_test(NAME pn532_frame_tests COMMAND pn532_frame_tests)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.10)
    project(SigilSecurityTests LANGUAGES CXX)
    enable_testing()
    set(SIGIL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")
else()
    set(SIGIL_SOURCE_DIR "${CMAKE_SOURCE_DIR}/src")
endif()

add_executable(zip_extraction_limits_test
    ZipExtractionLimitsTest.cpp
    ${SIGIL_SOURCE_DIR}/Misc/ZipExtractionLimits.cpp
)

target_include_directories(zip_extraction_limits_test PRIVATE
    ${SIGIL_SOURCE_DIR}
)

set_target_properties(zip_extraction_limits_test PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

add_test(NAME zip_extraction_limits_test COMMAND zip_extraction_limits_test)

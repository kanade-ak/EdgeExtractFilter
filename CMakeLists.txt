cmake_minimum_required(VERSION 3.20)

project(EdgeExtractFilter LANGUAGES CXX)

add_library(EdgeExtractFilter SHARED
    EdgeExtractFilter.cpp
)

target_include_directories(EdgeExtractFilter PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../aviutl2_sdk"
)

target_compile_features(EdgeExtractFilter PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(EdgeExtractFilter PRIVATE /utf-8 /W4 /permissive- /wd4100 /wd4201 /wd4828)
    target_compile_definitions(EdgeExtractFilter PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        UNICODE
        _UNICODE
    )
endif()

set_target_properties(EdgeExtractFilter PROPERTIES
    PREFIX ""
    SUFFIX ".auf2"
    OUTPUT_NAME "EdgeExtractFilter"
)

set(AVIUTL2_PLUGIN_DIR "${CMAKE_CURRENT_LIST_DIR}/../aviutl2beta44/data/Plugin/EdgeExtractFilter")

add_custom_command(TARGET EdgeExtractFilter POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${AVIUTL2_PLUGIN_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:EdgeExtractFilter>"
        "${AVIUTL2_PLUGIN_DIR}/$<TARGET_FILE_NAME:EdgeExtractFilter>"
)

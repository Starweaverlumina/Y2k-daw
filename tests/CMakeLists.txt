include(CTest)
include("${catch2_SOURCE_DIR}/extras/Catch.cmake")

file(GLOB_RECURSE Y2K_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/**/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# Exclude recording tests unless headers present
set(Y2K_FILTERED "")
foreach(src ${Y2K_TEST_SOURCES})
    get_filename_component(src_dir "${src}" DIRECTORY)
    get_filename_component(dir_name "${src_dir}" NAME)
    if(dir_name STREQUAL "recording")
        if(EXISTS "${CMAKE_SOURCE_DIR}/engine/y2k_dsp/recording/audio_recorder.h")
            list(APPEND Y2K_FILTERED "${src}")
        endif()
    else()
        list(APPEND Y2K_FILTERED "${src}")
    endif()
endforeach()

add_executable(y2k_tests ${Y2K_FILTERED})

target_include_directories(y2k_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/engine
    ${CMAKE_SOURCE_DIR}/engine/y2k_dsp
    ${CMAKE_SOURCE_DIR}/engine/y2k_dsp/graph
    ${CMAKE_SOURCE_DIR}/engine/y2k_dsp/meter
    ${CMAKE_SOURCE_DIR}/engine/y2k_dsp/mixer
    ${CMAKE_SOURCE_DIR}/engine/y2k_dsp/voice
    ${CMAKE_SOURCE_DIR}/engine/y2k_dsp/param
    ${CMAKE_SOURCE_DIR}/engine/y2k_engine
    ${CMAKE_SOURCE_DIR}/engine/y2k_engine/mixer
    ${CMAKE_SOURCE_DIR}/engine/y2k_engine/graph_synth
    ${CMAKE_SOURCE_DIR}/engine/y2k_engine/session
    ${CMAKE_SOURCE_DIR}/ui
    ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(y2k_tests PRIVATE
    y2k_dsp
    y2k_engine
    Catch2::Catch2WithMain
    Threads::Threads
)

target_compile_features(y2k_tests PRIVATE cxx_std_20)
catch_discover_tests(y2k_tests)

add_library(y2k_dsp STATIC
    graph/processor_graph.cpp
    generators/sine_osc.cpp
    instruments/bondi_synth.cpp
    instruments/sampler_player.cpp
    effects/grape_compressor.cpp
    mixer/track_gain.cpp
    mixer/clip_scheduler.cpp
    # v9 additions: header-only nodes are included via .h
    # but any future .cpp files for meter/mixer go here
)

foreach(src
    recording/audio_recorder.cpp
    recording/midi_recorder.cpp
    recording/audio_file_importer.cpp)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${src}")
        target_sources(y2k_dsp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/${src}")
    endif()
endforeach()

target_include_directories(y2k_dsp
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}        # so #include "y2k_dsp/..." works
        ${CMAKE_CURRENT_SOURCE_DIR}/..     # so engine-level includes work
        ${CMAKE_CURRENT_SOURCE_DIR}/graph  # so pdc.h / latency_compensator.h are found
        ${CMAKE_CURRENT_SOURCE_DIR}/meter
        ${CMAKE_CURRENT_SOURCE_DIR}/mixer
        ${CMAKE_CURRENT_SOURCE_DIR}/voice
        ${CMAKE_CURRENT_SOURCE_DIR}/param
    PRIVATE
        ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(y2k_dsp
    PUBLIC
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_core
        Threads::Threads
    PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

target_compile_features(y2k_dsp PUBLIC cxx_std_20)

if(NOT MSVC)
    target_compile_options(y2k_dsp PRIVATE -ffast-math)
else()
    target_compile_options(y2k_dsp PRIVATE /fp:fast)
endif()

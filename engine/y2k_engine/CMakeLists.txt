add_library(y2k_engine STATIC
    session.cpp
    undo.cpp
    midi_learn.cpp
    autoplay.cpp
    automation_writer.cpp
    mixer/track_model.cpp
    session/session_schema.cpp          # v9: TrackModel / BusModel / ProjectRouting
    session/session_serializer.cpp
    session/session_validator.cpp
    bounce_engine.cpp
)

target_include_directories(y2k_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..        # engine root
        ${CMAKE_CURRENT_SOURCE_DIR}/mixer     # track_model.h
        ${CMAKE_CURRENT_SOURCE_DIR}/graph_synth  # graph_compiler.h
        ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(y2k_engine
    PUBLIC
        y2k_dsp                               # graph_compiler.h pulls DSP nodes
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_data_structures
        Threads::Threads
    PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

target_compile_features(y2k_engine PUBLIC cxx_std_20)

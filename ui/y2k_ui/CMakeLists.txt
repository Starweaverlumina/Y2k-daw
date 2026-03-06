add_library(y2k_ui STATIC
    screens/arrange_view.cpp
    screens/piano_roll.cpp
    screens/mixer_view.cpp
    components/y2k_kontrol_panel.cpp
    components/effects_chain.cpp
)

target_include_directories(y2k_ui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    PRIVATE
        ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(y2k_ui
    PUBLIC
        y2k_engine
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_core
    PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

target_compile_features(y2k_ui PUBLIC cxx_std_20)

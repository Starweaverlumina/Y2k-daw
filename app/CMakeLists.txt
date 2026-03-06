# app — Y2K DAW executable

juce_add_gui_app(Y2KDAW
    PRODUCT_NAME   "Y2K DAW"
    BUNDLE_ID      "com.y2kdaw.app"
    VERSION        "0.1.0"
    NEEDS_CURL     FALSE
    NEEDS_WEB_BROWSER FALSE
    ICON_BIG       ""
    MICROPHONE_PERMISSION_ENABLED  FALSE
    AUDIO_SAMPLE_RATE 44100
)

juce_generate_juce_header(Y2KDAW)

target_sources(Y2KDAW PRIVATE
    main.cpp
    y2k_application.cpp
    main_window.cpp
)

target_include_directories(Y2KDAW PRIVATE
    ${CMAKE_SOURCE_DIR}/engine
    ${CMAKE_SOURCE_DIR}/ui
    ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(Y2KDAW PRIVATE
    y2k_dsp
    y2k_engine
    y2k_ui
    juce::juce_audio_devices
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_data_structures
    juce::juce_core
    Threads::Threads
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

target_compile_features(Y2KDAW PRIVATE cxx_std_20)

#  Platform-branched install
if(APPLE)
    install(TARGETS Y2KDAW
        BUNDLE DESTINATION /Applications
    )
elseif(WIN32)
    install(TARGETS Y2KDAW
        RUNTIME DESTINATION "C:/Program Files/Y2K DAW"
    )
else()
    install(TARGETS Y2KDAW
        RUNTIME DESTINATION bin
    )
endif()

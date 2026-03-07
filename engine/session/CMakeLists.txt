# ─────────────────────────────────────────────────────────────────
#  engine/session — SessionDocument module
#
#  Provides the single-source-of-truth project state layer:
#    SessionDocument  (owner of state, undo stack, save/load)
#    Commands         (all reversible mutations)
#    SessionSchema    (schema constants + migration facade)
#    UndoStack        (command-pattern history, header-only)
#
#  Depends on: y2k_engine (for ProjectRouting, Y2KSession, SessionSchema)
#  Consumed by: app/ and any UI code that needs project editing.
# ─────────────────────────────────────────────────────────────────

add_library(y2k_session STATIC
    SessionDocument.cpp
    Commands.cpp
)

target_include_directories(y2k_session
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}          # exposes engine/session/ headers
        ${CMAKE_CURRENT_SOURCE_DIR}/..       # exposes engine/ root (for y2k_engine/)
)

target_link_libraries(y2k_session
    PUBLIC
        y2k_engine                           # ProjectRouting, Y2KSession, SessionSchema
        juce::juce_core
        juce::juce_data_structures
    PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

target_compile_features(y2k_session PUBLIC cxx_std_20)

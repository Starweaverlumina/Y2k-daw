#pragma once

// ═══════════════════════════════════════════════════════════════════
//  Y2K UI — PianoRoll
//  MIDI note editor for a single MidiClip.
//
//  Layout:
//   ┌──[kKeyW=48px]──┬──────────────────────[grid]──┐
//   │  C5 (white)    │  ████         ██              │
//   │  B4 (white)    │                               │
//   │  A#4 (black)   │       ████                    │
//   │  ...           │                               │
//   └────────────────┴───────────────────────────────┘
//
//  Edit modes: Draw (pencil), Select (marquee), Erase (rubber).
// ═══════════════════════════════════════════════════════════════════

#include <juce_gui_basics/juce_gui_basics.h>
#include "../../../engine/y2k_engine/session.h"
#include "../../../engine/y2k_engine/session/session_command_bus.h"
#include <functional>

namespace y2k::ui {

// ─────────────────────────────────────────────────────────────────
//  NoteRect  —  a lightweight copy used for hit-testing and display
// ─────────────────────────────────────────────────────────────────
struct NoteRect {
    int    pitch;
    double startBeat;
    double durationBeats;
    float  velocity;
};

// ─────────────────────────────────────────────────────────────────
//  PianoRoll
// ─────────────────────────────────────────────────────────────────
class PianoRoll final : public juce::Component, private juce::Timer {
public:
    // Layout constants
    static constexpr int   kKeyW    = 48;    // Piano keyboard strip width (px)
    static constexpr int   kRowH    = 14;    // Pixels per semitone row
    static constexpr int   kNumKeys = 88;    // A0 (MIDI 21) … C8 (MIDI 108)
    static constexpr float kDefPPB  = 80.f;  // Default pixels per beat

    PianoRoll();

    // ── Data binding ──────────────────────────────────────────────
    // Load a MidiClip for display and editing.
    void loadClip(y2k::engine::MidiClip*          clip,
                  y2k::engine::SessionCommandBus*  bus);

    // ── View controls (call from UI thread) ───────────────────────
    void setPixelsPerBeat(float ppb) noexcept { pixelsPerBeat_ = ppb; repaint(); }
    void setScrollBeat  (double b)  noexcept { scrollBeat_    = b;   repaint(); }
    // scrollPitch: MIDI pitch number shown at the top row
    void setScrollPitch (int p)     noexcept { scrollPitch_   = p;   repaint(); }

    // ── Edit mode ─────────────────────────────────────────────────
    enum class EditMode { Draw, Select, Erase };
    void     setEditMode(EditMode m) noexcept { mode_ = m; }
    EditMode editMode()        const noexcept { return mode_; }

    // ── Callback ──────────────────────────────────────────────────
    // Fires whenever a note is added, removed, or resized.
    std::function<void()> onClipChanged;

    // ── juce::Component overrides ─────────────────────────────────
    void paint          (juce::Graphics& g) override;
    void resized        () override;
    void mouseDown      (const juce::MouseEvent& e) override;
    void mouseDrag      (const juce::MouseEvent& e) override;
    void mouseUp        (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e,
                         const juce::MouseWheelDetails& w) override;

private:
    // ── Draw helpers ──────────────────────────────────────────────
    void drawPianoKeys (juce::Graphics& g) const;
    void drawGrid      (juce::Graphics& g) const;
    void drawNotes     (juce::Graphics& g) const;
    void drawGhostNote (juce::Graphics& g) const;   // note being dragged in Draw mode
    void drawPlayhead  (juce::Graphics& g) const;

    // ── Coordinate helpers ────────────────────────────────────────
    juce::Rectangle<int> gridArea()   const noexcept;
    float  beatToX (double beat)      const noexcept;
    double xToBeat (float  x)         const noexcept;
    int    pitchToY(int    pitch)      const noexcept;  // top of row for this pitch
    int    yToPitch(int    y)          const noexcept;

    // ── Key helpers ───────────────────────────────────────────────
    bool         isBlackKey (int pitch) const noexcept;
    juce::String pitchName  (int pitch) const;

    // ── Note editing helpers ───────────────────────────────────────
    // Returns the index of the note under pixel (px, py), or -1.
    int  noteAtPoint(int px, int py) const;
    // Quantise a raw beat to the clip's grid division.
    double quantise(double beat) const noexcept;

    // ── juce::Timer (unused — reserved for playhead animation) ────
    void timerCallback() override {}

    // ── State ─────────────────────────────────────────────────────
    y2k::engine::MidiClip*          clip_          = nullptr;
    y2k::engine::SessionCommandBus*  bus_           = nullptr;

    float  pixelsPerBeat_ = kDefPPB;
    double scrollBeat_    = 0.0;
    int    scrollPitch_   = 84;   // C6 at top by default

    EditMode mode_ = EditMode::Draw;

    // ── Draw-mode drag state ──────────────────────────────────────
    bool   dragging_      = false;
    int    dragPitch_     = -1;
    double dragStartBeat_ = 0.0;
    double dragEndBeat_   = 0.0;
    // dragNoteIdx_ >= 0  → resizing/moving existing note
    // dragNoteIdx_ == -1 → drawing a brand-new note
    int    dragNoteIdx_   = -1;

    // ── Select-mode drag state ────────────────────────────────────
    juce::Rectangle<int> selectionRect_;
    bool                 selectDragging_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRoll)
};

} // namespace y2k::ui

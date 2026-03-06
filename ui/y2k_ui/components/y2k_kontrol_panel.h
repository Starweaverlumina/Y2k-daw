#pragma once
// ═══════════════════════════════════════════════════════════════════
//  ui/y2k_ui/components/y2k_kontrol_panel.h
//
//  Y2KKontrolPanel — 25-key piano keyboard + 8 rotary knobs.
//
//  Layout:
//    ┌──────────────────────────────────────────────────┐
//    │  Knob0  Knob1  Knob2  Knob3  Knob4  Knob5  Knob6  Knob7  │ kKnobRowH
//    ├──────────────────────────────────────────────────┤
//    │                25-key keyboard (C3–C5)           │ kKeyH
//    └──────────────────────────────────────────────────┘
//
//  Callbacks (set from outside, called on message thread):
//    onNoteOn (int midiNote, float velocity)
//    onNoteOff(int midiNote)
//    onKnobChanged(int knobIndex, float value01)
//    onKnobRightClicked(int knobIndex)   // for MIDI Learn arm
// ═══════════════════════════════════════════════════════════════════

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <array>

namespace y2k::ui {

// ─────────────────────────────────────────────────────────────────
//  Y2KKnob — single rotary knob with label
// ─────────────────────────────────────────────────────────────────
class Y2KKnob final : public juce::Component {
public:
    explicit Y2KKnob(const juce::String& label, juce::Colour accent);

    void setValue(float v01) noexcept;  // 0..1, no callback
    float getValue() const noexcept;

    std::function<void(float)>  onChange;         // value 0..1
    std::function<void()>       onRightClick;     // MIDI Learn

    void paint   (juce::Graphics&) override;
    void resized ()                override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

private:
    juce::Slider slider_;
    juce::Label  label_;
    juce::Colour accent_;
    juce::Point<int> dragStart_;
    float           dragStartVal_ = 0.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Y2KKnob)
};

// ─────────────────────────────────────────────────────────────────
//  Y2KPianoKeyboard — 25 keys, C3–C5 (MIDI 48–72)
// ─────────────────────────────────────────────────────────────────
class Y2KPianoKeyboard final : public juce::Component {
public:
    static constexpr int kStartNote = 48;   // C3
    static constexpr int kNumKeys   = 25;   // C3–C5 inclusive
    static constexpr int kEndNote   = kStartNote + kNumKeys - 1;

    Y2KPianoKeyboard();

    std::function<void(int note, float vel)> onNoteOn;
    std::function<void(int note)>            onNoteOff;

    void paint   (juce::Graphics&) override;
    void resized ()                override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    // Show a note as pressed (from MIDI input, RT-safe via repaint)
    void highlightNote(int midiNote, bool on);

private:
    static bool isBlackKey(int noteInOctave) noexcept;
    int  noteAtPosition(juce::Point<float> pos) const noexcept;
    juce::Rectangle<float> keyRect(int midiNote) const noexcept;

    std::array<bool, 128> pressed_ {};
    int  activeNote_ = -1;

    // Layout cache (set in resized)
    float whiteKeyW_  = 0.f;
    float blackKeyW_  = 0.f;
    float blackKeyH_  = 0.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Y2KPianoKeyboard)
};

// ─────────────────────────────────────────────────────────────────
//  Y2KKontrolPanel — container for keyboard + knobs
// ─────────────────────────────────────────────────────────────────
class Y2KKontrolPanel final : public juce::Component {
public:
    static constexpr int kNumKnobs  = 8;
    static constexpr int kKnobRowH  = 80;
    static constexpr int kKeyH      = 100;
    static constexpr int kMinWidth  = 500;
    static constexpr int kPanelH    = kKnobRowH + kKeyH + 4;

    Y2KKontrolPanel();
    ~Y2KKontrolPanel() override = default;

    // Callbacks — set from outside before showing
    std::function<void(int note, float vel)> onNoteOn;
    std::function<void(int note)>            onNoteOff;
    std::function<void(int knobIdx, float v)> onKnobChanged;
    std::function<void(int knobIdx)>          onKnobRightClicked;

    // Update knob from external source (thread-safe: posts to message thread)
    void setKnobValue(int knobIdx, float v01);

    void paint  (juce::Graphics&) override;
    void resized()                 override;

    static const char* knobName(int i) noexcept;
    static juce::Colour knobAccent(int i) noexcept;

private:
    std::array<std::unique_ptr<Y2KKnob>, kNumKnobs> knobs_;
    Y2KPianoKeyboard keyboard_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Y2KKontrolPanel)
};

} // namespace y2k::ui

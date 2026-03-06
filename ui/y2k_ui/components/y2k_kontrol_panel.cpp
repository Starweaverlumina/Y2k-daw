// ═══════════════════════════════════════════════════════════════════
//  ui/y2k_ui/components/y2k_kontrol_panel.cpp
// ═══════════════════════════════════════════════════════════════════

#include "y2k_kontrol_panel.h"
#include "../theme.h"
#include <cmath>

namespace y2k::ui {

// ═════════════════════════════════════════════════════════════════
//  Y2KKnob
// ═════════════════════════════════════════════════════════════════

Y2KKnob::Y2KKnob(const juce::String& labelText, juce::Colour accent)
    : accent_(accent)
{
    slider_.setRange(0.0, 1.0, 0.001);
    slider_.setValue(0.5);
    slider_.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider_.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    slider_.onValueChange = [this] {
        if (onChange) onChange(static_cast<float>(slider_.getValue()));
    };
    addAndMakeVisible(slider_);

    label_.setText(labelText, juce::dontSendNotification);
    label_.setFont(juce::Font(9.f, juce::Font::bold));
    label_.setJustificationType(juce::Justification::centredTop);
    label_.setColour(juce::Label::textColourId, juce::Colour(0xFF444440));
    addAndMakeVisible(label_);
}

void Y2KKnob::setValue(float v01) noexcept {
    slider_.setValue(static_cast<double>(v01), juce::dontSendNotification);
}

float Y2KKnob::getValue() const noexcept {
    return static_cast<float>(slider_.getValue());
}

void Y2KKnob::paint(juce::Graphics& g) {
    const auto b = getLocalBounds().toFloat();

    // Background pill
    g.setColour(juce::Colour(0xFFE0E0DC));
    g.fillRoundedRectangle(b.reduced(1.f), 8.f);
    g.setColour(accent_.withAlpha(0.35f));
    g.drawRoundedRectangle(b.reduced(1.f), 8.f, 1.f);
}

void Y2KKnob::resized() {
    auto r = getLocalBounds();
    label_.setBounds(r.removeFromBottom(14));
    slider_.setBounds(r.reduced(4));
}

void Y2KKnob::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isRightButtonDown()) {
        if (onRightClick) onRightClick();
        return;
    }
    slider_.mouseDown(e.getEventRelativeTo(&slider_));
}

void Y2KKnob::mouseDrag(const juce::MouseEvent& e) {
    slider_.mouseDrag(e.getEventRelativeTo(&slider_));
}

void Y2KKnob::mouseUp(const juce::MouseEvent& e) {
    slider_.mouseUp(e.getEventRelativeTo(&slider_));
}

// ═════════════════════════════════════════════════════════════════
//  Y2KPianoKeyboard
// ═════════════════════════════════════════════════════════════════

Y2KPianoKeyboard::Y2KPianoKeyboard() {
    pressed_.fill(false);
    setWantsKeyboardFocus(false);
}

bool Y2KPianoKeyboard::isBlackKey(int noteInOctave) noexcept {
    static const bool black[] = { false,true,false,true,false,false,
                                   true,false,true,false,true,false };
    return black[noteInOctave % 12];
}

// Count white keys from startNote up to (not including) midiNote
static int whiteKeysBefore(int startNote, int midiNote) {
    int count = 0;
    for (int n = startNote; n < midiNote; ++n) {
        const int pos = n % 12;
        static const bool black[] = { false,true,false,true,false,false,
                                       true,false,true,false,true,false };
        if (!black[pos]) ++count;
    }
    return count;
}

static int totalWhiteKeys(int startNote, int numKeys) {
    int count = 0;
    for (int n = startNote; n < startNote + numKeys; ++n) {
        const int pos = n % 12;
        static const bool black[] = { false,true,false,true,false,false,
                                       true,false,true,false,true,false };
        if (!black[pos]) ++count;
    }
    return count;
}

void Y2KPianoKeyboard::resized() {
    const int numWhite = totalWhiteKeys(kStartNote, kNumKeys);
    whiteKeyW_ = static_cast<float>(getWidth())  / numWhite;
    blackKeyW_ = whiteKeyW_ * 0.6f;
    blackKeyH_ = static_cast<float>(getHeight()) * 0.60f;
}

juce::Rectangle<float> Y2KPianoKeyboard::keyRect(int midiNote) const noexcept {
    const int notePos = midiNote % 12;
    static const bool black[] = { false,true,false,true,false,false,
                                   true,false,true,false,true,false };
    const bool isBlack = black[notePos];

    const int whitesBefore = whiteKeysBefore(kStartNote, midiNote);
    const float h = static_cast<float>(getHeight());

    if (!isBlack) {
        return { whitesBefore * whiteKeyW_, 0.f, whiteKeyW_ - 1.f, h };
    } else {
        // Position black key centred over the gap between white keys
        const float cx = whitesBefore * whiteKeyW_;
        return { cx - blackKeyW_ * 0.5f, 0.f, blackKeyW_, blackKeyH_ };
    }
}

int Y2KPianoKeyboard::noteAtPosition(juce::Point<float> pos) const noexcept {
    // Check black keys first (they're on top)
    for (int n = kStartNote; n <= kEndNote; ++n) {
        if (isBlackKey(n % 12)) {
            if (keyRect(n).contains(pos)) return n;
        }
    }
    // Then white keys
    for (int n = kStartNote; n <= kEndNote; ++n) {
        if (!isBlackKey(n % 12)) {
            if (keyRect(n).contains(pos)) return n;
        }
    }
    return -1;
}

void Y2KPianoKeyboard::paint(juce::Graphics& g) {
    const float h = static_cast<float>(getHeight());

    // White keys
    for (int n = kStartNote; n <= kEndNote; ++n) {
        if (isBlackKey(n % 12)) continue;
        const auto r = keyRect(n);
        const bool isPressed = pressed_[static_cast<size_t>(n)];

        if (isPressed) {
            g.setColour(Color::forTrackIndex(3).withAlpha(0.5f));  // lime tint
        } else {
            // Gradient: warm white top → platinum bottom
            g.setGradientFill(juce::ColourGradient(
                juce::Colour(0xFFFFFFFD), r.getX(), 0.f,
                juce::Colour(0xFFDDDDD8), r.getX(), h, false));
        }
        g.fillRect(r);
        g.setColour(juce::Colour(0xFFB0B0A8));
        g.drawRect(r, 1.f);
    }

    // Black keys (draw over white)
    for (int n = kStartNote; n <= kEndNote; ++n) {
        if (!isBlackKey(n % 12)) continue;
        const auto r = keyRect(n);
        const bool isPressed = pressed_[static_cast<size_t>(n)];

        if (isPressed) {
            g.setColour(Color::forTrackIndex(0).withAlpha(0.8f));  // bondi tint
        } else {
            g.setGradientFill(juce::ColourGradient(
                juce::Colour(0xFF2A2A2A), r.getX(), 0.f,
                juce::Colour(0xFF1A1A1A), r.getX(), r.getBottom(), false));
        }
        g.fillRoundedRectangle(r, 3.f);
        g.setColour(juce::Colour(0xFF555555));
        g.drawRoundedRectangle(r, 3.f, 0.5f);
    }

    // Octave labels (C note labels)
    g.setFont(juce::Font(8.f));
    g.setColour(juce::Colour(0xFF888884));
    for (int n = kStartNote; n <= kEndNote; ++n) {
        if ((n % 12) == 0 && !isBlackKey(n % 12)) {
            const auto r = keyRect(n);
            g.drawText("C" + juce::String(n / 12 - 1),
                        r.withTop(h - 14.f), juce::Justification::centred);
        }
    }
}

void Y2KPianoKeyboard::mouseDown(const juce::MouseEvent& e) {
    const int note = noteAtPosition(e.position);
    if (note >= 0 && note <= 127) {
        pressed_[static_cast<size_t>(note)] = true;
        activeNote_ = note;
        if (onNoteOn) onNoteOn(note, 0.8f);
        repaint();
    }
}

void Y2KPianoKeyboard::mouseUp(const juce::MouseEvent&) {
    if (activeNote_ >= 0) {
        pressed_[static_cast<size_t>(activeNote_)] = false;
        if (onNoteOff) onNoteOff(activeNote_);
        activeNote_ = -1;
        repaint();
    }
}

void Y2KPianoKeyboard::mouseDrag(const juce::MouseEvent& e) {
    const int note = noteAtPosition(e.position);
    if (note == activeNote_) return;

    if (activeNote_ >= 0) {
        pressed_[static_cast<size_t>(activeNote_)] = false;
        if (onNoteOff) onNoteOff(activeNote_);
    }
    if (note >= 0 && note <= 127) {
        pressed_[static_cast<size_t>(note)] = true;
        if (onNoteOn) onNoteOn(note, 0.8f);
    }
    activeNote_ = note;
    repaint();
}

void Y2KPianoKeyboard::highlightNote(int midiNote, bool on) {
    if (midiNote >= 0 && midiNote < 128) {
        pressed_[static_cast<size_t>(midiNote)] = on;
        juce::MessageManager::callAsync([this] { repaint(); });
    }
}

// ═════════════════════════════════════════════════════════════════
//  Y2KKontrolPanel
// ═════════════════════════════════════════════════════════════════

const char* Y2KKontrolPanel::knobName(int i) noexcept {
    static const char* names[] = {
        "Cutoff", "Resonance", "Attack", "Release",
        "FX Send", "Pan", "Volume", "Pitch" };
    return (i >= 0 && i < kNumKnobs) ? names[i] : "";
}

juce::Colour Y2KKontrolPanel::knobAccent(int i) noexcept {
    return Color::forTrackIndex(i);
}

Y2KKontrolPanel::Y2KKontrolPanel() {
    for (int i = 0; i < kNumKnobs; ++i) {
        auto knob = std::make_unique<Y2KKnob>(knobName(i), knobAccent(i));

        knob->onChange = [this, i](float v) {
            if (onKnobChanged) onKnobChanged(i, v);
        };
        knob->onRightClick = [this, i] {
            if (onKnobRightClicked) onKnobRightClicked(i);
        };
        addAndMakeVisible(*knob);
        knobs_[i] = std::move(knob);
    }

    keyboard_.onNoteOn  = [this](int n, float v) { if (onNoteOn)  onNoteOn(n, v);  };
    keyboard_.onNoteOff = [this](int n)           { if (onNoteOff) onNoteOff(n);    };
    addAndMakeVisible(keyboard_);

    setSize(kMinWidth, kPanelH);
}

void Y2KKontrolPanel::setKnobValue(int idx, float v01) {
    if (idx >= 0 && idx < kNumKnobs) {
        juce::MessageManager::callAsync([this, idx, v01] {
            knobs_[idx]->setValue(v01);
        });
    }
}

void Y2KKontrolPanel::paint(juce::Graphics& g) {
    // Platinum panel background
    g.setGradientFill(juce::ColourGradient(
        juce::Colour(0xFFEFEFEB), 0.f, 0.f,
        juce::Colour(0xFFDFDFDB), 0.f, static_cast<float>(getHeight()), false));
    g.fillAll();

    // Top accent line
    g.setColour(juce::Colour(0xFF0071E3).withAlpha(0.4f));
    g.drawHorizontalLine(0, 0.f, static_cast<float>(getWidth()));

    // Section separator between knobs and keyboard
    g.setColour(juce::Colour(0xFFB8B8B4));
    g.drawHorizontalLine(kKnobRowH + 2, 0.f, static_cast<float>(getWidth()));

    // Panel label
    g.setFont(juce::Font(9.f, juce::Font::bold));
    g.setColour(juce::Colour(0xFF888884));
    g.drawText("KONTROL", getLocalBounds().withHeight(12).translated(8, 2),
               juce::Justification::centredLeft);
}

void Y2KKontrolPanel::resized() {
    auto r = getLocalBounds();

    // Knob row
    auto knobRow = r.removeFromTop(kKnobRowH).reduced(4, 6);
    const int knobW = knobRow.getWidth() / kNumKnobs;
    for (int i = 0; i < kNumKnobs; ++i)
        knobs_[i]->setBounds(knobRow.removeFromLeft(knobW).reduced(2));

    r.removeFromTop(4);  // separator gap

    // Keyboard
    keyboard_.setBounds(r.reduced(4, 2));
}

} // namespace y2k::ui

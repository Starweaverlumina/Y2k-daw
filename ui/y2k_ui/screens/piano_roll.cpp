#include "piano_roll.h"
#include "../theme.h"

#include <cmath>
#include <algorithm>

namespace y2k::ui {

// ═══════════════════════════════════════════════════════════════════
//  Palette constants
// ═══════════════════════════════════════════════════════════════════
namespace {

const juce::Colour kBgDark       { 0xFF1A1A1A };   // overall background
const juce::Colour kWhiteKey     { 0xFFF5F5F0 };   // white piano key
const juce::Colour kBlackKey     { 0xFF1C1C1C };   // black piano key
const juce::Colour kKeyBorder    { 0xFFAAAAAA };
const juce::Colour kRowWhite     { 0xFF252525 };   // grid row for white key
const juce::Colour kRowBlack     { 0xFF1A1A1A };   // grid row for black key
const juce::Colour kRowOctave    { 0xFF2D2D2D };   // C note row accent
const juce::Colour kGridLine     { 0xFF3A3A3A };   // horizontal semitone line
const juce::Colour kBeatLine     { 0xFF454545 };   // vertical beat line
const juce::Colour kBarLine      { 0xFF666666 };   // vertical bar (4-beat) line
const juce::Colour kNoteOutline  { 0x40000000 };
const juce::Colour kGhostNote    { 0x80FFFFFF };
const juce::Colour kPlayhead     { 0xFFFF2D55 };
const juce::Colour kTextWhiteKey { 0xFF888884 };

// Semitone-within-octave lookup for black keys: C=0,C#=1,...,B=11
inline bool semitoneIsBlack(int s) noexcept {
    return s == 1 || s == 3 || s == 6 || s == 8 || s == 10;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════
//  Constructor
// ═══════════════════════════════════════════════════════════════════

PianoRoll::PianoRoll() {
    setOpaque(true);
    setWantsKeyboardFocus(true);
}

// ═══════════════════════════════════════════════════════════════════
//  Data binding
// ═══════════════════════════════════════════════════════════════════

void PianoRoll::loadClip(y2k::engine::MidiClip* clip,
                          y2k::engine::SessionCommandBus* bus) {
    clip_ = clip;
    bus_  = bus;
    repaint();
}

// ═══════════════════════════════════════════════════════════════════
//  Coordinate helpers
// ═══════════════════════════════════════════════════════════════════

juce::Rectangle<int> PianoRoll::gridArea() const noexcept {
    return { kKeyW, 0, getWidth() - kKeyW, getHeight() };
}

float PianoRoll::beatToX(double beat) const noexcept {
    return static_cast<float>(kKeyW + (beat - scrollBeat_) * pixelsPerBeat_);
}

double PianoRoll::xToBeat(float x) const noexcept {
    return scrollBeat_ + static_cast<double>(x - kKeyW) / pixelsPerBeat_;
}

// Pitch -> top-of-row y.  scrollPitch_ is the MIDI pitch at y=0.
int PianoRoll::pitchToY(int pitch) const noexcept {
    return (scrollPitch_ - pitch) * kRowH;
}

int PianoRoll::yToPitch(int y) const noexcept {
    return scrollPitch_ - y / kRowH;
}

bool PianoRoll::isBlackKey(int pitch) const noexcept {
    return semitoneIsBlack(pitch % 12);
}

juce::String PianoRoll::pitchName(int pitch) const {
    static const char* noteNames[] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    const int oct = pitch / 12 - 1;
    return juce::String(noteNames[pitch % 12]) + juce::String(oct);
}

double PianoRoll::quantise(double beat) const noexcept {
    const int div = (clip_ && clip_->gridDivision > 0) ? clip_->gridDivision : 16;
    const double grid = 1.0 / static_cast<double>(div) * 4.0; // beats per grid step (div=16 → 0.25 beats)
    return std::round(beat / grid) * grid;
}

int PianoRoll::noteAtPoint(int px, int py) const {
    if (!clip_) return -1;
    const int pitch = yToPitch(py);
    const double beat = xToBeat(static_cast<float>(px));

    for (int i = 0; i < static_cast<int>(clip_->notes.size()); ++i) {
        const auto& n = clip_->notes[i];
        if (n.pitch != pitch) continue;
        if (beat >= n.startBeat && beat < n.startBeat + n.durationBeats)
            return i;
    }
    return -1;
}

// ═══════════════════════════════════════════════════════════════════
//  Draw helpers
// ═══════════════════════════════════════════════════════════════════

void PianoRoll::drawPianoKeys(juce::Graphics& g) const {
    const int h = getHeight();

    g.setColour(juce::Colour { 0xFF111111 });
    g.fillRect(0, 0, kKeyW, h);

    // Iterate visible pitches
    const int topPitch    = yToPitch(0);
    const int bottomPitch = yToPitch(h) - 1;

    // Draw white keys first so black keys overdraw them
    for (int p = topPitch; p >= bottomPitch; --p) {
        if (p < 0 || p > 127) continue;
        if (isBlackKey(p))    continue;

        const int y  = pitchToY(p);
        const int y2 = pitchToY(p - 1); // bottom of this row
        const juce::Rectangle<int> r { 0, y, kKeyW - 1, y2 - y - 1 };

        g.setColour(kWhiteKey);
        g.fillRect(r);
        g.setColour(kKeyBorder);
        g.drawRect(r);

        // Label C notes
        if (p % 12 == 0) {
            g.setFont(Type::chicago(8.f, true));
            g.setColour(juce::Colour(Color::BondiBlue));
            g.drawText(pitchName(p), 2, y, kKeyW - 4, kRowH,
                       juce::Justification::centredLeft, true);
        }
    }

    // Black keys
    for (int p = topPitch; p >= bottomPitch; --p) {
        if (p < 0 || p > 127) continue;
        if (!isBlackKey(p))   continue;

        const int y = pitchToY(p);
        const juce::Rectangle<int> r { 0, y + 1, kKeyW * 2 / 3, kRowH - 2 };

        g.setColour(kBlackKey);
        g.fillRect(r);
        g.setColour(juce::Colour { 0xFF444444 });
        g.drawRect(r);
    }

    // Right-edge separator
    g.setColour(kKeyBorder);
    g.drawVerticalLine(kKeyW - 1, 0.f, static_cast<float>(h));
}

void PianoRoll::drawGrid(juce::Graphics& g) const {
    const auto ga = gridArea();
    const int  h  = getHeight();

    // ── Horizontal row backgrounds ────────────────────────────────
    const int topPitch    = yToPitch(0);
    const int bottomPitch = yToPitch(h) - 1;

    for (int p = topPitch; p >= bottomPitch; --p) {
        if (p < 0 || p > 127) continue;
        const int y  = pitchToY(p);
        const int y2 = pitchToY(p - 1);

        if (p % 12 == 0)
            g.setColour(kRowOctave);
        else if (isBlackKey(p))
            g.setColour(kRowBlack);
        else
            g.setColour(kRowWhite);

        g.fillRect(kKeyW, y, ga.getWidth(), y2 - y);
    }

    // ── Horizontal semitone lines ─────────────────────────────────
    g.setColour(kGridLine);
    for (int p = topPitch; p >= bottomPitch; --p) {
        const int y = pitchToY(p);
        if (y < 0 || y > h) continue;
        g.drawHorizontalLine(y, static_cast<float>(kKeyW),
                             static_cast<float>(getWidth()));
    }

    // ── Vertical beat / bar lines ─────────────────────────────────
    // Grid division (1/gridDiv quarter beats) — minimum one beat
    const int div  = (clip_ && clip_->gridDivision > 0) ? clip_->gridDivision : 16;
    // We draw lines at every 1/div of a whole bar (4 beats).
    // A "grid step" in beats:
    const double stepBeats = 4.0 / static_cast<double>(div);

    const double firstBeat = std::floor(scrollBeat_ / stepBeats) * stepBeats;
    const double lastBeat  = scrollBeat_ + static_cast<double>(ga.getWidth()) / pixelsPerBeat_ + stepBeats;

    for (double beat = firstBeat; beat <= lastBeat; beat += stepBeats) {
        const float x = beatToX(beat);
        if (x < static_cast<float>(kKeyW)) continue;
        if (x > static_cast<float>(getWidth())) break;

        const bool isBar  = (std::fmod(beat, 4.0) < 1e-9 || std::fmod(beat, 4.0) > 4.0 - 1e-9);
        const bool isBeat = (std::fmod(beat, 1.0) < 1e-9 || std::fmod(beat, 1.0) > 1.0 - 1e-9);

        if (isBar) {
            g.setColour(kBarLine);
        } else if (isBeat) {
            g.setColour(kBeatLine);
        } else {
            g.setColour(kGridLine.withAlpha(0.6f));
        }

        g.drawVerticalLine(static_cast<int>(x), 0.f, static_cast<float>(h));
    }
}

void PianoRoll::drawNotes(juce::Graphics& g) const {
    if (!clip_) return;

    const auto ga = gridArea();

    for (const auto& note : clip_->notes) {
        const float x = beatToX(note.startBeat);
        const float w = static_cast<float>(note.durationBeats) * pixelsPerBeat_ - 1.f;
        const int   y = pitchToY(note.pitch);

        if (x + w < static_cast<float>(kKeyW)) continue;
        if (x > static_cast<float>(getWidth())) break;

        const juce::Rectangle<float> r { x, static_cast<float>(y) + 1.f,
                                          std::max(w, 2.f), static_cast<float>(kRowH) - 2.f };

        // Use BondiBlue as the default note color (track colour tinting left for future)
        const juce::Colour noteCol = Color::forTrackIndex(0);

        // Velocity-shaded fill
        const float velAlpha = 0.5f + 0.5f * note.velocity;
        g.setColour(noteCol.withAlpha(velAlpha));
        g.fillRoundedRectangle(r, 2.f);

        // Lighter top edge highlight
        g.setColour(juce::Colours::white.withAlpha(0.25f));
        g.fillRoundedRectangle(r.withHeight(r.getHeight() * 0.35f), 2.f);

        // Outline
        g.setColour(kNoteOutline);
        g.drawRoundedRectangle(r, 2.f, 0.7f);
    }
}

void PianoRoll::drawGhostNote(juce::Graphics& g) const {
    if (!dragging_ || dragPitch_ < 0 || dragNoteIdx_ >= 0) return;

    const double start = std::min(dragStartBeat_, dragEndBeat_);
    const double dur   = std::max(std::abs(dragEndBeat_ - dragStartBeat_), quantise(0.25));

    const float x = beatToX(start);
    const float w = static_cast<float>(dur) * pixelsPerBeat_ - 1.f;
    const int   y = pitchToY(dragPitch_);

    const juce::Rectangle<float> r { x, static_cast<float>(y) + 1.f,
                                      std::max(w, 2.f), static_cast<float>(kRowH) - 2.f };
    g.setColour(kGhostNote);
    g.fillRoundedRectangle(r, 2.f);
    g.setColour(juce::Colours::white.withAlpha(0.6f));
    g.drawRoundedRectangle(r, 2.f, 1.f);
}

void PianoRoll::drawPlayhead(juce::Graphics& g) const {
    // Playhead position is not currently maintained in the PianoRoll itself;
    // it would be fed from the engine. Stub for future use.
    (void)g;
}

// ═══════════════════════════════════════════════════════════════════
//  paint / resized
// ═══════════════════════════════════════════════════════════════════

void PianoRoll::paint(juce::Graphics& g) {
    g.fillAll(kBgDark);
    drawGrid(g);
    drawNotes(g);
    drawGhostNote(g);
    drawPianoKeys(g);
    drawPlayhead(g);

    // Selection rectangle (Select mode)
    if (selectDragging_ && !selectionRect_.isEmpty()) {
        g.setColour(juce::Colour(Color::BondiBlue).withAlpha(0.25f));
        g.fillRect(selectionRect_);
        g.setColour(juce::Colour(Color::BondiBlue).withAlpha(0.75f));
        g.drawRect(selectionRect_, 1);
    }
}

void PianoRoll::resized() {}

// ═══════════════════════════════════════════════════════════════════
//  Mouse interaction
// ═══════════════════════════════════════════════════════════════════

void PianoRoll::mouseDown(const juce::MouseEvent& e) {
    const int px = e.x;
    const int py = e.y;

    // Ignore clicks in the piano key strip
    if (px < kKeyW) return;

    if (mode_ == EditMode::Draw) {
        const int    pitch = yToPitch(py);
        const double beat  = quantise(xToBeat(static_cast<float>(px)));

        // Check if clicking an existing note — if so, prepare to move/resize it
        const int idx = noteAtPoint(px, py);
        if (idx >= 0) {
            dragNoteIdx_   = idx;
            dragPitch_     = clip_->notes[idx].pitch;
            dragStartBeat_ = clip_->notes[idx].startBeat;
            dragEndBeat_   = dragStartBeat_ + clip_->notes[idx].durationBeats;
        } else {
            dragNoteIdx_   = -1;
            dragPitch_     = juce::jlimit(0, 127, pitch);
            dragStartBeat_ = beat;
            dragEndBeat_   = beat + quantise(0.25);
        }
        dragging_ = true;
        repaint();

    } else if (mode_ == EditMode::Erase) {
        if (!clip_) return;
        const int idx = noteAtPoint(px, py);
        if (idx >= 0) {
            clip_->notes.erase(clip_->notes.begin() + idx);
            if (onClipChanged) onClipChanged();
            repaint();
        }

    } else if (mode_ == EditMode::Select) {
        selectDragging_ = true;
        selectionRect_  = juce::Rectangle<int>(e.x, e.y, 0, 0);
        repaint();
    }
}

void PianoRoll::mouseDrag(const juce::MouseEvent& e) {
    if (mode_ == EditMode::Draw && dragging_) {
        const double rawBeat = xToBeat(static_cast<float>(e.x));
        dragEndBeat_ = quantise(rawBeat);

        // Keep minimum one grid step duration
        const double minDur = quantise(0.25);
        if (dragEndBeat_ <= dragStartBeat_)
            dragEndBeat_ = dragStartBeat_ + minDur;

        repaint();

    } else if (mode_ == EditMode::Select && selectDragging_) {
        const int x1 = std::min(e.getMouseDownX(), e.x);
        const int y1 = std::min(e.getMouseDownY(), e.y);
        const int x2 = std::max(e.getMouseDownX(), e.x);
        const int y2 = std::max(e.getMouseDownY(), e.y);
        selectionRect_ = juce::Rectangle<int>(x1, y1, x2 - x1, y2 - y1);
        repaint();
    }
}

void PianoRoll::mouseUp(const juce::MouseEvent& /*e*/) {
    if (mode_ == EditMode::Draw && dragging_) {
        dragging_ = false;

        if (!clip_) return;

        const double start = std::min(dragStartBeat_, dragEndBeat_);
        const double dur   = std::max(std::abs(dragEndBeat_ - dragStartBeat_),
                                      quantise(0.25));

        if (dragNoteIdx_ >= 0 && dragNoteIdx_ < static_cast<int>(clip_->notes.size())) {
            // Modify existing note's duration
            auto& n = clip_->notes[static_cast<size_t>(dragNoteIdx_)];
            n.startBeat    = start;
            n.durationBeats = dur;
        } else {
            // Commit new note
            y2k::engine::MidiClip::Note n;
            n.pitch         = juce::jlimit(0, 127, dragPitch_);
            n.velocity      = 0.8f;
            n.startBeat     = start;
            n.durationBeats = dur;
            clip_->notes.push_back(n);
        }

        dragNoteIdx_ = -1;
        if (onClipChanged) onClipChanged();
        repaint();

    } else if (mode_ == EditMode::Select) {
        selectDragging_ = false;
        // Selection rectangle stays visible until next click
        repaint();
    }
}

void PianoRoll::mouseWheelMove(const juce::MouseEvent& e,
                                const juce::MouseWheelDetails& w) {
    if (e.mods.isCommandDown()) {
        // Zoom
        setPixelsPerBeat(juce::jlimit(10.f, 400.f,
                                       pixelsPerBeat_ * (1.f + w.deltaY * 0.2f)));
    } else if (e.mods.isAltDown()) {
        // Vertical scroll
        setScrollPitch(juce::jlimit(21, 108,
                                     scrollPitch_ + static_cast<int>(w.deltaY * 3.f)));
    } else {
        // Horizontal scroll
        setScrollBeat(std::max(0.0, scrollBeat_ - static_cast<double>(w.deltaX) * 2.0));
    }
}

} // namespace y2k::ui

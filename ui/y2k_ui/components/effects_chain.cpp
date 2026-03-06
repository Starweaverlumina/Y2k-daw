#include "effects_chain.h"
#include "../theme.h"

#include <algorithm>

namespace y2k::ui {

// ═══════════════════════════════════════════════════════════════════
//  Palette constants
// ═══════════════════════════════════════════════════════════════════
namespace {

const juce::Colour kBg          { 0xFFE8E8E4 };
const juce::Colour kRowBg       { 0xFFF2F2EE };
const juce::Colour kRowBgHover  { 0xFFE0E8F0 };
const juce::Colour kRowBgBypass { 0xFFF0F0F0 };
const juce::Colour kBorder      { 0xFFCCCCCC };
const juce::Colour kTextDark    { 0xFF1A1A1A };
const juce::Colour kTextDim     { 0xFF888884 };
const juce::Colour kBypassOn    { 0xFFFF9500 };   // orange = active bypass
const juce::Colour kBypassOff   { 0xFFDDDDDD };
const juce::Colour kRemove      { 0xFFFF3B30 };

// Lookup a friendly display name for a typeId
juce::String displayName(const juce::String& typeId) {
    if (typeId == "GrapeComp")   return "Grape Compressor";
    if (typeId == "BondiSynth")  return "Bondi Synth";
    if (typeId == "SineOsc")     return "Sine Oscillator";
    if (typeId == "LimePEQ")     return "Lime Parametric EQ";
    if (typeId == "TangDelay")   return "Tangerine Delay";
    return typeId;  // fallback: show raw id
}

} // namespace

// ═══════════════════════════════════════════════════════════════════
//  InsertSlotRow
// ═══════════════════════════════════════════════════════════════════

InsertSlotRow::InsertSlotRow(y2k::engine::InsertSlot& slot)
    : slot_(slot)
{
    // Name label — left section, acts as a click target
    nameLabel_.setText(displayName(slot_.typeId), juce::dontSendNotification);
    nameLabel_.setFont(Type::chicago(Type::Small));
    nameLabel_.setColour(juce::Label::textColourId, kTextDark);
    nameLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(nameLabel_);

    // Bypass button
    bypassBtn_.setClickingTogglesState(false);
    bypassBtn_.setColour(juce::TextButton::buttonColourId,
                          slot_.bypassed ? kBypassOn : kBypassOff);
    bypassBtn_.setColour(juce::TextButton::textColourOffId, kTextDark);
    bypassBtn_.onClick = [this] {
        slot_.bypassed = !slot_.bypassed;
        bypassBtn_.setColour(juce::TextButton::buttonColourId,
                              slot_.bypassed ? kBypassOn : kBypassOff);
        repaint();
        if (onBypassToggled) onBypassToggled();
    };
    addAndMakeVisible(bypassBtn_);

    // Remove button
    removeBtn_.setColour(juce::TextButton::buttonColourId,
                          juce::Colour(0xFFEEEEEE));
    removeBtn_.setColour(juce::TextButton::textColourOffId, kRemove);
    removeBtn_.onClick = [this] {
        if (onRemoveClicked) onRemoveClicked();
    };
    addAndMakeVisible(removeBtn_);
}

void InsertSlotRow::resized() {
    const int w   = getWidth();
    const int h   = getHeight();
    const int pad = kBtnPad;

    // Buttons on the right
    const int btnY = (h - kBtnW) / 2;
    removeBtn_.setBounds(w - kBtnW - pad, btnY, kBtnW, kBtnW);
    bypassBtn_.setBounds(w - (kBtnW + pad) * 2, btnY, kBtnW, kBtnW);

    // Label fills the rest
    nameLabel_.setBounds(pad + 8, 0, bypassBtn_.getX() - pad - 12, h);
}

void InsertSlotRow::paint(juce::Graphics& g) {
    const auto r = getLocalBounds().toFloat();

    if (slot_.bypassed)
        g.setColour(kRowBgBypass);
    else if (hovered_)
        g.setColour(kRowBgHover);
    else
        g.setColour(kRowBg);

    g.fillRoundedRectangle(r, 4.f);

    // Left accent stripe — color encodes bypass state
    g.setColour(slot_.bypassed
                    ? juce::Colour(Color::Tangerine).withAlpha(0.7f)
                    : juce::Colour(Color::BondiBlue).withAlpha(0.85f));
    g.fillRoundedRectangle(0.f, 2.f, 4.f, r.getHeight() - 4.f, 2.f);

    // Bottom border
    g.setColour(kBorder);
    g.drawHorizontalLine(getHeight() - 1, 0.f, r.getWidth());

    // Bypassed text dim
    nameLabel_.setColour(juce::Label::textColourId,
                          slot_.bypassed ? kTextDim : kTextDark);
}

void InsertSlotRow::mouseDown(const juce::MouseEvent& e) {
    // If the click is not on one of the buttons, fire onSelected
    if (!bypassBtn_.getBounds().contains(e.getPosition()) &&
        !removeBtn_.getBounds().contains(e.getPosition()))
    {
        if (onSelected) onSelected();
    }
}

// ═══════════════════════════════════════════════════════════════════
//  EffectsChain
// ═══════════════════════════════════════════════════════════════════

EffectsChain::EffectsChain() {
    addBtn_.setColour(juce::TextButton::buttonColourId,
                       juce::Colour(Color::BondiBlue).withAlpha(0.15f));
    addBtn_.setColour(juce::TextButton::textColourOffId,
                       juce::Colour(Color::BondiBlue));
    addBtn_.onClick = [this] {
        if (!track_ || trackIdx_ < 0) return;
        // Default insert type — in a real app a popup menu would appear here
        const juce::String typeId { "GrapeComp" };
        if (onAddInsert) onAddInsert(trackIdx_, typeId);
    };
    addAndMakeVisible(addBtn_);
}

void EffectsChain::setTrack(y2k::engine::TrackModel* track, int trackIdx) {
    track_    = track;
    trackIdx_ = trackIdx;
    rebuild();
}

void EffectsChain::rebuild() {
    rows_.clear();

    if (!track_) {
        resized();
        repaint();
        return;
    }

    for (int i = 0; i < static_cast<int>(track_->inserts.size()); ++i) {
        auto* row = rows_.add(new InsertSlotRow(track_->inserts[static_cast<size_t>(i)]));
        addAndMakeVisible(row);

        const int capturedIdx = i;

        row->onBypassToggled = [this, capturedIdx] {
            if (!track_ || capturedIdx >= static_cast<int>(track_->inserts.size())) return;
            const bool bypassed = track_->inserts[static_cast<size_t>(capturedIdx)].bypassed;
            if (onBypassInsert) onBypassInsert(trackIdx_, capturedIdx, bypassed);
        };

        row->onRemoveClicked = [this, capturedIdx] {
            if (!track_) return;
            if (onRemoveInsert) onRemoveInsert(trackIdx_, capturedIdx);
        };

        row->onSelected = [this, capturedIdx] {
            // Future: open parameter panel for this slot
            (void)capturedIdx;
        };
    }

    resized();
    repaint();
}

void EffectsChain::paint(juce::Graphics& g) {
    g.fillAll(kBg);
    g.setColour(kBorder);
    g.drawRect(getLocalBounds());
}

void EffectsChain::resized() {
    const int pad = 4;
    int y = pad;
    const int rowH = InsertSlotRow::kRowH;
    const int w    = getWidth() - pad * 2;

    for (auto* row : rows_) {
        row->setBounds(pad, y, w, rowH);
        y += rowH + 2;
    }

    // Add button at bottom
    addBtn_.setBounds(pad, y, w, rowH);
    y += rowH + pad;

    // Ensure the component height matches its content
    if (getParentComponent() != nullptr)
        setSize(getWidth(), y);
}

} // namespace y2k::ui

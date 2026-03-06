#pragma once

// ═══════════════════════════════════════════════════════════════════
//  Y2K UI — EffectsChain
//  Shows a vertical list of insert slots for a single track.
//
//  Layout (each InsertSlotRow, 32 px tall):
//    ┌─[name label──────────────]─[B]─[X]─┐
//    │  GrapeCompressor                   │  ← slot 0
//    ├────────────────────────────────────┤
//    │  BondiSynth                        │  ← slot 1
//    ├────────────────────────────────────┤
//    │  + Add Insert                      │  ← add button
//    └────────────────────────────────────┘
//
//  [B] = Bypass toggle    [X] = Remove
//  Clicking the name label fires onSelected.
// ═══════════════════════════════════════════════════════════════════

#include <juce_gui_basics/juce_gui_basics.h>
#include "../../../engine/y2k_engine/mixer/track_model.h"
#include <functional>

namespace y2k::ui {

// ─────────────────────────────────────────────────────────────────
//  InsertSlotRow — one row in the insert chain list
// ─────────────────────────────────────────────────────────────────
class InsertSlotRow final : public juce::Component {
public:
    static constexpr int kRowH    = 32;
    static constexpr int kBtnW    = 22;
    static constexpr int kBtnPad  =  4;

    explicit InsertSlotRow(y2k::engine::InsertSlot& slot);

    void paint  (juce::Graphics& g) override;
    void resized()                   override;

    // Callbacks — set by EffectsChain
    std::function<void()> onBypassToggled;
    std::function<void()> onRemoveClicked;
    std::function<void()> onSelected;

private:
    y2k::engine::InsertSlot& slot_;
    juce::TextButton bypassBtn_ { "B" };
    juce::TextButton removeBtn_ { "X" };
    juce::Label      nameLabel_;

    bool hovered_ = false;

    void mouseEnter(const juce::MouseEvent&) override { hovered_ = true;  repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hovered_ = false; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InsertSlotRow)
};

// ─────────────────────────────────────────────────────────────────
//  EffectsChain — vertical container of InsertSlotRows
// ─────────────────────────────────────────────────────────────────
class EffectsChain final : public juce::Component {
public:
    EffectsChain();

    /// Bind to a track. Rebuilds the row list immediately.
    void setTrack(y2k::engine::TrackModel* track, int trackIdx);

    void paint  (juce::Graphics& g) override;
    void resized()                   override;

    // ── Callbacks ─────────────────────────────────────────────────
    /// Called when the user clicks "+ Add Insert". Supply a typeId
    /// (e.g. "GrapeComp") to the command bus from outside.
    std::function<void(int trackIdx, const juce::String& typeId)>  onAddInsert;

    /// Called when the user clicks [X] on a slot.
    std::function<void(int trackIdx, int insertIdx)>               onRemoveInsert;

    /// Called when the user clicks [B] on a slot.
    std::function<void(int trackIdx, int insertIdx, bool bypassed)> onBypassInsert;

private:
    void rebuild();

    y2k::engine::TrackModel* track_    = nullptr;
    int                      trackIdx_ = -1;

    juce::OwnedArray<InsertSlotRow> rows_;
    juce::TextButton                addBtn_ { "+ Add Insert" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectsChain)
};

} // namespace y2k::ui

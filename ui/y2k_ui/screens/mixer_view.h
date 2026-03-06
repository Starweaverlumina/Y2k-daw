#pragma once

// ═══════════════════════════════════════════════════════════════════
//  Y2K UI — MixerView
//  Horizontal strip of ChannelStrips (one per track + master).
//
//  Layout (per strip, 72 px wide):
//   ┌───────┐
//   │ name  │
//   │  [M]  │  ← Mute
//   │  [S]  │  ← Solo
//   │ ████  │  ← VU meter bar
//   │  │ │  │  ← Fader (LinearVertical Slider)
//   │ Pan ◄►│  ← Pan rotary
//   └───────┘
// ═══════════════════════════════════════════════════════════════════

#include <juce_gui_basics/juce_gui_basics.h>
#include "../../../engine/y2k_engine/mixer/track_model.h"
#include "../../../engine/y2k_engine/session/session_command_bus.h"
#include <vector>
#include <functional>

namespace y2k::ui {

// ─────────────────────────────────────────────────────────────────
//  ChannelStrip — single fader + meter column
// ─────────────────────────────────────────────────────────────────
class ChannelStrip final : public juce::Component,
                           public juce::Slider::Listener {
public:
    static constexpr int kStripW   = 72;
    static constexpr int kFaderH   = 120;
    static constexpr int kMeterW   = 10;
    static constexpr int kKnobSz   = 36;
    static constexpr int kBtnH     = 20;

    explicit ChannelStrip(bool isMaster = false);

    // Bind to engine objects. Both pointers are non-owning.
    void setTrack(y2k::engine::TrackModel*         track,
                  y2k::engine::SessionCommandBus*  bus);

    // Drive the VU meter from the audio thread meter callback (UI thread only).
    void setVULevel(float level01)  { vuLevel_ = juce::jlimit(0.f, 1.f, level01); repaint(); }

    // Directly set fader position (used for Master strip).
    void setMasterVolume(float vol) { fader_.setValue(static_cast<double>(vol),
                                                       juce::dontSendNotification); }

    // juce::Component overrides
    void paint  (juce::Graphics& g) override;
    void resized()                   override;

    // juce::Slider::Listener
    void sliderValueChanged(juce::Slider* s) override;

private:
    const bool isMaster_;

    y2k::engine::TrackModel*         track_ = nullptr;
    y2k::engine::SessionCommandBus*  bus_   = nullptr;

    juce::Slider    fader_ { juce::Slider::LinearVertical,
                              juce::Slider::NoTextBox };
    juce::Slider    pan_   { juce::Slider::RotaryHorizontalVerticalDrag,
                              juce::Slider::NoTextBox };
    juce::TextButton muteBtn_ { "M" };
    juce::TextButton soloBtn_ { "S" };
    juce::Label      nameLabel_;

    float vuLevel_   = 0.f;
    bool  muteState_ = false;
    bool  soloState_ = false;

    void onMuteClicked();
    void onSoloClicked();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelStrip)
};

// ─────────────────────────────────────────────────────────────────
//  MixerView — scrollable container of ChannelStrips
// ─────────────────────────────────────────────────────────────────
class MixerView final : public juce::Component {
public:
    MixerView();

    /// Rebuild all strips from the ProjectRouting. Call on UI thread only.
    void setRouting(y2k::engine::ProjectRouting*    routing,
                    y2k::engine::SessionCommandBus* bus);

    /// Push fresh VU levels for each track (index-matched to routing->tracks).
    void updateVULevels(const std::vector<float>& levels);

    void paint  (juce::Graphics& g) override;
    void resized()                   override;

private:
    void rebuild();

    y2k::engine::ProjectRouting*    routing_ = nullptr;
    y2k::engine::SessionCommandBus* bus_     = nullptr;

    juce::OwnedArray<ChannelStrip> strips_;
    ChannelStrip                   masterStrip_ { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerView)
};

} // namespace y2k::ui

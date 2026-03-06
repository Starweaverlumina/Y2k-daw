#include "mixer_view.h"
#include "../theme.h"

#include <cmath>
#include <algorithm>

namespace y2k::ui {

// ═══════════════════════════════════════════════════════════════════
//  Palette constants
// ═══════════════════════════════════════════════════════════════════
namespace {

const juce::Colour kBg          { 0xFF1E1E1E };
const juce::Colour kStripBg     { 0xFF2A2A2A };
const juce::Colour kStripBorder { 0xFF3A3A3A };
const juce::Colour kMasterBg    { 0xFF222233 };
const juce::Colour kNameText    { 0xFFDDDDDD };
const juce::Colour kDimText     { 0xFF888884 };
const juce::Colour kMuteOn      { 0xFFFF9500 };
const juce::Colour kSoloOn      { 0xFF4CD964 };
const juce::Colour kBtnOff      { 0xFF3A3A3A };
const juce::Colour kBtnText     { 0xFFCCCCCC };

// VU meter colors
inline juce::Colour vuColor(float seg01) {
    if (seg01 > 0.917f) return juce::Colour(Color::ClipPeak);   // top ~2 segments
    if (seg01 > 0.750f) return juce::Colour(Color::ClipLevel);  // amber zone
    return juce::Colour(Color::SafeGreen);
}

/// Draw a segmented VU meter bar inside 'bounds'.
void drawVUBar(juce::Graphics& g, juce::Rectangle<int> bounds, float level01) {
    const int numSegs  = Geom::VUNumSegments;
    const int segH     = Geom::VUSegmentH;
    const int segGap   = Geom::VUSegmentGap;
    const int totalH   = numSegs * (segH + segGap);

    int y = bounds.getBottom() - segH;
    for (int s = 0; s < numSegs; ++s) {
        const float seg01   = static_cast<float>(s) / static_cast<float>(numSegs - 1);
        const float thresh  = static_cast<float>(s) / static_cast<float>(numSegs);
        const bool  lit     = (level01 >= thresh);

        g.setColour(lit ? vuColor(seg01) : juce::Colour(0xFF2E2E2E));
        g.fillRect(bounds.getX(), y, bounds.getWidth(), segH);

        y -= (segH + segGap);
        if (y < bounds.getY()) break;
    }
}

/// Style a toggle button (mute / solo).
void styleToggleButton(juce::TextButton& btn, juce::Colour onColor, bool isOn) {
    btn.setColour(juce::TextButton::buttonColourId,      isOn ? onColor  : kBtnOff);
    btn.setColour(juce::TextButton::buttonOnColourId,    onColor);
    btn.setColour(juce::TextButton::textColourOffId,     isOn ? juce::Colours::white : kBtnText);
    btn.setColour(juce::TextButton::textColourOnId,      juce::Colours::white);
}

} // namespace

// ═══════════════════════════════════════════════════════════════════
//  ChannelStrip
// ═══════════════════════════════════════════════════════════════════

ChannelStrip::ChannelStrip(bool isMaster)
    : isMaster_(isMaster)
{
    // ── Fader ────────────────────────────────────────────────────
    fader_.setRange(0.0, 1.0, 0.001);
    fader_.setValue(0.8, juce::dontSendNotification);
    fader_.addListener(this);
    addAndMakeVisible(fader_);

    // ── Pan knob ─────────────────────────────────────────────────
    pan_.setRange(-1.0, 1.0, 0.01);
    pan_.setValue(0.0, juce::dontSendNotification);
    pan_.addListener(this);
    addAndMakeVisible(pan_);

    // ── Mute button ──────────────────────────────────────────────
    styleToggleButton(muteBtn_, kMuteOn, false);
    muteBtn_.onClick = [this] { onMuteClicked(); };
    addAndMakeVisible(muteBtn_);

    // ── Solo button ──────────────────────────────────────────────
    styleToggleButton(soloBtn_, kSoloOn, false);
    soloBtn_.onClick = [this] { onSoloClicked(); };
    if (!isMaster_)
        addAndMakeVisible(soloBtn_);

    // ── Name label ───────────────────────────────────────────────
    nameLabel_.setFont(Type::chicago(Type::Micro));
    nameLabel_.setJustificationType(juce::Justification::centred);
    nameLabel_.setColour(juce::Label::textColourId, kNameText);
    nameLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(nameLabel_);

    if (isMaster_)
        nameLabel_.setText("Master", juce::dontSendNotification);
}

void ChannelStrip::setTrack(y2k::engine::TrackModel*        track,
                             y2k::engine::SessionCommandBus* bus) {
    track_ = track;
    bus_   = bus;

    if (!track_) return;

    muteState_ = track_->muted;
    soloState_ = track_->soloed;

    nameLabel_.setText(track_->name, juce::dontSendNotification);
    fader_.setValue(static_cast<double>(track_->volume), juce::dontSendNotification);
    pan_.setValue(static_cast<double>(track_->pan), juce::dontSendNotification);

    styleToggleButton(muteBtn_, kMuteOn, muteState_);
    styleToggleButton(soloBtn_, kSoloOn, soloState_);

    repaint();
}

// ── Layout ─────────────────────────────────────────────────────────

void ChannelStrip::resized() {
    const int w   = getWidth();
    const int h   = getHeight();
    const int pad = 4;

    // Name label at top
    nameLabel_.setBounds(0, pad, w, 16);

    // Mute / Solo buttons
    const int btnY = 24;
    const int btnW = (w - pad * 3) / 2;
    muteBtn_.setBounds(pad, btnY, btnW, kBtnH);
    if (!isMaster_)
        soloBtn_.setBounds(pad + btnW + pad, btnY, btnW, kBtnH);

    // VU meter — narrow column on the right
    const int vuX = w - kMeterW - pad;
    const int vuY = btnY + kBtnH + pad;
    const int vuH = kFaderH;
    // (VU is painted directly in paint(), not a child component)

    // Fader — to the left of the meter
    const int faderX = pad;
    const int faderW = vuX - faderX - 2;
    fader_.setBounds(faderX, vuY, faderW, vuH);

    // Pan knob below fader
    const int knobY = vuY + vuH + 2;
    const int knobH = std::min(kKnobSz, h - knobY - 2);
    if (knobH > 0)
        pan_.setBounds((w - kKnobSz) / 2, knobY, kKnobSz, knobH);
}

// ── Paint ──────────────────────────────────────────────────────────

void ChannelStrip::paint(juce::Graphics& g) {
    const auto r = getLocalBounds().toFloat();

    // Strip background
    g.setColour(isMaster_ ? kMasterBg : kStripBg);
    g.fillRoundedRectangle(r.reduced(1.f), 4.f);

    // Border
    g.setColour(kStripBorder);
    g.drawRoundedRectangle(r.reduced(0.5f), 4.f, 1.f);

    // Top accent bar — track color
    juce::Colour accentCol = isMaster_
        ? juce::Colour(Color::BondiBlue)
        : (track_ ? Color::forTrackIndex(
                        static_cast<int>(std::distance(
                            const_cast<const y2k::engine::TrackModel*>(track_),
                            const_cast<const y2k::engine::TrackModel*>(track_))))
                  : juce::Colour(Color::Graphite));
    // Simpler: cycle based on component sibling index
    if (!isMaster_ && getParentComponent() != nullptr) {
        const int idx = getParentComponent()->getIndexOfChildComponent(this);
        accentCol = Color::forTrackIndex(idx);
    }
    g.setColour(accentCol);
    g.fillRoundedRectangle(2.f, 2.f, r.getWidth() - 4.f, 4.f, 2.f);

    // VU meter (drawn over fader area on the right side)
    const int pad  = 4;
    const int vuX  = getWidth() - kMeterW - pad;
    const int vuY  = 24 + kBtnH + pad;
    const int vuH  = kFaderH;
    drawVUBar(g, { vuX, vuY, kMeterW, vuH }, vuLevel_);
}

// ── Callbacks ──────────────────────────────────────────────────────

void ChannelStrip::sliderValueChanged(juce::Slider* s) {
    if (!bus_) return;

    if (s == &fader_) {
        if (isMaster_) {
            bus_->setBusVolume("", static_cast<float>(fader_.getValue()));
        } else if (track_) {
            bus_->setTrackVolume(track_->id.toString(),
                                  static_cast<float>(fader_.getValue()));
        }
    } else if (s == &pan_) {
        if (!isMaster_ && track_) {
            bus_->setTrackPan(track_->id.toString(),
                               static_cast<float>(pan_.getValue()));
        }
    }
}

void ChannelStrip::onMuteClicked() {
    if (!bus_ || !track_) return;
    muteState_ = !muteState_;
    styleToggleButton(muteBtn_, kMuteOn, muteState_);
    bus_->setTrackMuted(track_->id.toString(), muteState_);
}

void ChannelStrip::onSoloClicked() {
    if (!bus_ || !track_) return;
    soloState_ = !soloState_;
    styleToggleButton(soloBtn_, kSoloOn, soloState_);
    bus_->setTrackSoloed(track_->id.toString(), soloState_);
}

// ═══════════════════════════════════════════════════════════════════
//  MixerView
// ═══════════════════════════════════════════════════════════════════

MixerView::MixerView() {
    addAndMakeVisible(masterStrip_);
}

void MixerView::setRouting(y2k::engine::ProjectRouting*    routing,
                            y2k::engine::SessionCommandBus* bus) {
    routing_ = routing;
    bus_     = bus;
    rebuild();
}

void MixerView::rebuild() {
    strips_.clear();

    if (!routing_) {
        resized();
        repaint();
        return;
    }

    for (auto& track : routing_->tracks) {
        auto* strip = strips_.add(new ChannelStrip(false));
        strip->setTrack(&track, bus_);
        addAndMakeVisible(strip);
    }

    // Master strip is always present (added in constructor)
    // Bind master bus volume to the master strip's fader
    masterStrip_.setMasterVolume(routing_->masterBus.volume);

    resized();
    repaint();
}

void MixerView::updateVULevels(const std::vector<float>& levels) {
    for (int i = 0; i < static_cast<int>(strips_.size()); ++i) {
        if (i < static_cast<int>(levels.size()))
            strips_[i]->setVULevel(levels[static_cast<size_t>(i)]);
    }
}

void MixerView::paint(juce::Graphics& g) {
    g.fillAll(kBg);

    // Separator between track strips and master
    const int masterX = getWidth() - ChannelStrip::kStripW - 4;
    g.setColour(juce::Colour(Color::BondiBlue).withAlpha(0.4f));
    g.fillRect(masterX - 2, 4, 2, getHeight() - 8);
}

void MixerView::resized() {
    const int h   = getHeight();
    const int pad = 2;

    // Master strip — pinned to right edge
    const int masterW = ChannelStrip::kStripW;
    masterStrip_.setBounds(getWidth() - masterW - pad, pad,
                            masterW, h - pad * 2);

    // Track strips — left-to-right before master
    int x = pad;
    for (auto* strip : strips_) {
        strip->setBounds(x, pad, ChannelStrip::kStripW, h - pad * 2);
        x += ChannelStrip::kStripW + pad;
    }

    // Ensure component is wide enough to hold all strips
    const int desiredW = x + masterW + pad * 2;
    if (getParentComponent() != nullptr && desiredW > getWidth())
        setSize(desiredW, h);
}

} // namespace y2k::ui

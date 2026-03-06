#include "main_window.h"
#include "y2k_ui/theme.h"
#include "y2k_engine/autoplay.h"
#include "y2k_engine/automation_writer.h"
#include <cmath>

namespace y2k::app {

static void styleBtn(juce::TextButton& btn,
                     juce::Colour face = juce::Colour(ui::Color::PlatinumMid)) {
    btn.setColour(juce::TextButton::buttonColourId,  face);
    btn.setColour(juce::TextButton::buttonOnColourId, face.darker(0.15f));
    btn.setColour(juce::TextButton::textColourOffId,  juce::Colour(ui::Color::PlatinumText));
    btn.setColour(juce::TextButton::textColourOnId,   juce::Colour(ui::Color::PlatinumText));
}

// ═════════════════════════════════════════════════════════════════
//  TransportBar
// ═════════════════════════════════════════════════════════════════
TransportBar::TransportBar(Y2KApplication& app) : app_(app) {
    styleBtn(rewindBtn_);
    rewindBtn_.onClick = [this] {
        app_.getAudioEngine().setPlaying(false);
        app_.getAudioEngine().rewind();
        playing_ = false;
        playBtn_.setButtonText("▶");
        styleBtn(playBtn_, juce::Colour(ui::Color::BondiBlue));
        playBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    };
    addAndMakeVisible(rewindBtn_);

    styleBtn(playBtn_, juce::Colour(ui::Color::BondiBlue));
    playBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    playBtn_.onClick = [this] {
        playing_ = !app_.getAudioEngine().isPlaying();
        app_.getAudioEngine().setPlaying(playing_);
        if (playing_) {
            playBtn_.setButtonText("⏸");
            playBtn_.setColour(juce::TextButton::buttonColourId,
                               juce::Colour(ui::Color::PlayGreen));
        } else {
            playBtn_.setButtonText("▶");
            styleBtn(playBtn_, juce::Colour(ui::Color::BondiBlue));
            playBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        }
    };
    addAndMakeVisible(playBtn_);

    styleBtn(stopBtn_);
    stopBtn_.onClick = [this] {
        app_.getAudioEngine().setPlaying(false);
        playing_ = false;
        playBtn_.setButtonText("▶");
        styleBtn(playBtn_, juce::Colour(ui::Color::BondiBlue));
        playBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    };
    addAndMakeVisible(stopBtn_);

    bpmLabel_.setText("BPM", juce::dontSendNotification);
    bpmLabel_.setFont(juce::Font(10.f));
    bpmLabel_.setColour(juce::Label::textColourId, juce::Colour(ui::Color::PlatinumLabelDim));
    bpmLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(bpmLabel_);

    bpmSlider_.setRange(40.0, 280.0, 0.5);
    bpmSlider_.setValue(120.0, juce::dontSendNotification);
    bpmSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    bpmSlider_.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 44, 20);
    bpmSlider_.onValueChange = [this] { app_.getAudioEngine().setBPM(bpmSlider_.getValue()); };
    addAndMakeVisible(bpmSlider_);

    timecodeLabel_.setText("001 : 1 : 000", juce::dontSendNotification);
    timecodeLabel_.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 13.f, juce::Font::plain));
    timecodeLabel_.setColour(juce::Label::textColourId, juce::Colour(ui::Color::BondiBlue));
    timecodeLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(timecodeLabel_);

    startTimerHz(10);
}

void TransportBar::setDisplayBeat(double beat) {
    const int bar   = static_cast<int>(beat / 4.0) + 1;
    const int b     = static_cast<int>(std::fmod(beat, 4.0)) + 1;
    const int ticks = static_cast<int>(std::fmod(beat, 1.0) * 960.0);
    timecodeLabel_.setText(juce::String::formatted("%03d : %d : %03d", bar, b, ticks),
                            juce::dontSendNotification);
}

void TransportBar::timerCallback() {
    bpmSlider_.setValue(app_.getAudioEngine().bpm(), juce::dontSendNotification);
}

void TransportBar::paint(juce::Graphics& g) {
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xFFF6F6F3), 0.f, 0.f,
                                             juce::Colour(0xFFE2E2DE), 0.f,
                                             static_cast<float>(getHeight()), false));
    g.fillAll();
    g.setColour(juce::Colour(ui::Color::PlatinumDark));
    g.drawHorizontalLine(getHeight() - 1, 0.f, static_cast<float>(getWidth()));
    g.setColour(juce::Colour(ui::Color::BondiBlue).withAlpha(0.4f));
    g.drawHorizontalLine(0, 0.f, static_cast<float>(getWidth()));
}

void TransportBar::resized() {
    auto r = getLocalBounds().reduced(8, 8);
    rewindBtn_.setBounds(r.removeFromLeft(30).withSizeKeepingCentre(28, 24));
    r.removeFromLeft(2);
    playBtn_  .setBounds(r.removeFromLeft(34).withSizeKeepingCentre(32, 28));
    r.removeFromLeft(2);
    stopBtn_  .setBounds(r.removeFromLeft(30).withSizeKeepingCentre(28, 24));
    r.removeFromLeft(16);
    timecodeLabel_.setBounds(r.removeFromLeft(110).withSizeKeepingCentre(108, 24));
    r.removeFromLeft(16);
    bpmLabel_.setBounds(r.removeFromLeft(32).withSizeKeepingCentre(30, 18));
    r.removeFromLeft(4);
    bpmSlider_.setBounds(r.removeFromLeft(180).withSizeKeepingCentre(178, 26));
}

// ═════════════════════════════════════════════════════════════════
//  AutoPlayBar
// ═════════════════════════════════════════════════════════════════
AutoPlayBar::AutoPlayBar(Y2KApplication& app) : app_(app) {
    scaleLabel_.setText("Scale:", juce::dontSendNotification);
    scaleLabel_.setFont(juce::Font(10.f));
    scaleLabel_.setColour(juce::Label::textColourId, juce::Colour(0xFF555550));
    addAndMakeVisible(scaleLabel_);

    scaleBox_.addItem("C Major",     1);
    scaleBox_.addItem("A Minor",     2);
    scaleBox_.addItem("D Pentatonic",3);
    scaleBox_.addItem("Blues",       4);
    scaleBox_.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(scaleBox_);

    densityLabel_.setText("Density:", juce::dontSendNotification);
    densityLabel_.setFont(juce::Font(10.f));
    densityLabel_.setColour(juce::Label::textColourId, juce::Colour(0xFF555550));
    addAndMakeVisible(densityLabel_);

    densitySlider_.setRange(0.1, 1.0, 0.05);
    densitySlider_.setValue(0.5);
    densitySlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    densitySlider_.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 36, 18);
    addAndMakeVisible(densitySlider_);

    // Style generate button (lime / action colour)
    generateBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFA6E22E));
    generateBtn_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xFF1A1A1A));
    generateBtn_.onClick = [this] {
        y2k::engine::AutoPlayParams p;
        p.scale    = static_cast<y2k::engine::Scale>(scaleBox_.getSelectedId() - 1);
        p.rootNote = 60;
        p.density  = static_cast<float>(densitySlider_.getValue());
        p.bars     = 8;
        p.seed     = static_cast<uint32_t>(juce::Time::currentTimeMillis() & 0xFFFFFFFF);
        p.startBeat = app_.getAudioEngine().ppqPosition();

        y2k::engine::AutoPlayGenerator gen;
        auto clip = gen.generate(p);

        // Insert into track 0 (Bondi Bass / instrument track)
        auto& session = app_.getSession();
        if (!session.tracks.empty()) {
            session.tracks[0]->clips.push_back(std::move(clip));
            app_.getAudioEngine().sessionChanged();
        }
    };
    addAndMakeVisible(generateBtn_);
}

void AutoPlayBar::paint(juce::Graphics& g) {
    g.setColour(juce::Colour(0xFFE8E8E4));
    g.fillAll();
    g.setColour(juce::Colour(0xFFB8B8B4));
    g.drawHorizontalLine(getHeight() - 1, 0.f, static_cast<float>(getWidth()));
    g.setFont(juce::Font(9.f, juce::Font::bold));
    g.setColour(juce::Colour(0xFF888884));
    g.drawText("AUTO-PLAY", getLocalBounds().withWidth(80).translated(4, 0),
               juce::Justification::centredLeft);
}

void AutoPlayBar::resized() {
    auto r = getLocalBounds().reduced(6, 6).withTrimmedLeft(72);
    generateBtn_  .setBounds(r.removeFromLeft(120).withSizeKeepingCentre(118, 24));
    r.removeFromLeft(12);
    scaleLabel_   .setBounds(r.removeFromLeft(38).withSizeKeepingCentre(36, 18));
    r.removeFromLeft(2);
    scaleBox_     .setBounds(r.removeFromLeft(110).withSizeKeepingCentre(108, 22));
    r.removeFromLeft(12);
    densityLabel_ .setBounds(r.removeFromLeft(48).withSizeKeepingCentre(46, 18));
    r.removeFromLeft(2);
    densitySlider_.setBounds(r.removeFromLeft(120).withSizeKeepingCentre(118, 22));
}

// ═════════════════════════════════════════════════════════════════
//  MainContentComponent
// ═════════════════════════════════════════════════════════════════
MainContentComponent::MainContentComponent(Y2KApplication& app)
    : app_(app), transport_(app), autoPlayBar_(app)
{
    addAndMakeVisible(transport_);
    addAndMakeVisible(autoPlayBar_);
    addAndMakeVisible(arrange_);
    addAndMakeVisible(kontrol_);

    arrange_.setBPM(app_.getAudioEngine().bpm());

    // Wire Kontrol Panel → AudioEngine
    kontrol_.onNoteOn  = [&app](int note, float vel) {
        app.getAudioEngine().noteOn(1, note, vel);
    };
    kontrol_.onNoteOff = [&app](int note) {
        app.getAudioEngine().noteOff(1, note);
    };
    kontrol_.onKnobChanged = [&app](int idx, float v) {
        // Knob 0 = SineOsc frequency (220–880 Hz)
        if (idx == 0) app.getAudioEngine().setToneFrequency(220.f + v * 660.f);
        // Knob 6 = track 0 volume
        if (idx == 6) app.getAudioEngine().setTrackVolume(0, v);
        // Knob 7 = track 0 pan
        if (idx == 7) app.getAudioEngine().setTrackPan(0, v * 2.f - 1.f);
    };
    kontrol_.onKnobRightClicked = [&app](int idx) {
        // Arm MIDI learn for this knob
        app.getMidiLearn().armLearn(
            y2k::engine::MidiLearnTargetType::GraphParam, 0, idx,
            juce::String(y2k::ui::Y2KKontrolPanel::knobName(idx)));
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon, "MIDI Learn",
            "Move a CC knob on your controller to bind it to: " +
            juce::String(y2k::ui::Y2KKontrolPanel::knobName(idx)));
    };

    startTimerHz(30);
    setSize(1200, 900);
}

void MainContentComponent::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(ui::Color::PlatinumMid));
}

void MainContentComponent::resized() {
    auto area = getLocalBounds();
    transport_  .setBounds(area.removeFromTop(TransportBar::kHeight));
    autoPlayBar_.setBounds(area.removeFromTop(AutoPlayBar::kHeight));
    kontrol_    .setBounds(area.removeFromBottom(ui::Y2KKontrolPanel::kPanelH));
    arrange_    .setBounds(area);
}

void MainContentComponent::timerCallback() {
    const bool   playing = app_.getAudioEngine().isPlaying();
    const double bpm     = app_.getAudioEngine().bpm();
    const double beat    = app_.getAudioEngine().ppqPosition();

    arrange_.setBPM(bpm);
    arrange_.setPlayheadBeat(beat);
    if (playing != arrange_.isPlaying()) arrange_.setPlaying(playing);
    transport_.setDisplayBeat(beat);
}

// ═════════════════════════════════════════════════════════════════
//  MainWindow
// ═════════════════════════════════════════════════════════════════
MainWindow::MainWindow(juce::String name, Y2KApplication& app)
    : juce::DocumentWindow(name, juce::Colour(ui::Color::PlatinumMid),
                           juce::DocumentWindow::allButtons),
      app_(app)
{
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    setResizeLimits(900, 700, 3840, 2160);
    auto* content = new MainContentComponent(app_);
    setContentOwned(content, true);
    centreWithSize(content->getWidth(), content->getHeight());
    setVisible(true);
}

void MainWindow::closeButtonPressed() { app_.systemRequestedQuit(); }

} // namespace y2k::app

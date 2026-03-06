#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "y2k_application.h"
#include "y2k_ui/screens/arrange_view.h"
#include "y2k_ui/components/y2k_kontrol_panel.h"

namespace y2k::app {

// ─────────────────────────────────────────────────────────────────
//  TransportBar
// ─────────────────────────────────────────────────────────────────
class TransportBar final : public juce::Component, private juce::Timer {
public:
    static constexpr int kHeight = 48;
    explicit TransportBar(Y2KApplication& app);
    void setDisplayBeat(double beat);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    Y2KApplication& app_;
    juce::TextButton rewindBtn_{"⏮"};
    juce::TextButton playBtn_  {"▶"};
    juce::TextButton stopBtn_  {"⏹"};
    juce::Label bpmLabel_, timecodeLabel_;
    juce::Slider bpmSlider_;
    bool playing_ = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportBar)
};

// ─────────────────────────────────────────────────────────────────
//  AutoPlayBar — "Generate Clip" toolbar
// ─────────────────────────────────────────────────────────────────
class AutoPlayBar final : public juce::Component {
public:
    static constexpr int kHeight = 38;
    explicit AutoPlayBar(Y2KApplication& app);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    Y2KApplication& app_;
    juce::TextButton generateBtn_ {"⚡ Generate Clip"};
    juce::ComboBox   scaleBox_;
    juce::Slider     densitySlider_;
    juce::Label      densityLabel_, scaleLabel_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutoPlayBar)
};

// ─────────────────────────────────────────────────────────────────
//  MainContentComponent
// ─────────────────────────────────────────────────────────────────
class MainContentComponent final : public juce::Component, private juce::Timer {
public:
    explicit MainContentComponent(Y2KApplication& app);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    Y2KApplication&         app_;
    TransportBar             transport_;
    AutoPlayBar              autoPlayBar_;
    y2k::ui::ArrangeView     arrange_;
    y2k::ui::Y2KKontrolPanel kontrol_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainContentComponent)
};

// ─────────────────────────────────────────────────────────────────
//  MainWindow
// ─────────────────────────────────────────────────────────────────
class MainWindow final : public juce::DocumentWindow {
public:
    MainWindow(juce::String name, Y2KApplication& app);
    void closeButtonPressed() override;
private:
    Y2KApplication& app_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

} // namespace y2k::app

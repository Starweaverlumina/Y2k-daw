#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../../engine/y2k_engine/session.h"
#include "../../../engine/y2k_engine/mixer/track_model.h"
#include "../../../engine/y2k_engine/session/session_command_bus.h"

#include <functional>
#include <vector>

namespace y2k::ui {

// ─────────────────────────────────────────────────────────────────
//  ArrangeView  —  main timeline / arrange screen
//
//  Layout (all sizes in pixels):
//
//   ┌──────────[kHeaderW]──┬────────────────────────┐  ─┬─ kRulerH
//   │  header corner       │   ruler (bar numbers)   │   │
//   ├──────────────────────┼────────────────────────┤  ─┘
//   │  track 0 header      │   track 0 clip lane    │  ─┬─ kTrackH
//   ├──────────────────────┼────────────────────────┤   │
//   │  track 1 header      │   track 1 clip lane    │  ...
//   └──────────────────────┴────────────────────────┘
//
//  Transport is injected from outside. No direct AudioEngine dep.
// ─────────────────────────────────────────────────────────────────
class ArrangeView final : public juce::Component,
                          private juce::Timer {
public:
    static constexpr int   kHeaderW   = 172;
    static constexpr int   kRulerH    = 26;
    static constexpr int   kTrackH    = 72;
    static constexpr int   kNumTracks = 8;
    static constexpr float kDefPPB    = 44.f;   // pixels per beat

    ArrangeView();
    ~ArrangeView() override = default;

    // Transport (call from UI thread only)
    void   setPlaying(bool p)        noexcept;
    void   setPlayheadBeat(double b) noexcept;
    void   setBPM(double bpm)        noexcept { bpm_ = bpm; }
    double getPlayheadBeat()   const noexcept { return playheadBeat_; }
    bool   isPlaying()         const noexcept { return playing_; }

    // View controls
    void setPixelsPerBeat(float ppb) noexcept;
    void setScrollBeat(double beat)  noexcept;

    // Session wiring (call from UI thread after session load)
    void setSession(y2k::engine::Y2KSession*        session,
                    y2k::engine::ProjectRouting*     routing,
                    y2k::engine::SessionCommandBus*  bus);

    // Callbacks fired when user interacts with the arrange view
    std::function<void(const juce::String& trackId,
                       const juce::String& clipId,
                       double newBeat)>          onClipMoved;
    std::function<void(const juce::String& trackId)> onTrackMuteToggled;
    std::function<void(const juce::String& trackId)> onTrackSoloToggled;
    std::function<void(const juce::String& clipId)>  onClipDoubleClicked;

    // juce::Component
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp  (const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e,
                        const juce::MouseWheelDetails& w) override;

private:
    // ── Internal model ────────────────────────────────────────────
    struct Clip {
        juce::String sessionClipId;   // links to y2k::engine::Clip::id (empty if demo)
        double       startBeat  = 0.0;
        double       lenBeats   = 4.0;
        juce::String label;
        bool         isMidi     = false;
    };

    struct Track {
        juce::String      sessionTrackId;  // links to TrackModel::id (empty if demo)
        juce::String      name;
        juce::Colour      color;
        bool              muted  = false;
        bool              soloed = false;
        float             volume = 0.8f;
        std::vector<Clip> clips;
    };

    void buildDefaultTracks();

    // ── Draw helpers ──────────────────────────────────────────────
    void drawBackground (juce::Graphics& g) const;
    void drawGridLines  (juce::Graphics& g) const;
    void drawRuler      (juce::Graphics& g) const;
    void drawTrackHeader(juce::Graphics& g, const Track& t, int row) const;
    void drawClipLane   (juce::Graphics& g, const Track& t, int row) const;
    void drawClip       (juce::Graphics& g, const Clip& c,
                         juce::Colour col, juce::Rectangle<float> r) const;
    void drawPlayhead   (juce::Graphics& g) const;
    // Draws a drag-ghost of the currently dragged clip
    void drawDragGhost  (juce::Graphics& g) const;

    // ── Coordinate conversion ─────────────────────────────────────
    // beatToX: beat → pixels relative to LEFT EDGE OF CLIP AREA (not component).
    // Add kHeaderW to get a component-relative x.
    float  beatToX (double beat) const noexcept;
    double xToBeat (float  x)    const noexcept;   // x is clip-area-relative

    // Bounds (all component-relative)
    juce::Rectangle<int> rulerBounds()        const noexcept;
    juce::Rectangle<int> clipAreaBounds()     const noexcept;
    juce::Rectangle<int> headerBounds(int row) const noexcept;
    juce::Rectangle<int> laneBounds  (int row) const noexcept;

    // Hit test helpers
    // Returns the row index for a y coordinate, or -1 if in ruler/below tracks
    int rowAtY(int y) const noexcept;
    // Returns the clip index within a track that contains a given beat, or -1
    int clipIndexAtBeat(int trackIdx, double beat) const noexcept;

    // ── Timer ─────────────────────────────────────────────────────
    void timerCallback() override;

    // ── State ─────────────────────────────────────────────────────
    std::vector<Track> tracks_;
    double  playheadBeat_  = 0.0;
    double  scrollBeat_    = 0.0;
    float   pixelsPerBeat_ = kDefPPB;
    double  bpm_           = 120.0;
    bool    playing_       = false;
    bool    rulerDrag_     = false;

    // ── Session wiring ────────────────────────────────────────────
    y2k::engine::Y2KSession*        session_ = nullptr;
    y2k::engine::ProjectRouting*    routing_ = nullptr;
    y2k::engine::SessionCommandBus* cmdBus_  = nullptr;

    // ── Clip drag state ───────────────────────────────────────────
    bool         draggingClip_      = false;
    int          dragTrackIdx_      = -1;
    int          dragClipIdx_       = -1;
    double       dragStartBeat_     = 0.0;   // original beat position
    double       dragCurrentBeat_   = 0.0;   // current dragged beat
    float        dragMouseStartX_   = 0.f;   // mouse x at drag start (component-relative)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArrangeView)
};

} // namespace y2k::ui

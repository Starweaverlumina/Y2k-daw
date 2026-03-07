#include "screens/arrange_view.h"    // own header
#include "theme.h"                    // y2k_ui PUBLIC include root

#include <cmath>
#include <algorithm>

namespace y2k::ui {

// ── Palette constants ─────────────────────────────────────────────
namespace {

const juce::Colour kBg       { 0xFFEEEEEB };
const juce::Colour kBgStripe { 0xFFE6E6E2 };
const juce::Colour kRulerBg  { 0xFFF4F4F0 };
const juce::Colour kHeaderBg { 0xFFE8E8E4 };
const juce::Colour kGrid     { 0xFFD0D0CC };
const juce::Colour kBarLine  { 0xFFB0B0AC };
const juce::Colour kTextDark { 0xFF1A1A1A };
const juce::Colour kTextDim  { 0xFF888884 };
const juce::Colour kPlayhead { 0xFFFF2D55 };

juce::Colour trackColor(int i) { return Color::forTrackIndex(i); }

} // namespace

// ─────────────────────────────────────────────────────────────────
ArrangeView::ArrangeView() {
    buildDefaultTracks();
    setWantsKeyboardFocus(false);
    setSize(1200, kRulerH + kNumTracks * kTrackH + 40);
}

void ArrangeView::buildDefaultTracks() {
    struct TD {
        const char* name;
        bool midi;
        struct CD { double s, l; const char* lbl; };
        std::initializer_list<CD> clips;
    };

    const TD defs[] = {
        { "Bondi Bass",    false, { {0,4,"Bass Verse"}, {8,4,"Bass Chorus"}, {16,4,"Bass Verse"} } },
        { "Tang Lead",     true,  { {4,4,"Lead A"}, {8,8,"Lead Chorus"}, {18,4,"Lead A"} } },
        { "Grape Pads",    false, { {0,16,"Pad Layer"}, {16,8,"Pad Layer"} } },
        { "Lime Synth",    true,  { {2,6,"Arp Up"}, {12,4,"Arp Fill"}, {18,6,"Arp Up"} } },
        { "Flower Drums",  true,  { {0,8,"Kit 808"}, {8,8,"Kit 808"}, {16,8,"Kit 808"} } },
        { "Strawberry FX", false, { {4,20,"FX Bus"} } },
        { "Graphite Vox",  false, { {8,4,"Vox Take 3"}, {14,4,"Vox Take 3"} } },
        { "Bondi Aux",     false, { {0,24,"Master Rev"} } },
    };
    static_assert(std::size(defs) == kNumTracks, "defs must match kNumTracks");

    tracks_.clear();
    tracks_.reserve(kNumTracks);
    for (int i = 0; i < kNumTracks; ++i) {
        Track t;
        t.name  = defs[i].name;
        t.color = trackColor(i);
        for (const auto& cd : defs[i].clips) {
            Clip c;
            c.startBeat = cd.s;
            c.lenBeats  = cd.l;
            c.label     = cd.lbl;
            c.isMidi    = defs[i].midi;
            t.clips.push_back(std::move(c));
        }
        tracks_.push_back(std::move(t));
    }
}

// ─────────────────────────────────────────────────────────────────
//  Session wiring
// ─────────────────────────────────────────────────────────────────

void ArrangeView::setSession(y2k::engine::Y2KSession*        session,
                              y2k::engine::ProjectRouting*    routing,
                              y2k::engine::SessionCommandBus* bus)
{
    session_ = session;
    routing_ = routing;
    cmdBus_  = bus;

    // Rebuild the default demo tracks first (preserves visual state)
    buildDefaultTracks();

    // If routing is available, mirror session track names/mute/solo
    // into the internal tracks_ array.
    if (routing_ != nullptr) {
        const int nRouting = static_cast<int>(routing_->tracks.size());
        const int nInternal = static_cast<int>(tracks_.size());
        const int nMirror = std::min(nRouting, nInternal);

        for (int i = 0; i < nMirror; ++i) {
            const auto& tm = routing_->tracks[i];
            tracks_[i].sessionTrackId = tm.id.toString();
            // Only override name if session has a meaningful one
            if (tm.name.isNotEmpty())
                tracks_[i].name = tm.name;
            tracks_[i].muted  = tm.muted;
            tracks_[i].soloed = tm.soloed;
            tracks_[i].volume = tm.volume;
        }
    }

    // Mirror session clip positions if session is available
    if (session_ != nullptr && routing_ != nullptr) {
        const int nTracks = static_cast<int>(tracks_.size());
        for (int i = 0; i < nTracks && i < (int)session_->tracks.size(); ++i) {
            auto& sessionTrack = *session_->tracks[i];
            auto& internalTrack = tracks_[i];

            // If session track has clips, replace demo clips with session clips
            if (!sessionTrack.clips.empty()) {
                internalTrack.clips.clear();
                for (auto& sc : sessionTrack.clips) {
                    Clip c;
                    c.sessionClipId = sc->id.toString();
                    c.startBeat     = sc->startBeat;
                    c.lenBeats      = sc->durationBeats;
                    c.label         = sc->name;
                    c.isMidi        = (sc->getType() == "MIDI");
                    internalTrack.clips.push_back(std::move(c));
                }
            }
        }
    }

    repaint();
}

// ─────────────────────────────────────────────────────────────────
//  Transport
// ─────────────────────────────────────────────────────────────────

void ArrangeView::setPlaying(bool p) noexcept {
    playing_ = p;
    if (p) startTimerHz(30); else stopTimer();
}

void ArrangeView::setPlayheadBeat(double b) noexcept {
    playheadBeat_ = b;
    repaint(clipAreaBounds());
    repaint(rulerBounds());
}

void ArrangeView::setPixelsPerBeat(float ppb) noexcept {
    pixelsPerBeat_ = juce::jlimit(6.f, 240.f, ppb);
    repaint();
}

void ArrangeView::setScrollBeat(double beat) noexcept {
    scrollBeat_ = std::max(0.0, beat);
    repaint();
}

void ArrangeView::timerCallback() {
    // Local tick while playing — real position comes from AudioEngine
    // via MainContentComponent::timerCallback at 30 Hz.
    playheadBeat_ += bpm_ / (60.0 * 30.0);
    repaint(clipAreaBounds());
    repaint(rulerBounds());
}

// ─────────────────────────────────────────────────────────────────
//  Coordinate helpers
// ─────────────────────────────────────────────────────────────────

float  ArrangeView::beatToX(double beat) const noexcept {
    return static_cast<float>((beat - scrollBeat_) * pixelsPerBeat_);
}

double ArrangeView::xToBeat(float x) const noexcept {
    return scrollBeat_ + static_cast<double>(x) / pixelsPerBeat_;
}

juce::Rectangle<int> ArrangeView::rulerBounds() const noexcept {
    return { 0, 0, getWidth(), kRulerH };
}

juce::Rectangle<int> ArrangeView::clipAreaBounds() const noexcept {
    return { kHeaderW, kRulerH, getWidth() - kHeaderW, getHeight() - kRulerH };
}

juce::Rectangle<int> ArrangeView::headerBounds(int row) const noexcept {
    return { 0, kRulerH + row * kTrackH, kHeaderW, kTrackH };
}

juce::Rectangle<int> ArrangeView::laneBounds(int row) const noexcept {
    return { kHeaderW, kRulerH + row * kTrackH, getWidth() - kHeaderW, kTrackH };
}

int ArrangeView::rowAtY(int y) const noexcept {
    if (y < kRulerH) return -1;
    const int row = (y - kRulerH) / kTrackH;
    if (row < 0 || row >= (int)tracks_.size()) return -1;
    return row;
}

int ArrangeView::clipIndexAtBeat(int trackIdx, double beat) const noexcept {
    if (trackIdx < 0 || trackIdx >= (int)tracks_.size()) return -1;
    const auto& clips = tracks_[trackIdx].clips;
    for (int i = 0; i < (int)clips.size(); ++i) {
        const auto& c = clips[i];
        if (beat >= c.startBeat && beat < c.startBeat + c.lenBeats)
            return i;
    }
    return -1;
}

// ─────────────────────────────────────────────────────────────────
//  paint / resized
// ─────────────────────────────────────────────────────────────────

void ArrangeView::paint(juce::Graphics& g) {
    drawBackground(g);
    drawGridLines(g);

    const int nTracks = static_cast<int>(tracks_.size());
    for (int i = 0; i < nTracks; ++i) {
        drawTrackHeader(g, tracks_[i], i);
        drawClipLane   (g, tracks_[i], i);
        g.setColour(kGrid);
        g.drawHorizontalLine(kRulerH + (i + 1) * kTrackH - 1,
                             0.f, static_cast<float>(getWidth()));
    }

    drawRuler(g);
    drawPlayhead(g);

    // Draw drag ghost on top of everything else in the clip area
    if (draggingClip_)
        drawDragGhost(g);

    // Header / clip-area divider
    g.setColour(kBarLine);
    g.drawVerticalLine  (kHeaderW - 1, float(kRulerH), float(getHeight()));
    g.drawHorizontalLine(kRulerH  - 1, 0.f,            float(getWidth()));

    // "Add Track" hint below last track
    const int addY = kRulerH + nTracks * kTrackH;
    if (addY + 16 < getHeight()) {
        g.setColour(kTextDim);
        g.setFont(11.f);
        g.drawText("+ Add Track", 16, addY + 10, 120, 18,
                   juce::Justification::centredLeft);
    }
}

void ArrangeView::resized() {}   // pure paint — no child components in Phase 1

// ─────────────────────────────────────────────────────────────────
//  Draw helpers
// ─────────────────────────────────────────────────────────────────

void ArrangeView::drawBackground(juce::Graphics& g) const {
    g.setColour(kHeaderBg);
    g.fillRect(0, kRulerH, kHeaderW, getHeight() - kRulerH);

    for (int i = 0; i < static_cast<int>(tracks_.size()); ++i) {
        g.setColour(i % 2 == 0 ? kBg : kBgStripe);
        g.fillRect(laneBounds(i));
    }
}

void ArrangeView::drawGridLines(juce::Graphics& g) const {
    const int W = getWidth() - kHeaderW;
    if (W <= 0) return;
    const int maxBeat = static_cast<int>(std::ceil(xToBeat(float(W)))) + 4;
    for (int beat = 0; beat <= maxBeat; ++beat) {
        const float x = float(kHeaderW) + beatToX(beat);
        if (x < float(kHeaderW)) continue;
        if (x > float(getWidth()))  break;
        g.setColour((beat % 4 == 0) ? kBarLine.withAlpha(0.55f)
                                     : kGrid   .withAlpha(0.4f));
        g.drawVerticalLine(int(x), float(kRulerH), float(getHeight()));
    }
}

void ArrangeView::drawRuler(juce::Graphics& g) const {
    g.setColour(kRulerBg);
    g.fillRect(rulerBounds());
    g.setColour(kHeaderBg);
    g.fillRect(0, 0, kHeaderW, kRulerH);

    const int W = getWidth() - kHeaderW;
    if (W <= 0) return;
    const int maxBeat = static_cast<int>(std::ceil(xToBeat(float(W)))) + 4;

    g.setFont(juce::Font(
        juce::Font::getDefaultMonospacedFontName(), 10.f, juce::Font::plain));

    for (int beat = 0; beat <= maxBeat; ++beat) {
        const float x = float(kHeaderW) + beatToX(beat);
        if (x < float(kHeaderW) - 1.f) continue;
        if (x > float(getWidth()))       break;

        const bool isBar  = (beat % 4 == 0);
        const bool isHalf = (beat % 2 == 0);
        g.setColour(isBar ? kBarLine : kGrid);
        const float tickH = float(kRulerH) * (isBar ? 0.70f : isHalf ? 0.45f : 0.28f);
        g.drawLine(x, float(kRulerH) - tickH, x, float(kRulerH), 1.f);

        if (isBar) {
            g.setColour(kTextDark);
            g.drawText(juce::String(beat / 4 + 1),
                       int(x) + 3, 3, 28, kRulerH - 4,
                       juce::Justification::centredLeft);
        }
    }

    g.setColour(kBarLine);
    g.drawHorizontalLine(kRulerH - 1, 0.f, float(getWidth()));
}

void ArrangeView::drawTrackHeader(juce::Graphics& g,
                                  const Track& t, int row) const {
    const auto r = headerBounds(row);

    g.setColour(t.color);
    g.fillRect(r.getX(), r.getY(), 4, r.getHeight());

    g.setFont(juce::Font(11.f, juce::Font::bold));
    g.setColour(kTextDark);
    g.drawText(t.name, r.getX() + 10, r.getY() + 7,
               r.getWidth() - 52, 16,
               juce::Justification::centredLeft, true);

    auto drawToggleBtn = [&](juce::Rectangle<int> br,
                              bool active,
                              juce::Colour onColor,
                              const char* label)
    {
        g.setColour(active ? onColor : juce::Colour(Color::PlatinumMid));
        g.fillRoundedRectangle(br.toFloat(), 3.f);
        g.setColour(kBarLine);
        g.drawRoundedRectangle(br.toFloat(), 3.f, 0.5f);
        g.setFont(juce::Font(8.f, juce::Font::bold));
        g.setColour(active ? juce::Colours::white : kTextDim);
        g.drawText(label, br, juce::Justification::centred);
    };

    drawToggleBtn({ r.getRight() - 46, r.getY() + 26, 18, 13 },
                  t.muted,  juce::Colour(Color::Tangerine), "M");
    drawToggleBtn({ r.getRight() - 24, r.getY() + 26, 18, 13 },
                  t.soloed, juce::Colour(Color::PlayGreen), "S");

    const int fX = r.getX() + 10;
    const int fY = r.getY() + 47;
    const int fW = r.getWidth() - 20;
    g.setColour(kGrid);
    g.fillRoundedRectangle(float(fX), float(fY), float(fW), 3.f, 1.5f);
    g.setColour(t.color.withAlpha(0.75f));
    g.fillRoundedRectangle(float(fX), float(fY), float(fW) * t.volume, 3.f, 1.5f);
}

void ArrangeView::drawClipLane(juce::Graphics& g,
                               const Track& t, int row) const {
    const auto  lane       = laneBounds(row);
    const float laneRight  = float(lane.getRight());
    const float cy         = float(lane.getY()) + 4.f;
    const float ch         = float(lane.getHeight()) - 8.f;

    for (int ci = 0; ci < (int)t.clips.size(); ++ci) {
        const Clip& clip = t.clips[ci];

        // If this is the clip being dragged, show it at the drag position
        double displayBeat = clip.startBeat;
        if (draggingClip_ && dragTrackIdx_ == row && dragClipIdx_ == ci)
            displayBeat = dragCurrentBeat_;

        const float cx = float(kHeaderW) + beatToX(displayBeat);
        const float cw = float(clip.lenBeats) * pixelsPerBeat_ - 2.f;
        if (cx + cw < float(kHeaderW)) continue;
        if (cx > laneRight)             break;
        drawClip(g, clip, t.color, { cx, cy, cw, ch });
    }
}

void ArrangeView::drawClip(juce::Graphics& g, const Clip& clip,
                            juce::Colour col,
                            juce::Rectangle<float> r) const {
    constexpr float kRad = 5.f;
    if (r.getWidth() < 2.f) return;

    g.setColour(col.withAlpha(0.82f));
    g.fillRoundedRectangle(r, kRad);

    g.setColour(col.darker(0.3f));
    juce::Path hdr;
    hdr.addRoundedRectangle(r.getX(), r.getY(), r.getWidth(), 15.f,
                             kRad, kRad, true, true, false, false);
    g.fillPath(hdr);

    g.setColour(juce::Colours::white.withAlpha(0.16f));
    g.fillRoundedRectangle(r.withHeight(r.getHeight() * 0.38f), kRad);

    g.setColour(col.darker(0.45f).withAlpha(0.5f));
    g.drawRoundedRectangle(r, kRad, 1.f);

    if (r.getWidth() >= 20.f) {
        g.setFont(juce::Font(9.f, juce::Font::bold));
        g.setColour(juce::Colours::white.withAlpha(0.95f));
        g.drawText(clip.label,
                   int(r.getX()) + 4, int(r.getY()) + 1,
                   int(r.getWidth()) - 8, 13,
                   juce::Justification::centredLeft, true);
    }

    const juce::Rectangle<float> content {
        r.getX() + 2.f, r.getY() + 16.f,
        r.getWidth() - 4.f, r.getHeight() - 19.f
    };
    if (content.getWidth() < 4.f || content.getHeight() < 3.f) return;

    juce::Random rng(static_cast<int64_t>(
        std::hash<std::string>{}(clip.label.toStdString())));

    g.setColour(juce::Colours::white.withAlpha(0.52f));

    if (clip.isMidi) {
        const int notes = static_cast<int>(clip.lenBeats * 2.5);
        for (int i = 0; i < notes && i < 28; ++i) {
            const float nx  = content.getX() + rng.nextFloat() * content.getWidth();
            const float nw  = std::min(rng.nextFloat() * content.getWidth() * 0.18f + 3.f,
                                       content.getRight() - nx);
            const float ny  = content.getY() + rng.nextFloat() * content.getHeight();
            if (nw > 0.f) g.fillRect(juce::Rectangle<float>{ nx, ny, nw, 2.f });
        }
    } else {
        const int   segs = static_cast<int>(content.getWidth() / 2.f);
        const float mid  = content.getCentreY();
        const float maxH = content.getHeight() * 0.44f;
        for (int i = 0; i < segs; ++i) {
            const float h = rng.nextFloat() * maxH;
            g.fillRect(juce::Rectangle<float>{
                content.getX() + float(i) * 2.f, mid - h, 1.5f, h * 2.f });
        }
    }
}

void ArrangeView::drawPlayhead(juce::Graphics& g) const {
    const float px = float(kHeaderW) + beatToX(playheadBeat_);
    if (px < float(kHeaderW) || px > float(getWidth())) return;

    g.setColour(juce::Colour(0x20000000));
    g.drawVerticalLine(int(px) + 1, float(kRulerH), float(getHeight()));

    g.setColour(kPlayhead);
    g.drawVerticalLine(int(px), float(kRulerH), float(getHeight()));

    juce::Path tri;
    tri.addTriangle(px - 5.f, float(kRulerH) - 1.f,
                    px + 5.f, float(kRulerH) - 1.f,
                    px,       float(kRulerH) + 7.f);
    g.fillPath(tri);
}

void ArrangeView::drawDragGhost(juce::Graphics& g) const {
    if (dragTrackIdx_ < 0 || dragTrackIdx_ >= (int)tracks_.size()) return;
    if (dragClipIdx_  < 0 || dragClipIdx_  >= (int)tracks_[dragTrackIdx_].clips.size()) return;

    const Track& t    = tracks_[dragTrackIdx_];
    const Clip&  clip = t.clips[dragClipIdx_];
    const auto   lane = laneBounds(dragTrackIdx_);
    const float  cy   = float(lane.getY()) + 4.f;
    const float  ch   = float(lane.getHeight()) - 8.f;
    const float  cx   = float(kHeaderW) + beatToX(dragCurrentBeat_);
    const float  cw   = float(clip.lenBeats) * pixelsPerBeat_ - 2.f;

    // Semi-transparent ghost outline
    g.setColour(t.color.withAlpha(0.45f));
    g.fillRoundedRectangle({ cx, cy, cw, ch }, 5.f);
    g.setColour(t.color.brighter(0.3f).withAlpha(0.8f));
    g.drawRoundedRectangle({ cx, cy, cw, ch }, 5.f, 1.5f);
}

// ─────────────────────────────────────────────────────────────────
//  Mouse
// ─────────────────────────────────────────────────────────────────

void ArrangeView::mouseDown(const juce::MouseEvent& e) {
    // ── Ruler drag: click in ruler area (above track lane) ────────
    if (e.y < kRulerH && e.x >= kHeaderW) {
        rulerDrag_    = true;
        playheadBeat_ = std::max(0.0, xToBeat(float(e.x - kHeaderW)));
        repaint(rulerBounds());
        repaint(clipAreaBounds());
        return;
    }

    rulerDrag_ = false;

    const int row = rowAtY(e.y);
    if (row < 0) return;

    // ── Track header click ─────────────────────────────────────────
    if (e.x < kHeaderW) {
        const auto r = headerBounds(row);
        auto& t = tracks_[row];

        // Mute button area: right side of header, lower portion
        const juce::Rectangle<int> muteBtn  { r.getRight() - 46, r.getY() + 26, 18, 13 };
        const juce::Rectangle<int> soloBtn  { r.getRight() - 24, r.getY() + 26, 18, 13 };

        if (muteBtn.contains(e.x, e.y)) {
            t.muted = !t.muted;
            if (cmdBus_ && t.sessionTrackId.isNotEmpty())
                cmdBus_->setTrackMuted(t.sessionTrackId, t.muted);
            if (onTrackMuteToggled && t.sessionTrackId.isNotEmpty())
                onTrackMuteToggled(t.sessionTrackId);
            repaint(r);
        } else if (soloBtn.contains(e.x, e.y)) {
            t.soloed = !t.soloed;
            if (cmdBus_ && t.sessionTrackId.isNotEmpty())
                cmdBus_->setTrackSoloed(t.sessionTrackId, t.soloed);
            if (onTrackSoloToggled && t.sessionTrackId.isNotEmpty())
                onTrackSoloToggled(t.sessionTrackId);
            repaint(r);
        }
        return;
    }

    // ── Clip area click ────────────────────────────────────────────
    {
        const double clickBeat = xToBeat(float(e.x - kHeaderW));
        const int ci = clipIndexAtBeat(row, clickBeat);

        if (ci >= 0) {
            const auto& clip = tracks_[row].clips[ci];

            // Double click → open piano roll / detail view
            if (e.getNumberOfClicks() >= 2) {
                if (onClipDoubleClicked && clip.sessionClipId.isNotEmpty())
                    onClipDoubleClicked(clip.sessionClipId);
                return;
            }

            // Begin clip drag
            draggingClip_     = true;
            dragTrackIdx_     = row;
            dragClipIdx_      = ci;
            dragStartBeat_    = clip.startBeat;
            dragCurrentBeat_  = clip.startBeat;
            dragMouseStartX_  = float(e.x);
        }
    }
}

void ArrangeView::mouseDrag(const juce::MouseEvent& e) {
    if (rulerDrag_) {
        playheadBeat_ = std::max(0.0, xToBeat(float(e.x - kHeaderW)));
        repaint(rulerBounds());
        repaint(clipAreaBounds());
        return;
    }

    if (!draggingClip_) return;

    // Compute new beat position, snapped to quarter-beat grid
    const float deltaX    = float(e.x) - dragMouseStartX_;
    const double deltaBeat = static_cast<double>(deltaX) / pixelsPerBeat_;
    double newBeat = dragStartBeat_ + deltaBeat;

    // Snap to 0.25-beat grid
    newBeat = std::round(newBeat * 4.0) / 4.0;
    newBeat = std::max(0.0, newBeat);

    if (newBeat != dragCurrentBeat_) {
        dragCurrentBeat_ = newBeat;
        repaint(clipAreaBounds());
    }
}

void ArrangeView::mouseUp(const juce::MouseEvent& e) {
    if (rulerDrag_) {
        rulerDrag_ = false;
        return;
    }

    if (!draggingClip_) return;

    const double finalBeat = dragCurrentBeat_;
    const int    ti        = dragTrackIdx_;
    const int    ci        = dragClipIdx_;

    // Commit the new beat position
    if (ti >= 0 && ti < (int)tracks_.size() &&
        ci >= 0 && ci < (int)tracks_[ti].clips.size())
    {
        auto& clip = tracks_[ti].clips[ci];

        // Only commit if the position actually changed
        if (std::abs(finalBeat - dragStartBeat_) > 1e-6) {
            clip.startBeat = finalBeat;

            // Notify via command bus (undo-able)
            if (cmdBus_ && clip.sessionClipId.isNotEmpty() &&
                tracks_[ti].sessionTrackId.isNotEmpty()) {
                cmdBus_->moveClip(tracks_[ti].sessionTrackId,
                                  clip.sessionClipId,
                                  finalBeat);
            }

            // Notify callback
            if (onClipMoved && clip.sessionClipId.isNotEmpty() &&
                tracks_[ti].sessionTrackId.isNotEmpty()) {
                onClipMoved(tracks_[ti].sessionTrackId,
                            clip.sessionClipId,
                            finalBeat);
            }
        }
    }

    draggingClip_  = false;
    dragTrackIdx_  = -1;
    dragClipIdx_   = -1;
    repaint(clipAreaBounds());

    juce::ignoreUnused(e);
}

void ArrangeView::mouseWheelMove(const juce::MouseEvent& e,
                                 const juce::MouseWheelDetails& w) {
    if (e.mods.isCommandDown())
        setPixelsPerBeat(pixelsPerBeat_ * (1.f + w.deltaY * 0.25f));
    else
        setScrollBeat(scrollBeat_ - double(w.deltaX) * 2.0);
}

} // namespace y2k::ui

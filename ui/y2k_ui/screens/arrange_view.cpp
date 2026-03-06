#include "screens/arrange_view.h"    // own header
#include "theme.h"                    // y2k_ui PUBLIC include root

#include <cmath>

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
        for (const auto& cd : defs[i].clips)
            t.clips.push_back({ cd.s, cd.l, cd.lbl, defs[i].midi });
        tracks_.push_back(std::move(t));
    }
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

    for (const Clip& clip : t.clips) {
        const float cx = float(kHeaderW) + beatToX(clip.startBeat);
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

// ─────────────────────────────────────────────────────────────────
//  Mouse
// ─────────────────────────────────────────────────────────────────

void ArrangeView::mouseDown(const juce::MouseEvent& e) {
    if (e.y < kRulerH && e.x >= kHeaderW) {
        rulerDrag_    = true;
        playheadBeat_ = std::max(0.0, xToBeat(float(e.x - kHeaderW)));
        repaint(rulerBounds());
        repaint(clipAreaBounds());
    }
}

void ArrangeView::mouseDrag(const juce::MouseEvent& e) {
    if (!rulerDrag_) return;
    playheadBeat_ = std::max(0.0, xToBeat(float(e.x - kHeaderW)));
    repaint(rulerBounds());
    repaint(clipAreaBounds());
}

void ArrangeView::mouseWheelMove(const juce::MouseEvent& e,
                                 const juce::MouseWheelDetails& w) {
    if (e.mods.isCommandDown())
        setPixelsPerBeat(pixelsPerBeat_ * (1.f + w.deltaY * 0.25f));
    else
        setScrollBeat(scrollBeat_ - double(w.deltaX) * 2.0);
}

} // namespace y2k::ui

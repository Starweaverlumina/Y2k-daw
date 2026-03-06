#pragma once

// ═══════════════════════════════════════════════════════════════════
//  Y2K UI — Theme System
//  iMac G3 color palette, Platinum neutrals, typography,
//  material constants. The complete visual design language.
// ═══════════════════════════════════════════════════════════════════

#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace y2k::ui {

// ═══════════════════════════════════════════════════════════════════
//  Color Palette
// ═══════════════════════════════════════════════════════════════════
namespace Color {

    // ── iMac G3 Colors (the 13 original + extended) ───────────────
    inline constexpr juce::uint32 BondiBlue      = 0xFF0071E3;  // Original 1998
    inline constexpr juce::uint32 Strawberry      = 0xFFFF2D55;
    inline constexpr juce::uint32 Tangerine        = 0xFFFF9500;
    inline constexpr juce::uint32 Lime             = 0xFFA6E22E;
    inline constexpr juce::uint32 Grape            = 0xFFAF52DE;
    inline constexpr juce::uint32 BlueDalmatian    = 0xFF2E4C8A;
    inline constexpr juce::uint32 FlowerPower      = 0xFFFF69B4;
    inline constexpr juce::uint32 SageGreen        = 0xFF5AC8FA;
    inline constexpr juce::uint32 Graphite         = 0xFF5E5E5E;
    inline constexpr juce::uint32 IndyRed          = 0xFFCC3333;
    inline constexpr juce::uint32 KeyLime          = 0xFF8FBC44;
    inline constexpr juce::uint32 DalmationBlue    = 0xFF0099CC;
    inline constexpr juce::uint32 SnowyWhite       = 0xFFF5F5F0;

    // ── Platinum UI Neutrals ──────────────────────────────────────
    inline constexpr juce::uint32 PlatinumWhite    = 0xFFFFFFFF;
    inline constexpr juce::uint32 PlatinumLight    = 0xFFF2F2F2;
    inline constexpr juce::uint32 PlatinumMid      = 0xFFE8E8E8;
    inline constexpr juce::uint32 PlatinumDark     = 0xFFCCCCCC;
    inline constexpr juce::uint32 PlatinumShadow   = 0xFFAAAAAA;
    inline constexpr juce::uint32 PlatinumText     = 0xFF333333;
    inline constexpr juce::uint32 PlatinumLabelDim = 0xFF888888;

    // ── Semantic colors ───────────────────────────────────────────
    inline constexpr juce::uint32 RecordRed        = 0xFFFF3B30;  // Record button
    inline constexpr juce::uint32 PlayGreen        = 0xFF4CD964;  // Play active
    inline constexpr juce::uint32 ClipLevel        = 0xFFFF9500;  // Near clip
    inline constexpr juce::uint32 ClipPeak         = 0xFFFF2D55;  // Clipping
    inline constexpr juce::uint32 SafeGreen        = 0xFF4CD964;  // VU safe

    // ── Patch-cable signal type colors ────────────────────────────
    inline constexpr juce::uint32 CableAudio       = 0xFF0071E3;  // Bondi Blue
    inline constexpr juce::uint32 CableMidi        = 0xFFAF52DE;  // Grape
    inline constexpr juce::uint32 CableSidechain   = 0xFFA6E22E;  // Lime
    inline constexpr juce::uint32 CableCV          = 0xFFFF9500;  // Tangerine

    // ── Helper: get theme color for a TrackColor index ────────────
    inline juce::Colour forTrackIndex(int idx) {
        static const juce::uint32 palette[] = {
            BondiBlue, Tangerine, Strawberry, Grape, Lime, FlowerPower, BlueDalmatian, Graphite
        };
        return juce::Colour(palette[idx % 8]);
    }

} // namespace Color

// ═══════════════════════════════════════════════════════════════════
//  Typography
// ═══════════════════════════════════════════════════════════════════
namespace Type {

    // Font sizes (pt)
    inline constexpr float Micro    =  9.f;
    inline constexpr float Small    = 11.f;
    inline constexpr float Body     = 13.f;
    inline constexpr float UI       = 14.f;
    inline constexpr float Label    = 12.f;
    inline constexpr float Header   = 18.f;
    inline constexpr float Display  = 28.f;
    inline constexpr float Hero     = 48.f;

    // Y2K: Chicago for UI elements, system mono for data display
    inline juce::Font chicago(float size, bool bold = false) {
        // Falls back to bitmap-style font if Chicago is embedded
        return juce::Font(juce::Font::getDefaultSansSerifFontName(), size,
                          bold ? juce::Font::bold : juce::Font::plain);
    }

    inline juce::Font mono(float size) {
        return juce::Font(juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain);
    }

} // namespace Type

// ═══════════════════════════════════════════════════════════════════
//  Geometry & Spacing
// ═══════════════════════════════════════════════════════════════════
namespace Geom {

    inline constexpr float RadiusSmall   =  6.f;
    inline constexpr float RadiusMid     = 12.f;
    inline constexpr float RadiusLarge   = 20.f;
    inline constexpr float RadiusFull    = 99.f;

    inline constexpr int SpaceXS        =  4;
    inline constexpr int SpaceS         =  8;
    inline constexpr int SpaceM         = 12;
    inline constexpr int SpaceL         = 20;
    inline constexpr int SpaceXL        = 32;
    inline constexpr int SpaceXXL       = 48;

    // Component dimensions
    inline constexpr int ButtonH        = 28;
    inline constexpr int FaderH         = 140;
    inline constexpr int FaderW         = 24;
    inline constexpr int KnobSize       = 48;
    inline constexpr int KnobSizeSmall  = 32;
    inline constexpr int TrackH         = 80;
    inline constexpr int TrackHMin      = 40;
    inline constexpr int TrackHMax      = 200;
    inline constexpr int MixerStripW    = 72;
    inline constexpr int TransportH     = 52;
    inline constexpr int TabBarH        = 36;

    // VU meter
    inline constexpr int VUSegmentH     = 3;
    inline constexpr int VUSegmentGap   = 1;
    inline constexpr int VUNumSegments  = 24;

} // namespace Geom

// ═══════════════════════════════════════════════════════════════════
//  Material System  —  translucency & rendering parameters
// ═══════════════════════════════════════════════════════════════════
struct Material {
    float opacity         = 0.85f;   // Base panel opacity
    float blurRadius      = 12.f;    // Gaussian blur (for frosted glass)
    float innerShadow     = 0.3f;    // Inner shadow strength (depth)
    float topHighlight    = 0.6f;    // Top-edge specular highlight
    float cornerRadius    = Geom::RadiusMid;
    juce::Colour tint     = juce::Colour(0xFFFFFFFF); // White = neutral

    // Preset materials
    static Material MainPanel()  { return {0.88f, 10.f, 0.25f, 0.5f, 12.f}; }
    static Material SidePanel()  { return {0.92f,  8.f, 0.20f, 0.4f, 12.f}; }
    static Material Popover()    { return {0.82f, 16.f, 0.30f, 0.7f, 14.f}; }
    static Material Glass()      { return {0.40f, 20.f, 0.15f, 0.8f, 16.f}; }
    static Material ClipAudio()  { return {0.70f,  4.f, 0.20f, 0.4f,  6.f}; }
    static Material ClipMidi()   { return {0.65f,  4.f, 0.20f, 0.4f,  6.f}; }
    static Material Solid()      { return {1.00f,  0.f, 0.35f, 0.6f, 12.f}; }
};

// ═══════════════════════════════════════════════════════════════════
//  Shadow definitions
// ═══════════════════════════════════════════════════════════════════
struct Shadow {
    juce::Colour colour;
    juce::Point<int> offset;
    int radius;

    static Shadow Soft()   { return {juce::Colour(0x30000000), {0, 2}, 8};  }
    static Shadow Medium() { return {juce::Colour(0x40000000), {0, 4}, 14}; }
    static Shadow Heavy()  { return {juce::Colour(0x55000000), {0, 6}, 20}; }
    static Shadow Inner()  { return {juce::Colour(0x40000000), {0, 1}, 4};  }
};

// ═══════════════════════════════════════════════════════════════════
//  Animation constants  —  spring physics style
// ═══════════════════════════════════════════════════════════════════
namespace Anim {

    inline constexpr int   Fast         = 150;   // ms
    inline constexpr int   Normal       = 250;   // ms
    inline constexpr int   Slow         = 400;   // ms
    inline constexpr float SpringStiff  = 300.f; // Spring stiffness
    inline constexpr float SpringDamp   = 20.f;  // Damping (no wobble = √(4k))
    inline constexpr float ButtonScale  = 0.95f; // Scale on press
    inline constexpr float ClickSound   = 0.2f;  // Volume of click feedback

} // namespace Anim

// ═══════════════════════════════════════════════════════════════════
//  Drawing utilities
// ═══════════════════════════════════════════════════════════════════
struct Draw {
    // Render a "plastic" panel — the core Y2K widget primitive
    static void plasticPanel(juce::Graphics& g,
                             juce::Rectangle<float> bounds,
                             juce::Colour baseColor,
                             const Material& mat = Material::MainPanel());

    // Render a VU meter segment
    static void vuSegment(juce::Graphics& g,
                          juce::Rectangle<int> bounds,
                          float level,      // 0.0 – 1.0
                          int numSegments = Geom::VUNumSegments);

    // Render a "plastic" knob
    static void knob(juce::Graphics& g,
                     juce::Rectangle<float> bounds,
                     float value,           // 0.0 – 1.0
                     juce::Colour trackColor,
                     bool isHovered = false,
                     bool isDragging = false);

    // Render fader track + cap
    static void fader(juce::Graphics& g,
                      juce::Rectangle<float> bounds,
                      float value,          // 0.0 – 1.0
                      juce::Colour capColor,
                      bool isHovered = false);

    // Pin-striped background (arrange view)
    static void pinstripes(juce::Graphics& g,
                           juce::Rectangle<int> bounds,
                           juce::Colour lightColor = juce::Colour(0xFFF8F8F8),
                           juce::Colour darkColor  = juce::Colour(0xFFEFEFEF),
                           int stripeWidth = 4);

    // "Blob" organic accent shape
    static void blob(juce::Graphics& g,
                     juce::Point<float> centre,
                     float radius,
                     juce::Colour color,
                     float opacity = 0.15f);
};

} // namespace y2k::ui

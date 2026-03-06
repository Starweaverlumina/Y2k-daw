#pragma once

// ═══════════════════════════════════════════════════════════════════
//  Y2K Engine — Session
//  The complete project document model.
//  Saved as a .y2k bundle (directory package).
// ═══════════════════════════════════════════════════════════════════

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace y2k::engine {

// ─────────────────────────────────────────────────────────────────
//  Y2K Color (mirrors DSP definition for session layer)
// ─────────────────────────────────────────────────────────────────
enum class TrackColor : uint8_t {
    BondiBlue = 0,
    Tangerine,
    Strawberry,
    Grape,
    Lime,
    FlowerPower,
    BlueDalmatian,
    Graphite,
    COUNT
};

juce::Colour trackColorToJuce(TrackColor c) noexcept;
juce::String trackColorName(TrackColor c) noexcept;

// ─────────────────────────────────────────────────────────────────
//  Automation
// ─────────────────────────────────────────────────────────────────
struct AutomationPoint {
    double beat;   // Position in beats
    float  value;  // Normalised [0,1]
};

enum class AutomationCurve { Linear, Exponential, Stepped };

struct AutomationLane {
    uint32_t              targetNodeId;
    int                   targetParamId;
    juce::String          paramName;
    AutomationCurve       curve     = AutomationCurve::Linear;
    bool                  isActive  = false;
    std::vector<AutomationPoint> points;

    float evaluate(double beat) const noexcept;
};

// ─────────────────────────────────────────────────────────────────
//  Clips
// ─────────────────────────────────────────────────────────────────
struct FadeParams {
    double              inBeats    = 0.0;
    double              outBeats   = 0.0;
    enum class Curve { Linear, SCurve, Exponential } curve = Curve::SCurve;
};

struct WarpMarker {
    double sourceSec;  // Position in the original audio file
    double beatPos;    // What beat it aligns to in the session
};

class Clip {
public:
    virtual ~Clip() = default;

    juce::Uuid   id;
    juce::String name;
    double       startBeat  = 0.0;
    double       durationBeats = 4.0;
    double       loopStart  = 0.0;      // Within clip
    double       loopLength = 0.0;      // 0 = no loop
    FadeParams   fades;
    TrackColor   color = TrackColor::BondiBlue;

    virtual juce::String getType() const = 0;
    virtual juce::ValueTree toValueTree() const;
    static std::unique_ptr<Clip> fromValueTree(const juce::ValueTree&);
};

class AudioClip : public Clip {
public:
    juce::File   sourceFile;
    double       sourceOffsetSec = 0.0;

    enum class StretchQuality { Fast, Clean, Formant };
    StretchQuality stretchQuality = StretchQuality::Clean;
    float          stretchSpeed   = 1.f;   // 0.5 – 2.0
    float          stretchPitch   = 0.f;   // Semitones
    bool           formantPreserve = true;

    std::vector<WarpMarker> warpMarkers;

    juce::String getType() const override { return "Audio"; }
    juce::ValueTree toValueTree() const override;
};

class MidiClip : public Clip {
public:
    struct Note {
        int    pitch;
        float  velocity;   // 0-1
        double startBeat;
        double durationBeats;
    };

    struct CC {
        int    number;
        double beat;
        float  value;      // 0-1
    };

    std::vector<Note> notes;
    std::vector<CC>   controllers;
    int               gridDivision = 16; // 4, 8, 16, 32

    juce::String getType() const override { return "MIDI"; }
    juce::ValueTree toValueTree() const override;
};

// ─────────────────────────────────────────────────────────────────
//  Track
// ─────────────────────────────────────────────────────────────────
class Track {
public:
    enum class Type { Audio, Instrument, Aux, Master };

    juce::Uuid   id;
    juce::String name         = "Track";
    Type         type         = Type::Audio;
    TrackColor   color        = TrackColor::BondiBlue;

    bool         isMuted      = false;
    bool         isSoloed     = false;
    bool         isArmed      = false;   // Record arm

    float        volume       = 80.f;    // 0–100
    float        pan          = 0.f;     // -50 to +50
    int          pixelHeight  = 80;      // Arrange view lane height

    // Routing
    std::optional<juce::Uuid> inputFrom;    // For Aux tracks
    std::vector<juce::Uuid>   auxSends;    // Aux buses

    // Insert chain: node IDs in the ProcessGraph
    std::vector<uint32_t> insertNodeIds;

    // Content
    std::vector<std::unique_ptr<Clip>> clips;

    // Automation
    std::vector<AutomationLane> automationLanes;

    juce::ValueTree toValueTree() const;
    static std::unique_ptr<Track> fromValueTree(const juce::ValueTree&);

private:
    JUCE_DECLARE_NON_COPYABLE(Track)
};

// ─────────────────────────────────────────────────────────────────
//  Arrangement markers
// ─────────────────────────────────────────────────────────────────
struct ArrangementMarker {
    double       beat;
    juce::String label;   // "Intro", "Verse", "Chorus", "Bridge", "Outro"
    TrackColor   color = TrackColor::BondiBlue;
};

// ─────────────────────────────────────────────────────────────────
//  Mixer snapshot  (for A/B comparison)
// ─────────────────────────────────────────────────────────────────
struct ChannelSnapshot {
    juce::Uuid trackId;
    float      volume;
    float      pan;
    bool       isMuted;
    std::vector<std::pair<int, float>> pluginParams; // paramIdx → value
};

struct MixerSnapshot {
    juce::String                  name;  // "Mix A", "Mix B"
    juce::Time                    created;
    std::vector<ChannelSnapshot>  channels;
};

// ─────────────────────────────────────────────────────────────────
//  Y2KSession  —  the complete project document
// ─────────────────────────────────────────────────────────────────
class Y2KSession {
public:
    static constexpr const char* FILE_EXTENSION    = ".y2k";
    static constexpr const char* FORMAT_VERSION    = "1.0";

    // ── Metadata ─────────────────────────────────────────────────
    juce::Uuid   id;
    juce::String projectName   = "Untitled";
    juce::String author;
    juce::Time   createdDate;
    juce::Time   modifiedDate;
    juce::String dawVersion    = "1.0.0";
    TrackColor   themeColor    = TrackColor::BondiBlue;

    // ── Timeline ─────────────────────────────────────────────────
    struct Timeline {
        double tempo           = 120.0;    // 20–999 BPM
        int    timeSigNum      = 4;
        int    timeSigDen      = 4;
        int    sampleRate      = 44100;    // 44100, 48000, 88200, 96000
        int    bitDepth        = 24;       // 16, 24, 32f
        double totalBeats      = 128.0;
    };
    Timeline timeline;

    // ── Tracks ───────────────────────────────────────────────────
    std::vector<std::unique_ptr<Track>> tracks;

    // ── Markers ──────────────────────────────────────────────────
    std::vector<ArrangementMarker> markers;

    // ── Snapshots ────────────────────────────────────────────────
    std::vector<MixerSnapshot> snapshots;
    int activeSnapshotIndex = -1;

    // ── Playback state (not saved) ────────────────────────────────
    double playheadBeat = 0.0;
    double loopStartBeat = 0.0;
    double loopEndBeat  = 8.0;
    bool   loopEnabled  = false;

    // ── Factory helpers ──────────────────────────────────────────
    static std::unique_ptr<Y2KSession> createEmpty();
    static std::unique_ptr<Y2KSession> createFromTemplate(const juce::String& templateName);

    // ── Track management ─────────────────────────────────────────
    Track* addTrack(Track::Type type, const juce::String& name = {});
    bool   removeTrack(const juce::Uuid& trackId);
    Track* findTrack(const juce::Uuid& trackId);
    int    getTrackIndex(const juce::Uuid& trackId) const;
    void   reorderTrack(int fromIdx, int toIdx);

    // ── I/O ──────────────────────────────────────────────────────
    juce::Result save(const juce::File& bundleDir);
    juce::Result load(const juce::File& bundleDir);

    // ── Validation ───────────────────────────────────────────────
    bool isValid() const;

    // ── Utilities ────────────────────────────────────────────────
    double beatsToSeconds(double beats) const noexcept {
        return beats * 60.0 / timeline.tempo;
    }
    double secondsToBeats(double seconds) const noexcept {
        return seconds * timeline.tempo / 60.0;
    }

private:
    void writeProjectJson(const juce::File& dest) const;
    bool readProjectJson(const juce::File& src);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Y2KSession)
};

} // namespace y2k::engine

#pragma once
// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_dsp/recording/audio_file_importer.h
//
//  AudioFileImporter — loads a WAV/AIFF/FLAC/MP3 file from disk
//  and produces an AudioClip ready to place on a track.
//
//  All operations are non-RT (disk I/O, format decoding).
//  Call from a background thread; never from the audio callback.
//
//  Supports:
//    WAV, AIFF, FLAC  — via JUCE built-in formats
//    MP3              — via JUCE (platform-dependent)
//
//  After import, the file is copied into the session bundle's
//  audio/ directory so the session is self-contained.
// ═══════════════════════════════════════════════════════════════════

#include "../../y2k_engine/session.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <functional>
#include <memory>

namespace y2k::dsp {

class AudioFileImporter {
public:
    struct ImportResult {
        bool                                  ok      = false;
        juce::String                          error;
        std::unique_ptr<y2k::engine::AudioClip> clip;

        // Metadata extracted from file
        double sampleRate   = 44100.0;
        int    numChannels  = 2;
        int64_t numSamples  = 0;
        double durationSec  = 0.0;
    };

    AudioFileImporter();
    ~AudioFileImporter() = default;

    // Import a file. Copies it into bundleAudioDir if non-empty.
    // The returned AudioClip has sourceFile pointing to the copy.
    ImportResult import(const juce::File& sourceFile,
                        const juce::File& bundleAudioDir = {},
                        double            placeBeat      = 0.0,
                        double            sessionBpm     = 120.0);

    // Quick check: is this file format supported?
    bool canImport(const juce::File& f) const;

    // Supported extensions (lowercase, with dot)
    static juce::StringArray supportedExtensions();

private:
    juce::AudioFormatManager formatManager_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioFileImporter)
};

} // namespace y2k::dsp

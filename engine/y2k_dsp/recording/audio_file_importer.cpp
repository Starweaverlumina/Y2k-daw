// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_dsp/recording/audio_file_importer.cpp
// ═══════════════════════════════════════════════════════════════════

#include "audio_file_importer.h"
#include <cmath>

namespace y2k::dsp {

AudioFileImporter::AudioFileImporter() {
    formatManager_.registerBasicFormats();
}

juce::StringArray AudioFileImporter::supportedExtensions() {
    return { ".wav", ".aif", ".aiff", ".flac", ".ogg", ".mp3" };
}

bool AudioFileImporter::canImport(const juce::File& f) const {
    const juce::String ext = f.getFileExtension().toLowerCase();
    for (const auto& e : supportedExtensions())
        if (ext == e) return true;
    return false;
}

AudioFileImporter::ImportResult AudioFileImporter::import(
    const juce::File& sourceFile,
    const juce::File& bundleAudioDir,
    double            placeBeat,
    double            sessionBpm)
{
    ImportResult result;

    if (!sourceFile.existsAsFile()) {
        result.error = "File not found: " + sourceFile.getFullPathName();
        return result;
    }
    if (!canImport(sourceFile)) {
        result.error = "Unsupported format: " + sourceFile.getFileExtension();
        return result;
    }

    // Open and read metadata
    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager_.createReaderFor(sourceFile));

    if (!reader) {
        result.error = "Could not open: " + sourceFile.getFileName();
        return result;
    }

    result.sampleRate  = reader->sampleRate;
    result.numChannels = static_cast<int>(reader->numChannels);
    result.numSamples  = static_cast<int64_t>(reader->lengthInSamples);
    result.durationSec = result.numSamples / result.sampleRate;

    // Copy into bundle if requested
    juce::File finalFile = sourceFile;
    if (bundleAudioDir != juce::File()) {
        bundleAudioDir.createDirectory();
        finalFile = bundleAudioDir.getChildFile(sourceFile.getFileName());

        // Avoid overwriting with different content — add suffix if needed
        if (finalFile.existsAsFile() &&
            finalFile.getSize() != sourceFile.getSize()) {
            finalFile = bundleAudioDir.getChildFile(
                sourceFile.getFileNameWithoutExtension()
                + "_" + juce::String(juce::Time::currentTimeMillis())
                + sourceFile.getFileExtension());
        }
        if (!finalFile.existsAsFile())
            sourceFile.copyFileTo(finalFile);
    }

    // Build AudioClip
    auto clip = std::make_unique<y2k::engine::AudioClip>();
    clip->name            = sourceFile.getFileNameWithoutExtension();
    clip->sourceFile      = finalFile;
    clip->sourceOffsetSec = 0.0;
    clip->startBeat       = placeBeat;

    // Duration in beats at session BPM
    const double durationBeats = result.durationSec * sessionBpm / 60.0;
    clip->durationBeats = durationBeats;

    // Round up to nearest bar for clean grid placement
    clip->durationBeats = std::ceil(durationBeats / 4.0) * 4.0;
    if (clip->durationBeats < durationBeats)
        clip->durationBeats = durationBeats;

    result.clip = std::move(clip);
    result.ok   = true;
    return result;
}

} // namespace y2k::dsp

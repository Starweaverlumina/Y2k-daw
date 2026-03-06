// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_engine/bounce_engine.cpp
//  BounceEngine — offline render to WAV.
// ═══════════════════════════════════════════════════════════════════
#include "bounce_engine.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>

namespace y2k::engine {

BounceEngine::BounceResult BounceEngine::bounce(y2k::dsp::ProcessGraph& graph,
                                                 const Params& params,
                                                 ProgressCallback onProgress)
{
    BounceResult result;
    result.file = params.outputFile;

    // ── Validate params ────────────────────────────────────────────
    if (params.totalBeats <= 0.0) {
        result.error = "totalBeats must be > 0";
        return result;
    }
    if (params.sampleRate <= 0.0) {
        result.error = "sampleRate must be > 0";
        return result;
    }
    if (params.blockSize <= 0) {
        result.error = "blockSize must be > 0";
        return result;
    }
    if (params.bpm <= 0.0) {
        result.error = "bpm must be > 0";
        return result;
    }
    if (params.numChannels < 1 || params.numChannels > 8) {
        result.error = "numChannels must be in [1, 8]";
        return result;
    }
    if (params.bitDepth != 16 && params.bitDepth != 24 && params.bitDepth != 32) {
        result.error = "bitDepth must be 16, 24, or 32";
        return result;
    }

    // ── Create output directory if needed ─────────────────────────
    const juce::File outputDir = params.outputFile.getParentDirectory();
    if (outputDir != juce::File() && !outputDir.exists()) {
        if (!outputDir.createDirectory()) {
            result.error = "Cannot create output directory: " + outputDir.getFullPathName();
            return result;
        }
    }

    // ── Calculate total samples ────────────────────────────────────
    // seconds = beats * 60 / bpm
    const double totalSeconds = params.totalBeats * 60.0 / params.bpm;
    const int64_t totalSamples = static_cast<int64_t>(
        std::ceil(totalSeconds * params.sampleRate));

    // ── Open WAV file writer ───────────────────────────────────────
    juce::WavAudioFormat wavFormat;

    auto fileStream = std::unique_ptr<juce::FileOutputStream>(
        params.outputFile.createOutputStream());
    if (!fileStream || fileStream->failedToOpen()) {
        result.error = "Cannot open output file: " + params.outputFile.getFullPathName();
        return result;
    }

    // juce::WavAudioFormat::createWriterFor takes ownership of the stream
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wavFormat.createWriterFor(
            fileStream.get(),
            params.sampleRate,
            static_cast<unsigned int>(params.numChannels),
            params.bitDepth,
            {},
            0));

    if (!writer) {
        result.error = "Cannot create WAV writer for: " + params.outputFile.getFullPathName();
        return result;
    }

    // Writer took ownership of the stream — release our ownership
    fileStream.release();

    // ── Prepare graph ──────────────────────────────────────────────
    y2k::dsp::ProcessSpec spec;
    spec.sampleRate   = params.sampleRate;
    spec.maxBlockSize = params.blockSize;
    spec.numChannels  = params.numChannels;
    graph.prepare(spec);  // calls commitState() internally — non-RT path

    // ── Scratch buffer ─────────────────────────────────────────────
    juce::AudioBuffer<float> renderBuf(params.numChannels, params.blockSize);

    // ── Render loop ────────────────────────────────────────────────
    // beatPosition advances by (blockSize / sampleRate) * (bpm / 60) per block.
    const double beatsPerSample = params.bpm / (60.0 * params.sampleRate);

    int64_t samplesDone    = 0;
    double  ppqPosition    = 0.0;

    while (samplesDone < totalSamples) {
        const int samplesToProcess = static_cast<int>(
            std::min(static_cast<int64_t>(params.blockSize),
                     totalSamples - samplesDone));

        renderBuf.clear();

        juce::MidiBuffer midi;

        y2k::dsp::ProcessContext ctx;
        ctx.sampleRate    = params.sampleRate;
        ctx.blockSize     = samplesToProcess;
        ctx.transportBPM  = params.bpm;
        ctx.isPlaying     = true;
        ctx.ppqPosition   = ppqPosition;
        ctx.midiBuffer    = &midi;
        ctx.outputBuffer  = &renderBuf;

        graph.processBlock(renderBuf, midi, ctx);

        // Write block to WAV
        if (!writer->writeFromAudioSampleBuffer(renderBuf, 0, samplesToProcess)) {
            result.error = "WAV write failed at sample " +
                           juce::String(static_cast<int64_t>(samplesDone));
            return result;
        }

        // Advance beat position
        ppqPosition  += static_cast<double>(samplesToProcess) * beatsPerSample;
        samplesDone  += samplesToProcess;

        // Progress callback
        if (onProgress) {
            const float progress = static_cast<float>(
                static_cast<double>(samplesDone) / static_cast<double>(totalSamples));
            onProgress(juce::jlimit(0.f, 1.f, progress));
        }
    }

    // ── Flush and close ────────────────────────────────────────────
    writer->flush();
    writer.reset();  // closes the WAV file

    result.ok          = true;
    result.durationSec = totalSeconds;
    result.file        = params.outputFile;

    if (onProgress)
        onProgress(1.0f);

    return result;
}

} // namespace y2k::engine

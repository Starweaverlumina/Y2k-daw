#pragma once
// engine/y2k_engine/bounce_engine.h
// BounceEngine — offline render session to WAV.
//
// Usage (UI thread, non-RT):
//   BounceEngine::Params p;
//   p.outputFile = File("~/Desktop/bounce.wav");
//   p.totalBeats = 64.0;
//   p.sampleRate = 48000.0;
//   p.blockSize  = 512;
//   p.bpm        = session.timeline.tempo;
//   auto result = BounceEngine::bounce(graph, p, progressCallback);
//
// Thread safety: call only from non-RT thread. Takes snapshot of graph state.

#include "../y2k_dsp/graph/processor_graph.h"
#include "session.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>

namespace y2k::engine {

class BounceEngine {
public:
    struct Params {
        juce::File   outputFile;
        double       totalBeats  = 64.0;
        double       sampleRate  = 48000.0;
        int          blockSize   = 512;
        double       bpm         = 120.0;
        int          numChannels = 2;
        int          bitDepth    = 24;
    };

    struct BounceResult {
        bool         ok          = false;
        juce::String error;
        juce::File   file;
        double       durationSec = 0.0;
    };

    using ProgressCallback = std::function<void(float progress01)>;

    // Offline render — blocking, runs entirely on calling thread.
    // Takes ownership of rendering; graph must not be touched by RT thread during this call.
    static BounceResult bounce(y2k::dsp::ProcessGraph& graph,
                               const Params& params,
                               ProgressCallback onProgress = {});
};

} // namespace y2k::engine

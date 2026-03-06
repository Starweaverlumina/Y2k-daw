#pragma once
// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_engine/transport_engine.h  — v8
//
//  TransportEngine — single source of truth for timing.
//
//  All timing derives from sample count, not wall clock.
//  Audio thread advances beat position from sample count.
//  UI thread reads beat position for display.
//
//  Thread model:
//    play/stop/reset/setBPM  — UI thread (atomic writes)
//    tick(numSamples)        — audio thread (called once per block)
//    beatPosition()          — any thread (atomic read)
//    barBeat()               — any thread (derived from beatPosition)
//
//  Tempo change is sample-accurate: the new BPM takes effect at the
//  exact sample where tick() is called with the new value already set.
//
//  Usage:
//    TransportEngine transport;
//    transport.setSampleRate(44100.0);
//    transport.setBPM(120.0);
//    transport.play();
//    ...
//    // In audio callback:
//    transport.tick(numSamples);
//    double beat = transport.beatPosition();
// ═══════════════════════════════════════════════════════════════════

#include <atomic>
#include <cmath>

namespace y2k::engine {

class TransportEngine {
public:
    TransportEngine() = default;

    // ── Non-RT configuration ──────────────────────────────────
    void setSampleRate(double sr) noexcept {
        sampleRate_.store(sr, std::memory_order_release);
    }

    void setBPM(double bpm) noexcept {
        if (bpm < 1.0) bpm = 1.0;
        if (bpm > 999.0) bpm = 999.0;
        bpm_.store(bpm, std::memory_order_release);
    }

    void play()  noexcept { playing_.store(true,  std::memory_order_release); }
    void stop()  noexcept { playing_.store(false, std::memory_order_release); }

    void reset() noexcept {
        beatPosition_.store(0.0, std::memory_order_release);
        samplePosition_.store(0,  std::memory_order_release);
    }

    void setLooping(bool loop, double loopStartBeat=0.0, double loopEndBeat=0.0) noexcept {
        loopStart_.store(loopStartBeat, std::memory_order_release);
        loopEnd_  .store(loopEndBeat,   std::memory_order_release);
        looping_  .store(loop,          std::memory_order_release);
    }

    // ── Audio thread only ─────────────────────────────────────
    //  Advance position by numSamples. Returns beat position at
    //  the START of this block (use this for scheduling within block).
    double tick(int numSamples) noexcept {
        const double beatAtBlockStart = beatPosition_.load(std::memory_order_relaxed);
        if (!playing_.load(std::memory_order_relaxed)) return beatAtBlockStart;

        const double sr  = sampleRate_.load(std::memory_order_relaxed);
        const double bpm = bpm_       .load(std::memory_order_relaxed);
        if (sr <= 0.0 || bpm <= 0.0) return beatAtBlockStart;

        const double beatsPerSample = bpm / (60.0 * sr);
        double newBeat = beatAtBlockStart + beatsPerSample * numSamples;

        // Loop handling
        if (looping_.load(std::memory_order_relaxed)) {
            const double lEnd = loopEnd_.load(std::memory_order_relaxed);
            const double lSta = loopStart_.load(std::memory_order_relaxed);
            if (lEnd > lSta && newBeat >= lEnd) {
                newBeat = lSta + std::fmod(newBeat - lSta, lEnd - lSta);
            }
        }

        beatPosition_  .store(newBeat,
                               std::memory_order_release);
        samplePosition_.fetch_add(static_cast<int64_t>(numSamples),
                                   std::memory_order_relaxed);
        return beatAtBlockStart;
    }

    // Sample-accurate: beat position at sample offset within current block
    double beatAt(int sampleOffsetInBlock, int blockSize) const noexcept {
        if (blockSize <= 0) return beatPosition_.load(std::memory_order_acquire);
        const double sr  = sampleRate_.load(std::memory_order_relaxed);
        const double bpm = bpm_       .load(std::memory_order_relaxed);
        if (sr <= 0.0 || bpm <= 0.0) return beatPosition_.load(std::memory_order_acquire);
        const double beat = beatPosition_.load(std::memory_order_acquire);
        const double beatsPerSample = bpm / (60.0 * sr);
        return beat + beatsPerSample * sampleOffsetInBlock;
    }

    // ── Any-thread reads ──────────────────────────────────────
    bool   isPlaying()     const noexcept { return playing_      .load(std::memory_order_acquire); }
    double beatPosition()  const noexcept { return beatPosition_ .load(std::memory_order_acquire); }
    double bpm()           const noexcept { return bpm_          .load(std::memory_order_acquire); }
    double sampleRate()    const noexcept { return sampleRate_   .load(std::memory_order_acquire); }
    int64_t samplePosition() const noexcept { return samplePosition_.load(std::memory_order_acquire); }

    // Bar/beat breakdown (4/4 time, bars 1-based)
    struct BarBeat {
        int    bar;
        int    beat;    // 1–4
        double ticks;   // fractional part × 960
    };
    BarBeat barBeat() const noexcept {
        const double b   = beatPosition_.load(std::memory_order_acquire);
        const int    bar = static_cast<int>(b / 4.0) + 1;
        const int    bt  = static_cast<int>(std::fmod(b, 4.0)) + 1;
        const double tk  = std::fmod(b, 1.0) * 960.0;
        return { bar, bt, tk };
    }

    // Beats-to-seconds for given beat offset at current BPM
    double beatsToSeconds(double beats) const noexcept {
        const double bpm = bpm_.load(std::memory_order_acquire);
        return (bpm > 0.0) ? (beats * 60.0 / bpm) : 0.0;
    }

private:
    std::atomic<double>  sampleRate_   {44100.0};
    std::atomic<double>  bpm_          {120.0};
    std::atomic<double>  beatPosition_ {0.0};
    std::atomic<int64_t> samplePosition_{0};
    std::atomic<bool>    playing_      {false};
    std::atomic<bool>    looping_      {false};
    std::atomic<double>  loopStart_    {0.0};
    std::atomic<double>  loopEnd_      {32.0};  // 8 bars default
};

} // namespace y2k::engine

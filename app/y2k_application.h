#pragma once
// ═══════════════════════════════════════════════════════════════════
//  app/y2k_application.h  — v11
// ═══════════════════════════════════════════════════════════════════
#include <juce_audio_devices/juce_audio_devices.h>
#include "../engine/y2k_engine/session/session_schema.h"
#include "../engine/y2k_engine/session/auto_save_manager.h"
#include "../engine/y2k_engine/session/session_command_bus.h"
#include <juce_gui_basics/juce_gui_basics.h>

#include "y2k_dsp/graph/processor_graph.h"
#include "y2k_dsp/generators/sine_osc.h"
#include "y2k_dsp/instruments/bondi_synth.h"
#include "y2k_dsp/instruments/sampler_player.h"
#include "y2k_dsp/effects/grape_compressor.h"
#include "y2k_dsp/mixer/track_gain.h"
#include "y2k_dsp/mixer/clip_scheduler.h"
#include "y2k_dsp/recording/audio_recorder.h"
#include "y2k_dsp/recording/midi_recorder.h"
#include "y2k_engine/session.h"
#include "y2k_engine/undo.h"
#include "y2k_engine/midi_learn.h"
#include "y2k_engine/automation_writer.h"
#include "y2k_engine/transport_engine.h"
#include "y2k_engine/bounce_engine.h"

namespace y2k::app {

class MainWindow;

// ═══════════════════════════════════════════════════════════════════
//  AudioEngine  v8
//
//  New in v8:
//    • TransportEngine owns beat position (sample-accurate)
//    • GraphState swap on graph edits (no audio-thread lock)
//    • ParameterEvents carry layer (base/auto/midi) → ParameterBank
//    • LockFreeQueue<MidiEvent> for noteOn/noteOff (no CriticalSection)
//    • Automation dispatches to automation layer, not base
// ═══════════════════════════════════════════════════════════════════
class AudioEngine : public juce::AudioIODeviceCallback {
public:
    AudioEngine();
    ~AudioEngine() override;

    void initialise(y2k::engine::Y2KSession* session,
                    y2k::engine::MidiLearnSystem* midiLearn);
    void shutdown();

    void audioDeviceIOCallbackWithContext(
        const float* const*, int,
        float* const*, int, int,
        const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

    y2k::dsp::ProcessGraph& graph() { return graph_; }
    y2k::engine::TransportEngine& transport() { return transport_; }

    void sessionChanged();

    // ── Transport (delegates to TransportEngine) ──────────────
    void   setPlaying(bool p)   noexcept { p ? transport_.play() : transport_.stop(); }
    void   setBPM(double bpm)   noexcept { transport_.setBPM(bpm); }
    bool   isPlaying()    const noexcept { return transport_.isPlaying(); }
    double bpm()          const noexcept { return transport_.bpm(); }
    double ppqPosition()  const noexcept { return transport_.beatPosition(); }
    void   rewind()       noexcept       { transport_.reset(); }

    // ── Parameter control (layer-aware) ───────────────────────
    void setTrackVolume(int i, float v) noexcept;
    void setTrackPan   (int i, float p) noexcept;
    void setTrackMuted (int i, bool m)  noexcept;
    void setToneFrequency(float hz)     noexcept;
    void setToneGain(float lin)         noexcept;

    // ── Note input (UI → lock-free queue → RT) ────────────────
    void noteOn (int ch, int note, float vel);
    void noteOff(int ch, int note);

    // ── Automation ────────────────────────────────────────────
    void loadAutomation(const y2k::engine::Track&);
    void generateAndLoadAutomation(y2k::engine::Track&);

    // ── Node IDs (for automation routing) ─────────────────────
    uint32_t sineOscNodeId()   const noexcept { return sineOscNodeId_;   }
    uint32_t trackGainNodeId() const noexcept { return trackGainNodeId_; }

    // ── MIDI Learn dispatch (called from audio thread via MidiLearnSystem) ─
    void midiLearnDispatch(const y2k::engine::MidiLearnMapping&, float v) noexcept;

    // --- v10: Session save / load ---
    juce::Result saveProject(const juce::File& destFile);
    juce::Result loadProject(const juce::File& srcFile);
    bool         recoveryAvailable(const juce::File& bundleDir) const noexcept;
    juce::Result loadRecovery(const juce::File& bundleDir);
    void         startAutoSave(const juce::File& bundleDir, int intervalSeconds = 30);
    void         stopAutoSave();
    y2k::engine::SessionCommandBus* commandBus() noexcept { return commandBus_.get(); }

    // --- v11: Recording ---
    void armAudioRecord(int numChannels = 2, int ringSeconds = 60);
    void startAudioRecord(double startBeat);
    void stopAudioRecord();
    juce::File flushAudioRecord(const juce::File& destDir, const juce::String& name);
    bool isRecording() const noexcept;

    void armMidiRecord();
    void startMidiRecord(double startBeat);
    void stopMidiRecord();
    std::unique_ptr<y2k::engine::MidiClip> flushMidiRecord(double clipStartBeat);

    // --- v11: Bounce to WAV ---
    y2k::engine::BounceEngine::BounceResult bounceToWav(
        const juce::File& outputFile,
        double totalBeats,
        y2k::engine::BounceEngine::ProgressCallback onProgress = {});

    // --- v11: Sampler ---
    // Load a sample into a key zone on a specific track's sampler (track index 2-7)
    void loadSamplerZone(int trackIdx, const y2k::dsp::SamplerPlayer::KeyZone& zone);
    void clearSamplerZones(int trackIdx);

    // --- v11: Compressor ---
    // Access the GrapeCompressor on a track (returns nullptr if track has no compressor)
    y2k::dsp::GrapeCompressor* getTrackCompressor(int trackIdx) noexcept;

    void setRouting(const y2k::engine::ProjectRouting& r);

private:
    static constexpr int kMaxTracks = 8;

    juce::AudioDeviceManager       deviceManager_;
    y2k::dsp::ProcessGraph         graph_;
    y2k::engine::TransportEngine   transport_;
    y2k::engine::Y2KSession*       session_    = nullptr;
    y2k::engine::MidiLearnSystem*  midiLearn_  = nullptr;

    // Track 0: SineOsc + BondiSynth (proof-of-life)
    y2k::dsp::SineOscProcessor*    sineOsc_                  = nullptr;
    y2k::dsp::BondiSynth*          synth_                    = nullptr;
    // Track 1-4: additional BondiSynth instances
    y2k::dsp::BondiSynth*          synthTracks_[4]           = {};
    // Track 5-7: SamplerPlayer instances
    y2k::dsp::SamplerPlayer*       samplers_[3]              = {};
    // Per-track GrapeCompressors (one per track)
    y2k::dsp::GrapeCompressor*     compressors_[kMaxTracks]  = {};
    // Per-track gain nodes
    y2k::dsp::TrackGainProcessor*  trackGains_[kMaxTracks]   = {};

    uint32_t sineOscNodeId_   = 0;
    uint32_t trackGainNodeId_ = 0;

    y2k::dsp::ClipScheduler       clipScheduler_;
    y2k::engine::AutomationPlayer automationPlayer_;

    juce::AudioBuffer<float>  scratchOutput_;

    // Lock-free MIDI queue: UI → audio thread
    y2k::dsp::LockFreeQueue<y2k::dsp::MidiEvent, 512> midiQueue_;

    double sampleRate_   = 44100.0;
    int    maxBlockSize_ = 512;

    void buildGraph();
    void dispatchAutomation(uint32_t nodeId, int paramId, float value) noexcept;
    void rewireCommandBus();

    // --- v10 session ---
    y2k::engine::ProjectRouting                     routing_;
    std::unique_ptr<y2k::engine::AutoSaveManager>   autoSave_;
    std::unique_ptr<y2k::engine::SessionCommandBus> commandBus_;

    // --- v11 recording ---
    y2k::dsp::AudioRecorder audioRecorder_;
    y2k::dsp::MidiRecorder  midiRecorder_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};

// ═══════════════════════════════════════════════════════════════════
//  Y2KApplication  v8
// ═══════════════════════════════════════════════════════════════════
class Y2KApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName()    override { return "Y2K DAW"; }
    const juce::String getApplicationVersion() override { return "0.8.0"; }
    bool moreThanOneInstanceAllowed()          override { return false; }

    void initialise(const juce::String&) override;
    void shutdown()                      override;
    void systemRequestedQuit()           override;
    void anotherInstanceStarted(const juce::String&) override;

    AudioEngine&                 getAudioEngine() { return audioEngine_; }
    y2k::engine::Y2KSession&     getSession()     { return *session_;    }
    y2k::engine::UndoManager&    getUndoManager() { return undoManager_; }
    y2k::engine::MidiLearnSystem& getMidiLearn()  { return midiLearn_;   }

    juce::Result saveSession(const juce::File&);
    juce::Result loadSession(const juce::File&);
    void newSession();
    void generateAutomation();

private:
    std::unique_ptr<MainWindow>              mainWindow_;
    std::unique_ptr<y2k::engine::Y2KSession> session_;
    y2k::engine::UndoManager                 undoManager_ {100};
    y2k::engine::MidiLearnSystem             midiLearn_;
    AudioEngine                              audioEngine_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Y2KApplication)
};

} // namespace y2k::app

// ═══════════════════════════════════════════════════════════════════
//  app/y2k_application.cpp  — v11
//  Wired: 8 real instrument tracks, GrapeCompressor per track,
//         SamplerPlayer on tracks 5-7, recording, bounce, command bus.
// ═══════════════════════════════════════════════════════════════════
#include "y2k_application.h"
#include "../engine/y2k_engine/session/session_schema.h"
#include "../engine/y2k_engine/session/auto_save_manager.h"
#include "../engine/y2k_engine/session/session_command_bus.h"
#include "../engine/y2k_engine/graph_synth/graph_compiler.h"
#include "main_window.h"

namespace y2k::app {

using PG = y2k::dsp::ProcessGraph;

// ═════════════════════════════════════════════════════════════════
//  AudioEngine
// ═════════════════════════════════════════════════════════════════
AudioEngine::AudioEngine()  = default;
AudioEngine::~AudioEngine() { shutdown(); }

// ─────────────────────────────────────────────────────────────────
//  buildGraph  [non-RT — called once on initialise]
//
//  Graph topology per track:
//    Instrument → GrapeCompressor → TrackGain → OUTPUT
//
//  Track 0  : SineOsc + BondiSynth (proof-of-life)
//  Track 1-4: BondiSynth instances
//  Track 5-7: SamplerPlayer instances
// ─────────────────────────────────────────────────────────────────
void AudioEngine::buildGraph() {
    graph_.clear();
    std::fill(std::begin(trackGains_),   std::end(trackGains_),   nullptr);
    std::fill(std::begin(compressors_),  std::end(compressors_),  nullptr);
    std::fill(std::begin(synthTracks_),  std::end(synthTracks_),  nullptr);
    std::fill(std::begin(samplers_),     std::end(samplers_),     nullptr);
    sineOsc_ = nullptr; synth_ = nullptr;
    sineOscNodeId_ = trackGainNodeId_ = 0;

    // Helper: add instrument → compressor → gain → OUTPUT chain
    auto addTrack = [&](std::unique_ptr<y2k::dsp::ProcessorNode> instr, int trackIdx) {
        auto* instrNode = graph_.addNode(std::move(instr));

        auto comp = std::make_unique<y2k::dsp::GrapeCompressor>();
        compressors_[trackIdx] = comp.get();
        auto* compNode = graph_.addNode(std::move(comp));

        auto gain = std::make_unique<y2k::dsp::TrackGainProcessor>(0.8f, 0.f);
        trackGains_[trackIdx] = gain.get();
        auto* gainNode = graph_.addNode(std::move(gain));

        if (instrNode && compNode && gainNode) {
            graph_.connect({ instrNode->nodeId, 0, compNode->nodeId, 0 });
            graph_.connect({ instrNode->nodeId, 1, compNode->nodeId, 1 });
            graph_.connect({ compNode->nodeId,  0, gainNode->nodeId, 0 });
            graph_.connect({ compNode->nodeId,  1, gainNode->nodeId, 1 });
            graph_.connect({ gainNode->nodeId,  0, PG::OUTPUT_NODE_ID, 0 });
            graph_.connect({ gainNode->nodeId,  1, PG::OUTPUT_NODE_ID, 1 });
            return gainNode->nodeId;
        }
        return (uint32_t)0;
    };

    // ── Track 0: SineOsc (proof-of-life) + BondiSynth ─────────────
    {
        auto osc = std::make_unique<y2k::dsp::SineOscProcessor>(440.f, 0.10f);
        sineOsc_ = osc.get();
        auto* oscNode = graph_.addNode(std::move(osc));
        if (oscNode) {
            sineOscNodeId_ = oscNode->nodeId;
            graph_.connect({ oscNode->nodeId, 0, PG::OUTPUT_NODE_ID, 0 });
            graph_.connect({ oscNode->nodeId, 1, PG::OUTPUT_NODE_ID, 1 });
        }

        auto synth = std::make_unique<y2k::dsp::BondiSynth>();
        synth_ = synth.get();
        trackGainNodeId_ = addTrack(std::move(synth), 0);
    }

    // ── Tracks 1-4: BondiSynth instances ──────────────────────────
    for (int i = 0; i < 4; ++i) {
        auto synth = std::make_unique<y2k::dsp::BondiSynth>();
        synthTracks_[i] = synth.get();
        addTrack(std::move(synth), i + 1);
    }

    // ── Tracks 5-7: SamplerPlayer instances ───────────────────────
    for (int i = 0; i < 3; ++i) {
        auto sampler = std::make_unique<y2k::dsp::SamplerPlayer>();
        samplers_[i] = sampler.get();
        addTrack(std::move(sampler), i + 5);
    }
    // Note: prepare() (called from audioDeviceAboutToStart) does commitState()
}

void AudioEngine::initialise(y2k::engine::Y2KSession* session,
                              y2k::engine::MidiLearnSystem* midiLearn) {
    session_   = session;
    midiLearn_ = midiLearn;

    buildGraph();

    if (session_)
        clipScheduler_.updateFromSession(*session_, transport_.bpm());

    const juce::String err = deviceManager_.initialise(0, 2, nullptr, true);
    if (err.isNotEmpty())
        juce::Logger::writeToLog("[Y2K] Audio init: " + err);

    deviceManager_.addAudioCallback(this);
}

void AudioEngine::shutdown() {
    deviceManager_.removeAudioCallback(this);
    deviceManager_.closeAudioDevice();
    audioRecorder_.disarm();
    midiRecorder_.disarm();
    graph_.clear();
    clipScheduler_.clear();
    automationPlayer_.clear();
    sineOsc_ = nullptr; synth_ = nullptr;
    std::fill(std::begin(trackGains_),  std::end(trackGains_),  nullptr);
    std::fill(std::begin(compressors_), std::end(compressors_), nullptr);
    std::fill(std::begin(synthTracks_), std::end(synthTracks_), nullptr);
    std::fill(std::begin(samplers_),    std::end(samplers_),    nullptr);
}

void AudioEngine::sessionChanged() {
    if (session_) clipScheduler_.updateFromSession(*session_, transport_.bpm());
}

void AudioEngine::loadAutomation(const y2k::engine::Track& t) {
    automationPlayer_.loadFromTrack(t);
}

void AudioEngine::generateAndLoadAutomation(y2k::engine::Track& t) {
    y2k::engine::AutomationWriterParams p;
    p.freqMin = 220.f; p.freqMax = 880.f;
    y2k::engine::AutomationWriter::generate(t, sineOscNodeId_, trackGainNodeId_, p);
    automationPlayer_.loadFromTrack(t);
}

// ─────────────────────────────────────────────────────────────────
//  dispatchAutomation  [RT — writes to automation layer, NOT base]
// ─────────────────────────────────────────────────────────────────
void AudioEngine::dispatchAutomation(uint32_t nodeId, int paramId, float value) noexcept {
    auto* node = graph_.getNode(nodeId);
    if (!node) return;
    float scaledValue = value;
    if (nodeId == sineOscNodeId_ && paramId == 0)
        scaledValue = 220.f + value * 660.f;
    if (paramId < node->getNumParameters())
        node->setParameterAutomation(paramId, scaledValue);
}

// ─────────────────────────────────────────────────────────────────
//  midiLearnDispatch  [RT — writes to midi layer]
// ─────────────────────────────────────────────────────────────────
void AudioEngine::midiLearnDispatch(const y2k::engine::MidiLearnMapping& m,
                                     float value01) noexcept
{
    using T = y2k::engine::MidiLearnTargetType;
    switch (m.targetType) {
        case T::TrackVolume:
            if (m.targetId>=0 && m.targetId<kMaxTracks && trackGains_[m.targetId])
                trackGains_[m.targetId]->setVolume(value01);
            break;
        case T::TrackPan:
            if (m.targetId>=0 && m.targetId<kMaxTracks && trackGains_[m.targetId])
                trackGains_[m.targetId]->setPan(value01*2.f-1.f);
            break;
        case T::GraphParam: {
            auto* node = graph_.getNode(sineOscNodeId_);
            if (node && m.paramId < node->getNumParameters())
                node->setParameterMidi(m.paramId, value01 * 660.f);
            if (m.paramId==6 && trackGains_[0]) trackGains_[0]->setVolume(value01);
            if (m.paramId==7 && trackGains_[0]) trackGains_[0]->setPan(value01*2.f-1.f);
            break;
        }
        default: break;
    }
}

// ─────────────────────────────────────────────────────────────────
//  audioDeviceAboutToStart  [non-RT]
// ─────────────────────────────────────────────────────────────────
void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device) {
    if (!device) return;
    sampleRate_   = device->getCurrentSampleRate();
    maxBlockSize_ = device->getCurrentBufferSizeSamples();
    const int outCh = std::max(2, device->getActiveOutputChannels().countNumberOfSetBits());

    scratchOutput_.setSize(outCh, maxBlockSize_, false, true, false);
    scratchOutput_.clear();

    y2k::dsp::ProcessSpec spec;
    spec.sampleRate   = sampleRate_;
    spec.maxBlockSize = maxBlockSize_;
    spec.numChannels  = outCh;
    graph_.prepare(spec);   // calls commitState() internally

    transport_.setSampleRate(sampleRate_);
    transport_.reset();
}

void AudioEngine::audioDeviceStopped() {
    scratchOutput_.setSize(0, 0);
    midiQueue_.clear();
}

// ─────────────────────────────────────────────────────────────────
//  audioDeviceIOCallbackWithContext  [RT — no allocation, no locks]
// ─────────────────────────────────────────────────────────────────
void AudioEngine::audioDeviceIOCallbackWithContext(
    const float* const* inputData,
    int            numInputChannels,
    float* const*  outputData,
    int            numOutputChannels,
    int            numSamples,
    const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;
    if (numOutputChannels<=0 || numSamples<=0) return;

    if (scratchOutput_.getNumChannels() < numOutputChannels ||
        scratchOutput_.getNumSamples()  < numSamples) {
        scratchOutput_.setSize(numOutputChannels, numSamples, false, true, true);
    }
    scratchOutput_.clear();

    // ── Drain UI→RT MIDI queue into juce::MidiBuffer ──────────
    juce::MidiBuffer midi;
    while (auto evt = midiQueue_.pop()) {
        if (evt->size > 0)
            midi.addEvent(evt->toJuce(), evt->sampleOffset);
    }

    // ── Recording: capture live input ─────────────────────────
    if (audioRecorder_.isRecording() && inputData && numInputChannels > 0)
        audioRecorder_.writeBlock(inputData, numInputChannels, numSamples);
    if (midiRecorder_.isRecording() && !midi.isEmpty()) {
        const double beatAtNow = transport_.beatPosition();
        midiRecorder_.processBlock(midi, beatAtNow, transport_.bpm(),
                                   numSamples, sampleRate_);
    }

    // ── MIDI Learn: process CC events ─────────────────────────
    if (midiLearn_ && !midi.isEmpty()) {
        midiLearn_->processMidi(midi, [this](const y2k::engine::MidiLearnMapping& m, float v){
            midiLearnDispatch(m, v);
        });
    }

    // ── Advance transport (sample-accurate) ───────────────────
    const double beatAtStart = transport_.tick(numSamples);
    const bool   isPlaying   = transport_.isPlaying();

    // ── Clip scheduler: inject MIDI from timeline ──────────────
    clipScheduler_.fillMidiBuffer(midi, beatAtStart, numSamples, sampleRate_, transport_.bpm(), isPlaying);

    // ── Automation: evaluate lanes → automation layer ──────────
    automationPlayer_.tick(beatAtStart, isPlaying,
        [this](uint32_t nid, int pid, float val){ dispatchAutomation(nid, pid, val); });

    // ── Build ProcessContext ────────────────────────────────────
    y2k::dsp::ProcessContext ctx;
    ctx.sampleRate   = sampleRate_;
    ctx.blockSize    = numSamples;
    ctx.transportBPM = transport_.bpm();
    ctx.isPlaying    = isPlaying;
    ctx.ppqPosition  = beatAtStart;
    ctx.midiBuffer   = &midi;
    ctx.outputBuffer = &scratchOutput_;

    // ── Render graph (lock-free state snapshot) ────────────────
    graph_.processBlock(scratchOutput_, midi, ctx);

    // ── Copy to device ─────────────────────────────────────────
    const int srcCh = scratchOutput_.getNumChannels();
    for (int ch = 0; ch < numOutputChannels; ++ch) {
        if (!outputData[ch]) continue;
        const int s = std::min(ch, srcCh-1);
        if (s < 0) juce::FloatVectorOperations::clear(outputData[ch], numSamples);
        else        juce::FloatVectorOperations::copy(outputData[ch],
                        scratchOutput_.getReadPointer(s), numSamples);
    }
}

// ── Controls ──────────────────────────────────────────────────────
void AudioEngine::setTrackVolume(int i, float v) noexcept {
    if (i>=0&&i<kMaxTracks&&trackGains_[i]) trackGains_[i]->setVolume(v);
}
void AudioEngine::setTrackPan(int i, float p) noexcept {
    if (i>=0&&i<kMaxTracks&&trackGains_[i]) trackGains_[i]->setPan(p);
}
void AudioEngine::setTrackMuted(int i, bool m) noexcept {
    if (i>=0&&i<kMaxTracks&&trackGains_[i]) trackGains_[i]->setMuted(m);
}
void AudioEngine::setToneFrequency(float hz) noexcept {
    if (sineOsc_) sineOsc_->setFrequency(hz);
}
void AudioEngine::setToneGain(float lin) noexcept {
    if (sineOsc_) sineOsc_->setGain(lin);
}

// Note events: UI → lock-free MIDI queue → RT
void AudioEngine::noteOn(int ch, int note, float vel) {
    midiQueue_.push(y2k::dsp::MidiEvent::noteOn(ch, note,
        juce::roundToInt(vel * 127.f)));
    if (sineOsc_) sineOsc_->noteOn(note, vel, ch);
}
void AudioEngine::noteOff(int ch, int note) {
    midiQueue_.push(y2k::dsp::MidiEvent::noteOff(ch, note));
    if (sineOsc_) sineOsc_->noteOff(note, ch);
}

// ─────────────────────────────────────────────────────────────────
//  v11: Recording
// ─────────────────────────────────────────────────────────────────
void AudioEngine::armAudioRecord(int numChannels, int ringSeconds) {
    audioRecorder_.~AudioRecorder();
    new (&audioRecorder_) y2k::dsp::AudioRecorder(numChannels, ringSeconds);
    audioRecorder_.arm(sampleRate_, maxBlockSize_);
}
void AudioEngine::startAudioRecord(double startBeat) {
    audioRecorder_.startCapture(startBeat);
}
void AudioEngine::stopAudioRecord() {
    audioRecorder_.stopCapture();
}
juce::File AudioEngine::flushAudioRecord(const juce::File& destDir, const juce::String& name) {
    return audioRecorder_.flushToDisk(destDir, name);
}
bool AudioEngine::isRecording() const noexcept {
    return audioRecorder_.isRecording();
}

void AudioEngine::armMidiRecord() {
    midiRecorder_.arm(sampleRate_);
}
void AudioEngine::startMidiRecord(double startBeat) {
    midiRecorder_.startCapture(startBeat, transport_.bpm());
}
void AudioEngine::stopMidiRecord() {
    midiRecorder_.stopCapture();
}
std::unique_ptr<y2k::engine::MidiClip> AudioEngine::flushMidiRecord(double clipStartBeat) {
    return midiRecorder_.buildClip(clipStartBeat);
}

// ─────────────────────────────────────────────────────────────────
//  v11: Bounce
// ─────────────────────────────────────────────────────────────────
y2k::engine::BounceEngine::BounceResult AudioEngine::bounceToWav(
    const juce::File& outputFile,
    double totalBeats,
    y2k::engine::BounceEngine::ProgressCallback onProgress)
{
    y2k::engine::BounceEngine::Params p;
    p.outputFile  = outputFile;
    p.totalBeats  = totalBeats;
    p.sampleRate  = sampleRate_ > 0 ? sampleRate_ : 48000.0;
    p.blockSize   = 512;
    p.bpm         = transport_.bpm();
    p.numChannels = 2;
    p.bitDepth    = 24;
    return y2k::engine::BounceEngine::bounce(graph_, p, std::move(onProgress));
}

// ─────────────────────────────────────────────────────────────────
//  v11: Sampler zones
// ─────────────────────────────────────────────────────────────────
void AudioEngine::loadSamplerZone(int trackIdx, const y2k::dsp::SamplerPlayer::KeyZone& zone) {
    const int si = trackIdx - 5;
    if (si >= 0 && si < 3 && samplers_[si])
        samplers_[si]->loadZone(zone);
}
void AudioEngine::clearSamplerZones(int trackIdx) {
    const int si = trackIdx - 5;
    if (si >= 0 && si < 3 && samplers_[si])
        samplers_[si]->clearZones();
}

// ─────────────────────────────────────────────────────────────────
//  v11: Compressor access
// ─────────────────────────────────────────────────────────────────
y2k::dsp::GrapeCompressor* AudioEngine::getTrackCompressor(int trackIdx) noexcept {
    if (trackIdx >= 0 && trackIdx < kMaxTracks)
        return compressors_[trackIdx];
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────
//  setRouting  — applies routing changes from SessionCommandBus
// ─────────────────────────────────────────────────────────────────
void AudioEngine::setRouting(const y2k::engine::ProjectRouting& r) {
    // Mirror track volume/pan/mute from routing model to DSP
    for (int i = 0; i < std::min((int)r.tracks.size(), kMaxTracks); ++i) {
        const auto& tm = r.tracks[i];
        setTrackVolume(i, tm.volume);
        setTrackPan   (i, tm.pan);
        setTrackMuted (i, tm.muted);
    }
}

// ═════════════════════════════════════════════════════════════════
//  Y2KApplication
// ═════════════════════════════════════════════════════════════════
void Y2KApplication::initialise(const juce::String&) {
    newSession();
    audioEngine_.initialise(session_.get(), &midiLearn_);

    if (!session_->tracks.empty())
        audioEngine_.generateAndLoadAutomation(*session_->tracks[0]);

    mainWindow_ = std::make_unique<MainWindow>(getApplicationName(), *this);
}

void Y2KApplication::shutdown() {
    mainWindow_.reset();
    audioEngine_.shutdown();
}

void Y2KApplication::systemRequestedQuit() { quit(); }
void Y2KApplication::anotherInstanceStarted(const juce::String&) {
    if (mainWindow_) mainWindow_->toFront(true);
}

void Y2KApplication::newSession() {
    session_ = y2k::engine::Y2KSession::createFromTemplate("Y2K Session");
    undoManager_.clear();
    audioEngine_.sessionChanged();
}

void Y2KApplication::generateAutomation() {
    if (!session_->tracks.empty())
        audioEngine_.generateAndLoadAutomation(*session_->tracks[0]);
}

juce::Result Y2KApplication::saveSession(const juce::File& dir) {
    return session_ ? session_->save(dir) : juce::Result::fail("No session");
}
juce::Result Y2KApplication::loadSession(const juce::File& dir) {
    auto s = std::make_unique<y2k::engine::Y2KSession>();
    auto r = s->load(dir);
    if (r.wasOk()) {
        session_ = std::move(s);
        undoManager_.clear();
        audioEngine_.sessionChanged();
        if (!session_->tracks.empty())
            audioEngine_.loadAutomation(*session_->tracks[0]);
    }
    return r;
}

// ─────────────────────────────────────────────────────────────────
//  AudioEngine v10/v11: Session save / load / auto-save / command bus
// ─────────────────────────────────────────────────────────────────
juce::Result AudioEngine::saveProject(const juce::File& destFile) {
    if (!session_) return juce::Result::fail("No session loaded");
    auto result = y2k::engine::SessionSchema::atomicSave(*session_, routing_, destFile);
    if (result.wasOk() && autoSave_) autoSave_->onFullSaveCompleted();
    return result;
}

juce::Result AudioEngine::loadProject(const juce::File& srcFile) {
    auto xml = juce::XmlDocument::parse(srcFile);
    if (!xml) return juce::Result::fail("Cannot parse: " + srcFile.getFullPathName());
    auto lr = y2k::engine::SessionSchema::deserialise(juce::ValueTree::fromXml(*xml));
    if (!lr.status.wasOk()) return lr.status;
    if (lr.migrationLog.isNotEmpty())
        juce::Logger::writeToLog("[Session] Migration:\n" + lr.migrationLog);
    if (lr.session) session_ = std::move(lr.session);
    routing_ = std::move(lr.routing);
    setRouting(routing_);
    rewireCommandBus();
    return juce::Result::ok();
}

bool AudioEngine::recoveryAvailable(const juce::File& bundleDir) const noexcept {
    return y2k::engine::AutoSaveManager::recoveryAvailable(bundleDir);
}

juce::Result AudioEngine::loadRecovery(const juce::File& bundleDir) {
    auto lr = y2k::engine::AutoSaveManager::loadRecovery(bundleDir);
    if (!lr.status.wasOk()) return lr.status;
    if (lr.session) session_ = std::move(lr.session);
    routing_ = std::move(lr.routing);
    setRouting(routing_);
    rewireCommandBus();
    return juce::Result::ok();
}

void AudioEngine::startAutoSave(const juce::File& bundleDir, int intervalSeconds) {
    autoSave_ = std::make_unique<y2k::engine::AutoSaveManager>(intervalSeconds);
    autoSave_->start(
        bundleDir,
        [this]() -> const y2k::engine::Y2KSession*     { return session_.get(); },
        [this]() -> const y2k::engine::ProjectRouting*  { return &routing_; });
    rewireCommandBus();
}

void AudioEngine::stopAutoSave() {
    if (autoSave_) autoSave_->stop();
    autoSave_.reset();
}

void AudioEngine::rewireCommandBus() {
    commandBus_ = std::make_unique<y2k::engine::SessionCommandBus>(
        *session_, routing_, undoManager_, autoSave_.get());
    commandBus_->onRoutingChanged = [this] { setRouting(routing_); };
}

} // namespace y2k::app

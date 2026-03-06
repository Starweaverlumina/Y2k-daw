// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_engine/midi_learn.cpp
// ═══════════════════════════════════════════════════════════════════

#include "midi_learn.h"
#include <algorithm>

namespace y2k::engine {

// ─────────────────────────────────────────────────────────────────
//  MidiLearnMapping serialization
// ─────────────────────────────────────────────────────────────────
juce::ValueTree MidiLearnMapping::toValueTree() const {
    juce::ValueTree vt("MidiLearnMapping");
    vt.setProperty("targetType", static_cast<int>(targetType), nullptr);
    vt.setProperty("targetId",   targetId,                     nullptr);
    vt.setProperty("paramId",    paramId,                      nullptr);
    vt.setProperty("ccNumber",   ccNumber,                     nullptr);
    vt.setProperty("channel",    channel,                      nullptr);
    vt.setProperty("label",      label,                        nullptr);
    return vt;
}

MidiLearnMapping MidiLearnMapping::fromValueTree(const juce::ValueTree& vt) {
    MidiLearnMapping m;
    m.targetType = static_cast<MidiLearnTargetType>(
                       static_cast<int>(vt.getProperty("targetType", 0)));
    m.targetId  = static_cast<int>(vt.getProperty("targetId",  0));
    m.paramId   = static_cast<int>(vt.getProperty("paramId",   0));
    m.ccNumber  = static_cast<int>(vt.getProperty("ccNumber",  -1));
    m.channel   = static_cast<int>(vt.getProperty("channel",   0));
    m.label     = vt.getProperty("label", "").toString();
    return m;
}

// ─────────────────────────────────────────────────────────────────
//  UI thread methods
// ─────────────────────────────────────────────────────────────────
void MidiLearnSystem::armLearn(MidiLearnTargetType type, int targetId,
                                int paramId, const juce::String& label)
{
    std::lock_guard<std::mutex> lk(mutex_);
    armedTarget_.targetType = type;
    armedTarget_.targetId   = targetId;
    armedTarget_.paramId    = paramId;
    armedTarget_.ccNumber   = -1;
    armedTarget_.label      = label.isEmpty()
        ? ("Param " + juce::String(paramId)) : label;
    armed_.store(true, std::memory_order_release);
}

void MidiLearnSystem::cancelLearn() noexcept {
    armed_.store(false, std::memory_order_release);
}

void MidiLearnSystem::addMapping(const MidiLearnMapping& m) {
    std::lock_guard<std::mutex> lk(mutex_);
    // Remove any existing mapping for the same CC+channel
    mappings_.erase(
        std::remove_if(mappings_.begin(), mappings_.end(),
            [&](const MidiLearnMapping& x) {
                return x.ccNumber == m.ccNumber &&
                       (x.channel == m.channel || m.channel == 0);
            }),
        mappings_.end());
    mappings_.push_back(m);
}

bool MidiLearnSystem::removeMapping(int ccNumber, int channel) {
    std::lock_guard<std::mutex> lk(mutex_);
    const auto before = mappings_.size();
    mappings_.erase(
        std::remove_if(mappings_.begin(), mappings_.end(),
            [&](const MidiLearnMapping& x) {
                return x.ccNumber == ccNumber &&
                       (channel == 0 || x.channel == channel);
            }),
        mappings_.end());
    return mappings_.size() < before;
}

void MidiLearnSystem::clearAllMappings() {
    std::lock_guard<std::mutex> lk(mutex_);
    mappings_.clear();
}

// ─────────────────────────────────────────────────────────────────
//  processMidi  [audio thread — RT-safe]
// ─────────────────────────────────────────────────────────────────
void MidiLearnSystem::processMidi(const juce::MidiBuffer& midi,
                                   const MidiLearnDispatch& dispatch) noexcept
{
    if (midi.isEmpty()) return;

    // Non-blocking lock — skip if UI thread holds it
    if (!mutex_.try_lock()) return;

    for (const auto meta : midi) {
        const juce::MidiMessage msg = meta.getMessage();
        if (!msg.isController()) continue;

        const int cc  = msg.getControllerNumber();
        const int ch  = msg.getChannel();
        const float v = static_cast<float>(msg.getControllerValue()) / 127.f;

        // If armed, bind this CC
        if (armed_.load(std::memory_order_relaxed)) {
            armedTarget_.ccNumber = cc;
            armedTarget_.channel  = ch;
            // Commit to mapping list (replace any existing on this CC)
            mappings_.erase(
                std::remove_if(mappings_.begin(), mappings_.end(),
                    [cc, ch](const MidiLearnMapping& x) {
                        return x.ccNumber == cc &&
                               (x.channel == ch || x.channel == 0);
                    }),
                mappings_.end());
            mappings_.push_back(armedTarget_);
            armed_.store(false, std::memory_order_relaxed);
        }

        // Dispatch to all bound params
        for (const auto& m : mappings_) {
            if (m.ccNumber != cc) continue;
            if (m.channel != 0 && m.channel != ch) continue;
            if (dispatch) dispatch(m, v);
        }
    }

    mutex_.unlock();
}

// ─────────────────────────────────────────────────────────────────
//  Persistence
// ─────────────────────────────────────────────────────────────────
juce::ValueTree MidiLearnSystem::saveToValueTree() const {
    juce::ValueTree vt("MidiLearn");
    std::lock_guard<std::mutex> lk(mutex_);
    for (const auto& m : mappings_)
        vt.appendChild(m.toValueTree(), nullptr);
    return vt;
}

void MidiLearnSystem::loadFromValueTree(const juce::ValueTree& vt) {
    std::lock_guard<std::mutex> lk(mutex_);
    mappings_.clear();
    for (int i = 0; i < vt.getNumChildren(); ++i) {
        auto m = MidiLearnMapping::fromValueTree(vt.getChild(i));
        if (m.isBound()) mappings_.push_back(m);
    }
}

} // namespace y2k::engine

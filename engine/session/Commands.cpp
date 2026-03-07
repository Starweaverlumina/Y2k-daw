// ═══════════════════════════════════════════════════════════════════
//  engine/session/Commands.cpp
//
//  Implementations of all Command subclasses declared in Commands.h.
//
//  Pattern for every command:
//    1. apply()  — capture old state on first invocation (captured_ guard),
//                  write new state, fire appropriate notification.
//    2. revert() — restore old state, fire appropriate notification.
//
//  Notification rules:
//    fireRoutingChanged() — topology changed: track/bus add/remove,
//                           send rewired, insert added/removed.
//    fireSessionChanged() — non-topology change: volume, pan, mute, name.
// ═══════════════════════════════════════════════════════════════════

#include "Commands.h"
#include "SessionDocument.h"

#include <juce_core/juce_core.h>
#include <algorithm>
#include <cassert>

namespace y2k::session {

// ═══════════════════════════════════════════════════════════════════
//  Internal helpers
// ═══════════════════════════════════════════════════════════════════

namespace {

// Find a bus by string UUID. Returns &masterBus when busId is empty.
y2k::engine::BusModel* findBusById(SessionDocument& doc,
                                    const juce::String& busId) noexcept
{
    auto& r = doc.mutableRouting();
    if (busId.isEmpty())
        return &r.masterBus;
    if (auto* b = r.findBus(juce::Uuid(busId)))
        return b;
    return nullptr;
}

y2k::engine::SendSlot* findSend(SessionDocument& doc,
                                 const juce::String& trackId,
                                 int sendIndex) noexcept
{
    auto* t = doc.findTrack(trackId);
    if (!t) return nullptr;
    if (sendIndex < 0 || sendIndex >= static_cast<int>(t->sends.size()))
        return nullptr;
    return &t->sends[static_cast<size_t>(sendIndex)];
}

y2k::engine::InsertSlot* findInsert(SessionDocument& doc,
                                     const juce::String& trackId,
                                     int insertIndex) noexcept
{
    auto* t = doc.findTrack(trackId);
    if (!t) return nullptr;
    if (insertIndex < 0 || insertIndex >= static_cast<int>(t->inserts.size()))
        return nullptr;
    return &t->inserts[static_cast<size_t>(insertIndex)];
}

} // namespace

// ═══════════════════════════════════════════════════════════════════
//  AddTrackCommand
// ═══════════════════════════════════════════════════════════════════

AddTrackCommand::AddTrackCommand(juce::String name, juce::String instrumentId)
    : name_(std::move(name))
    , instrumentId_(std::move(instrumentId))
    , assignedId_()   // generates a fresh UUID
{}

void AddTrackCommand::apply(SessionDocument& doc)
{
    y2k::engine::TrackModel t;
    t.id           = assignedId_;
    t.name         = name_;
    t.instrumentId = instrumentId_;
    t.inputType    = y2k::engine::TrackInputType::Instrument;
    doc.mutableRouting().tracks.push_back(std::move(t));
    doc.fireRoutingChanged();
}

void AddTrackCommand::revert(SessionDocument& doc)
{
    auto& tracks = doc.mutableRouting().tracks;
    const juce::String idStr = assignedId_.toString();
    tracks.erase(std::remove_if(tracks.begin(), tracks.end(),
        [&](const y2k::engine::TrackModel& t) {
            return t.id.toString() == idStr;
        }), tracks.end());
    doc.fireRoutingChanged();
}

std::string AddTrackCommand::description() const
{
    return "Add Track: " + name_.toStdString();
}

// ═══════════════════════════════════════════════════════════════════
//  RemoveTrackCommand
// ═══════════════════════════════════════════════════════════════════

RemoveTrackCommand::RemoveTrackCommand(juce::String trackId)
    : trackId_(std::move(trackId))
{}

void RemoveTrackCommand::apply(SessionDocument& doc)
{
    auto& tracks = doc.mutableRouting().tracks;
    for (int i = 0; i < static_cast<int>(tracks.size()); ++i) {
        if (tracks[static_cast<size_t>(i)].id.toString() == trackId_) {
            if (!captured_) {
                snapshot_       = tracks[static_cast<size_t>(i)]; // copy
                insertionIndex_ = i;
                captured_       = true;
            }
            tracks.erase(tracks.begin() + i);
            doc.fireRoutingChanged();
            return;
        }
    }
}

void RemoveTrackCommand::revert(SessionDocument& doc)
{
    if (!captured_) return;
    auto& tracks = doc.mutableRouting().tracks;
    const int clampedIdx = juce::jlimit(0,
        static_cast<int>(tracks.size()), insertionIndex_);
    tracks.insert(tracks.begin() + clampedIdx, snapshot_);
    doc.fireRoutingChanged();
}

std::string RemoveTrackCommand::description() const
{
    return "Remove Track";
}

// ═══════════════════════════════════════════════════════════════════
//  SetTrackVolumeCommand
// ═══════════════════════════════════════════════════════════════════

SetTrackVolumeCommand::SetTrackVolumeCommand(juce::String trackId, float newVolume)
    : trackId_(std::move(trackId)), newVolume_(newVolume)
{}

void SetTrackVolumeCommand::apply(SessionDocument& doc)
{
    if (auto* t = doc.findTrack(trackId_)) {
        if (!captured_) { oldVolume_ = t->volume; captured_ = true; }
        t->volume = juce::jlimit(0.f, 1.f, newVolume_);
        doc.fireSessionChanged();
    }
}

void SetTrackVolumeCommand::revert(SessionDocument& doc)
{
    if (auto* t = doc.findTrack(trackId_)) {
        t->volume = oldVolume_;
        doc.fireSessionChanged();
    }
}

std::string SetTrackVolumeCommand::description() const
{
    return "Set Volume";
}

bool SetTrackVolumeCommand::canMerge(const Command& next) const noexcept
{
    if (auto* n = dynamic_cast<const SetTrackVolumeCommand*>(&next))
        return n->trackId_ == trackId_;
    return false;
}

void SetTrackVolumeCommand::mergeWith(std::unique_ptr<Command> next)
{
    // Absorb the target value from the newer command; keep our oldVolume_.
    newVolume_ = static_cast<SetTrackVolumeCommand*>(next.get())->newVolume_;
}

// ═══════════════════════════════════════════════════════════════════
//  SetTrackPanCommand
// ═══════════════════════════════════════════════════════════════════

SetTrackPanCommand::SetTrackPanCommand(juce::String trackId, float newPan)
    : trackId_(std::move(trackId)), newPan_(newPan)
{}

void SetTrackPanCommand::apply(SessionDocument& doc)
{
    if (auto* t = doc.findTrack(trackId_)) {
        if (!captured_) { oldPan_ = t->pan; captured_ = true; }
        t->pan = juce::jlimit(-1.f, 1.f, newPan_);
        doc.fireSessionChanged();
    }
}

void SetTrackPanCommand::revert(SessionDocument& doc)
{
    if (auto* t = doc.findTrack(trackId_)) {
        t->pan = oldPan_;
        doc.fireSessionChanged();
    }
}

std::string SetTrackPanCommand::description() const { return "Set Pan"; }

bool SetTrackPanCommand::canMerge(const Command& next) const noexcept
{
    if (auto* n = dynamic_cast<const SetTrackPanCommand*>(&next))
        return n->trackId_ == trackId_;
    return false;
}

void SetTrackPanCommand::mergeWith(std::unique_ptr<Command> next)
{
    newPan_ = static_cast<SetTrackPanCommand*>(next.get())->newPan_;
}

// ═══════════════════════════════════════════════════════════════════
//  SetTrackMutedCommand
// ═══════════════════════════════════════════════════════════════════

SetTrackMutedCommand::SetTrackMutedCommand(juce::String trackId, bool muted)
    : trackId_(std::move(trackId)), newMuted_(muted)
{}

void SetTrackMutedCommand::apply(SessionDocument& doc)
{
    if (auto* t = doc.findTrack(trackId_)) {
        if (!captured_) { oldMuted_ = t->muted; captured_ = true; }
        t->muted = newMuted_;
        doc.fireSessionChanged();
    }
}

void SetTrackMutedCommand::revert(SessionDocument& doc)
{
    if (auto* t = doc.findTrack(trackId_)) {
        t->muted = oldMuted_;
        doc.fireSessionChanged();
    }
}

std::string SetTrackMutedCommand::description() const
{
    return newMuted_ ? "Mute Track" : "Unmute Track";
}

// ═══════════════════════════════════════════════════════════════════
//  SetTrackSoloedCommand
// ═══════════════════════════════════════════════════════════════════

SetTrackSoloedCommand::SetTrackSoloedCommand(juce::String trackId, bool soloed)
    : trackId_(std::move(trackId)), newSoloed_(soloed)
{}

void SetTrackSoloedCommand::apply(SessionDocument& doc)
{
    if (auto* t = doc.findTrack(trackId_)) {
        if (!captured_) { oldSoloed_ = t->soloed; captured_ = true; }
        t->soloed = newSoloed_;
        doc.fireSessionChanged();
    }
}

void SetTrackSoloedCommand::revert(SessionDocument& doc)
{
    if (auto* t = doc.findTrack(trackId_)) {
        t->soloed = oldSoloed_;
        doc.fireSessionChanged();
    }
}

std::string SetTrackSoloedCommand::description() const
{
    return newSoloed_ ? "Solo Track" : "Unsolo Track";
}

// ═══════════════════════════════════════════════════════════════════
//  SetTrackNameCommand
// ═══════════════════════════════════════════════════════════════════

SetTrackNameCommand::SetTrackNameCommand(juce::String trackId, juce::String newName)
    : trackId_(std::move(trackId)), newName_(std::move(newName))
{}

void SetTrackNameCommand::apply(SessionDocument& doc)
{
    if (auto* t = doc.findTrack(trackId_)) {
        if (!captured_) { oldName_ = t->name; captured_ = true; }
        t->name = newName_;
        doc.fireSessionChanged();
    }
}

void SetTrackNameCommand::revert(SessionDocument& doc)
{
    if (auto* t = doc.findTrack(trackId_)) {
        t->name = oldName_;
        doc.fireSessionChanged();
    }
}

std::string SetTrackNameCommand::description() const
{
    return "Rename Track";
}

// ═══════════════════════════════════════════════════════════════════
//  SetTrackInstrumentCommand
// ═══════════════════════════════════════════════════════════════════

SetTrackInstrumentCommand::SetTrackInstrumentCommand(juce::String trackId,
                                                      juce::String newInstrumentId)
    : trackId_(std::move(trackId))
    , newInstrumentId_(std::move(newInstrumentId))
{}

void SetTrackInstrumentCommand::apply(SessionDocument& doc)
{
    if (auto* t = doc.findTrack(trackId_)) {
        if (!captured_) { oldInstrumentId_ = t->instrumentId; captured_ = true; }
        t->instrumentId = newInstrumentId_;
        t->inputType    = y2k::engine::TrackInputType::Instrument;
        doc.fireRoutingChanged();
    }
}

void SetTrackInstrumentCommand::revert(SessionDocument& doc)
{
    if (auto* t = doc.findTrack(trackId_)) {
        t->instrumentId = oldInstrumentId_;
        doc.fireRoutingChanged();
    }
}

std::string SetTrackInstrumentCommand::description() const
{
    return "Change Instrument";
}

// ═══════════════════════════════════════════════════════════════════
//  AddBusCommand
// ═══════════════════════════════════════════════════════════════════

AddBusCommand::AddBusCommand(juce::String name)
    : name_(std::move(name)), assignedId_()
{}

void AddBusCommand::apply(SessionDocument& doc)
{
    y2k::engine::BusModel b;
    b.id       = assignedId_;
    b.name     = name_;
    b.isMaster = false;
    doc.mutableRouting().buses.push_back(std::move(b));
    doc.fireRoutingChanged();
}

void AddBusCommand::revert(SessionDocument& doc)
{
    auto& buses = doc.mutableRouting().buses;
    const juce::String idStr = assignedId_.toString();
    buses.erase(std::remove_if(buses.begin(), buses.end(),
        [&](const y2k::engine::BusModel& b) {
            return b.id.toString() == idStr;
        }), buses.end());
    doc.fireRoutingChanged();
}

std::string AddBusCommand::description() const
{
    return "Add Bus: " + name_.toStdString();
}

// ═══════════════════════════════════════════════════════════════════
//  RemoveBusCommand
// ═══════════════════════════════════════════════════════════════════

RemoveBusCommand::RemoveBusCommand(juce::String busId)
    : busId_(std::move(busId))
{}

void RemoveBusCommand::apply(SessionDocument& doc)
{
    auto& buses = doc.mutableRouting().buses;
    for (int i = 0; i < static_cast<int>(buses.size()); ++i) {
        if (buses[static_cast<size_t>(i)].id.toString() == busId_) {
            if (!captured_) {
                snapshot_       = buses[static_cast<size_t>(i)];
                insertionIndex_ = i;
                captured_       = true;
            }
            buses.erase(buses.begin() + i);
            doc.fireRoutingChanged();
            return;
        }
    }
}

void RemoveBusCommand::revert(SessionDocument& doc)
{
    if (!captured_) return;
    auto& buses = doc.mutableRouting().buses;
    const int clampedIdx = juce::jlimit(0,
        static_cast<int>(buses.size()), insertionIndex_);
    buses.insert(buses.begin() + clampedIdx, snapshot_);
    doc.fireRoutingChanged();
}

std::string RemoveBusCommand::description() const { return "Remove Bus"; }

// ═══════════════════════════════════════════════════════════════════
//  SetBusVolumeCommand
// ═══════════════════════════════════════════════════════════════════

SetBusVolumeCommand::SetBusVolumeCommand(juce::String busId, float newVolume)
    : busId_(std::move(busId)), newVolume_(newVolume)
{}

y2k::engine::BusModel* SetBusVolumeCommand::findBus(SessionDocument& doc) const noexcept
{
    return findBusById(doc, busId_);
}

void SetBusVolumeCommand::apply(SessionDocument& doc)
{
    if (auto* b = findBus(doc)) {
        if (!captured_) { oldVolume_ = b->volume; captured_ = true; }
        b->volume = juce::jlimit(0.f, 1.f, newVolume_);
        doc.fireSessionChanged();
    }
}

void SetBusVolumeCommand::revert(SessionDocument& doc)
{
    if (auto* b = findBus(doc)) {
        b->volume = oldVolume_;
        doc.fireSessionChanged();
    }
}

std::string SetBusVolumeCommand::description() const
{
    return busId_.isEmpty() ? "Set Master Volume" : "Set Bus Volume";
}

bool SetBusVolumeCommand::canMerge(const Command& next) const noexcept
{
    if (auto* n = dynamic_cast<const SetBusVolumeCommand*>(&next))
        return n->busId_ == busId_;
    return false;
}

void SetBusVolumeCommand::mergeWith(std::unique_ptr<Command> next)
{
    newVolume_ = static_cast<SetBusVolumeCommand*>(next.get())->newVolume_;
}

// ═══════════════════════════════════════════════════════════════════
//  SetBusMutedCommand
// ═══════════════════════════════════════════════════════════════════

SetBusMutedCommand::SetBusMutedCommand(juce::String busId, bool muted)
    : busId_(std::move(busId)), newMuted_(muted)
{}

void SetBusMutedCommand::apply(SessionDocument& doc)
{
    if (auto* b = findBusById(doc, busId_)) {
        if (!captured_) { oldMuted_ = b->muted; captured_ = true; }
        b->muted = newMuted_;
        doc.fireSessionChanged();
    }
}

void SetBusMutedCommand::revert(SessionDocument& doc)
{
    if (auto* b = findBusById(doc, busId_)) {
        b->muted = oldMuted_;
        doc.fireSessionChanged();
    }
}

std::string SetBusMutedCommand::description() const
{
    return newMuted_ ? "Mute Bus" : "Unmute Bus";
}

// ═══════════════════════════════════════════════════════════════════
//  SetBusNameCommand
// ═══════════════════════════════════════════════════════════════════

SetBusNameCommand::SetBusNameCommand(juce::String busId, juce::String newName)
    : busId_(std::move(busId)), newName_(std::move(newName))
{}

void SetBusNameCommand::apply(SessionDocument& doc)
{
    if (auto* b = findBusById(doc, busId_)) {
        if (!captured_) { oldName_ = b->name; captured_ = true; }
        b->name = newName_;
        doc.fireSessionChanged();
    }
}

void SetBusNameCommand::revert(SessionDocument& doc)
{
    if (auto* b = findBusById(doc, busId_)) {
        b->name = oldName_;
        doc.fireSessionChanged();
    }
}

std::string SetBusNameCommand::description() const { return "Rename Bus"; }

// ═══════════════════════════════════════════════════════════════════
//  AddSendCommand
// ═══════════════════════════════════════════════════════════════════

AddSendCommand::AddSendCommand(juce::String trackId, juce::Uuid targetBusId,
                               float level, bool preFader)
    : trackId_(std::move(trackId))
    , targetBusId_(targetBusId)
    , level_(level)
    , preFader_(preFader)
{}

void AddSendCommand::apply(SessionDocument& doc)
{
    if (auto* t = doc.findTrack(trackId_)) {
        y2k::engine::SendSlot s;
        s.targetBusId = targetBusId_;
        s.level       = juce::jlimit(0.f, 1.f, level_);
        s.preFader    = preFader_;
        s.enabled     = true;
        insertedIndex_ = static_cast<int>(t->sends.size());
        t->sends.push_back(std::move(s));
        doc.fireRoutingChanged();
    }
}

void AddSendCommand::revert(SessionDocument& doc)
{
    if (auto* t = doc.findTrack(trackId_)) {
        if (insertedIndex_ >= 0 &&
            insertedIndex_ < static_cast<int>(t->sends.size())) {
            t->sends.erase(t->sends.begin() + insertedIndex_);
        }
        doc.fireRoutingChanged();
    }
}

std::string AddSendCommand::description() const { return "Add Send"; }

// ═══════════════════════════════════════════════════════════════════
//  RemoveSendCommand
// ═══════════════════════════════════════════════════════════════════

RemoveSendCommand::RemoveSendCommand(juce::String trackId, int sendIndex)
    : trackId_(std::move(trackId)), sendIndex_(sendIndex)
{}

void RemoveSendCommand::apply(SessionDocument& doc)
{
    if (auto* t = doc.findTrack(trackId_)) {
        if (sendIndex_ < 0 || sendIndex_ >= static_cast<int>(t->sends.size()))
            return;
        if (!captured_) {
            snapshot_ = t->sends[static_cast<size_t>(sendIndex_)];
            captured_ = true;
        }
        t->sends.erase(t->sends.begin() + sendIndex_);
        doc.fireRoutingChanged();
    }
}

void RemoveSendCommand::revert(SessionDocument& doc)
{
    if (!captured_) return;
    if (auto* t = doc.findTrack(trackId_)) {
        const int clampedIdx = juce::jlimit(0,
            static_cast<int>(t->sends.size()), sendIndex_);
        t->sends.insert(t->sends.begin() + clampedIdx, snapshot_);
        doc.fireRoutingChanged();
    }
}

std::string RemoveSendCommand::description() const { return "Remove Send"; }

// ═══════════════════════════════════════════════════════════════════
//  ChangeSendLevelCommand
// ═══════════════════════════════════════════════════════════════════

ChangeSendLevelCommand::ChangeSendLevelCommand(juce::String trackId,
                                               int sendIndex, float newLevel)
    : trackId_(std::move(trackId)), sendIndex_(sendIndex), newLevel_(newLevel)
{}

void ChangeSendLevelCommand::apply(SessionDocument& doc)
{
    if (auto* s = findSend(doc, trackId_, sendIndex_)) {
        if (!captured_) { oldLevel_ = s->level; captured_ = true; }
        s->level = juce::jlimit(0.f, 1.f, newLevel_);
        doc.fireSessionChanged();
    }
}

void ChangeSendLevelCommand::revert(SessionDocument& doc)
{
    if (auto* s = findSend(doc, trackId_, sendIndex_)) {
        s->level = oldLevel_;
        doc.fireSessionChanged();
    }
}

std::string ChangeSendLevelCommand::description() const { return "Set Send Level"; }

bool ChangeSendLevelCommand::canMerge(const Command& next) const noexcept
{
    if (auto* n = dynamic_cast<const ChangeSendLevelCommand*>(&next))
        return n->trackId_ == trackId_ && n->sendIndex_ == sendIndex_;
    return false;
}

void ChangeSendLevelCommand::mergeWith(std::unique_ptr<Command> next)
{
    newLevel_ = static_cast<ChangeSendLevelCommand*>(next.get())->newLevel_;
}

// ═══════════════════════════════════════════════════════════════════
//  SetSendPreFaderCommand
// ═══════════════════════════════════════════════════════════════════

SetSendPreFaderCommand::SetSendPreFaderCommand(juce::String trackId,
                                               int sendIndex, bool preFader)
    : trackId_(std::move(trackId))
    , sendIndex_(sendIndex)
    , newPreFader_(preFader)
{}

void SetSendPreFaderCommand::apply(SessionDocument& doc)
{
    if (auto* s = findSend(doc, trackId_, sendIndex_)) {
        if (!captured_) { oldPreFader_ = s->preFader; captured_ = true; }
        s->preFader = newPreFader_;
        doc.fireRoutingChanged();
    }
}

void SetSendPreFaderCommand::revert(SessionDocument& doc)
{
    if (auto* s = findSend(doc, trackId_, sendIndex_)) {
        s->preFader = oldPreFader_;
        doc.fireRoutingChanged();
    }
}

std::string SetSendPreFaderCommand::description() const
{
    return newPreFader_ ? "Pre-Fader Send" : "Post-Fader Send";
}

// ═══════════════════════════════════════════════════════════════════
//  SetSendEnabledCommand
// ═══════════════════════════════════════════════════════════════════

SetSendEnabledCommand::SetSendEnabledCommand(juce::String trackId,
                                              int sendIndex, bool enabled)
    : trackId_(std::move(trackId))
    , sendIndex_(sendIndex)
    , newEnabled_(enabled)
{}

void SetSendEnabledCommand::apply(SessionDocument& doc)
{
    if (auto* s = findSend(doc, trackId_, sendIndex_)) {
        if (!captured_) { oldEnabled_ = s->enabled; captured_ = true; }
        s->enabled = newEnabled_;
        doc.fireSessionChanged();
    }
}

void SetSendEnabledCommand::revert(SessionDocument& doc)
{
    if (auto* s = findSend(doc, trackId_, sendIndex_)) {
        s->enabled = oldEnabled_;
        doc.fireSessionChanged();
    }
}

std::string SetSendEnabledCommand::description() const
{
    return newEnabled_ ? "Enable Send" : "Disable Send";
}

// ═══════════════════════════════════════════════════════════════════
//  AddInsertCommand
// ═══════════════════════════════════════════════════════════════════

AddInsertCommand::AddInsertCommand(juce::String trackId, juce::String typeId,
                                   int position)
    : trackId_(std::move(trackId))
    , typeId_(std::move(typeId))
    , requestedPos_(position)
{}

void AddInsertCommand::apply(SessionDocument& doc)
{
    if (auto* t = doc.findTrack(trackId_)) {
        y2k::engine::InsertSlot ins;
        ins.typeId   = typeId_;
        ins.bypassed = false;

        const int chainSize = static_cast<int>(t->inserts.size());
        if (requestedPos_ < 0 || requestedPos_ >= chainSize) {
            // Append at end
            actualPos_ = chainSize;
            t->inserts.push_back(std::move(ins));
        } else {
            actualPos_ = requestedPos_;
            t->inserts.insert(t->inserts.begin() + actualPos_, std::move(ins));
        }
        doc.fireRoutingChanged();
    }
}

void AddInsertCommand::revert(SessionDocument& doc)
{
    if (auto* t = doc.findTrack(trackId_)) {
        if (actualPos_ >= 0 && actualPos_ < static_cast<int>(t->inserts.size()))
            t->inserts.erase(t->inserts.begin() + actualPos_);
        doc.fireRoutingChanged();
    }
}

std::string AddInsertCommand::description() const
{
    return "Add Insert: " + typeId_.toStdString();
}

// ═══════════════════════════════════════════════════════════════════
//  RemoveInsertCommand
// ═══════════════════════════════════════════════════════════════════

RemoveInsertCommand::RemoveInsertCommand(juce::String trackId, int insertIndex)
    : trackId_(std::move(trackId)), insertIndex_(insertIndex)
{}

void RemoveInsertCommand::apply(SessionDocument& doc)
{
    if (auto* t = doc.findTrack(trackId_)) {
        if (insertIndex_ < 0 || insertIndex_ >= static_cast<int>(t->inserts.size()))
            return;
        if (!captured_) {
            snapshot_ = t->inserts[static_cast<size_t>(insertIndex_)];
            captured_ = true;
        }
        t->inserts.erase(t->inserts.begin() + insertIndex_);
        doc.fireRoutingChanged();
    }
}

void RemoveInsertCommand::revert(SessionDocument& doc)
{
    if (!captured_) return;
    if (auto* t = doc.findTrack(trackId_)) {
        const int clampedIdx = juce::jlimit(0,
            static_cast<int>(t->inserts.size()), insertIndex_);
        t->inserts.insert(t->inserts.begin() + clampedIdx, snapshot_);
        doc.fireRoutingChanged();
    }
}

std::string RemoveInsertCommand::description() const { return "Remove Insert"; }

// ═══════════════════════════════════════════════════════════════════
//  SetInsertBypassedCommand
// ═══════════════════════════════════════════════════════════════════

SetInsertBypassedCommand::SetInsertBypassedCommand(juce::String trackId,
                                                    int insertIndex, bool bypassed)
    : trackId_(std::move(trackId))
    , insertIndex_(insertIndex)
    , newBypassed_(bypassed)
{}

void SetInsertBypassedCommand::apply(SessionDocument& doc)
{
    if (auto* ins = findInsert(doc, trackId_, insertIndex_)) {
        if (!captured_) { oldBypassed_ = ins->bypassed; captured_ = true; }
        ins->bypassed = newBypassed_;
        doc.fireRoutingChanged();
    }
}

void SetInsertBypassedCommand::revert(SessionDocument& doc)
{
    if (auto* ins = findInsert(doc, trackId_, insertIndex_)) {
        ins->bypassed = oldBypassed_;
        doc.fireRoutingChanged();
    }
}

std::string SetInsertBypassedCommand::description() const
{
    return newBypassed_ ? "Bypass Insert" : "Enable Insert";
}

// ═══════════════════════════════════════════════════════════════════
//  MoveInsertCommand
// ═══════════════════════════════════════════════════════════════════

MoveInsertCommand::MoveInsertCommand(juce::String trackId, int fromIndex, int toIndex)
    : trackId_(std::move(trackId))
    , fromIndex_(fromIndex)
    , toIndex_(toIndex)
{}

static void doMoveInsert(SessionDocument& doc, const juce::String& trackId,
                          int from, int to)
{
    auto* t = doc.findTrack(trackId);
    if (!t) return;
    const int sz = static_cast<int>(t->inserts.size());
    if (from < 0 || from >= sz || to < 0 || to >= sz || from == to) return;

    // Rotate to achieve in-place move
    auto& v = t->inserts;
    if (from < to)
        std::rotate(v.begin() + from, v.begin() + from + 1, v.begin() + to + 1);
    else
        std::rotate(v.begin() + to, v.begin() + from, v.begin() + from + 1);

    doc.fireRoutingChanged();
}

void MoveInsertCommand::apply(SessionDocument& doc)
{
    doMoveInsert(doc, trackId_, fromIndex_, toIndex_);
}

void MoveInsertCommand::revert(SessionDocument& doc)
{
    // Reverse: move from toIndex_ back to fromIndex_
    doMoveInsert(doc, trackId_, toIndex_, fromIndex_);
}

std::string MoveInsertCommand::description() const { return "Reorder Insert"; }

// ═══════════════════════════════════════════════════════════════════
//  SetTempoCommand
// ═══════════════════════════════════════════════════════════════════

SetTempoCommand::SetTempoCommand(double newTempo)
    : newTempo_(newTempo)
{}

void SetTempoCommand::apply(SessionDocument& doc)
{
    if (!captured_) { oldTempo_ = doc.mutableMetadata().tempo; captured_ = true; }
    doc.mutableMetadata().tempo = juce::jlimit(20.0, 999.0, newTempo_);
    doc.fireSessionChanged();
}

void SetTempoCommand::revert(SessionDocument& doc)
{
    doc.mutableMetadata().tempo = oldTempo_;
    doc.fireSessionChanged();
}

std::string SetTempoCommand::description() const
{
    return "Set Tempo " + std::to_string(static_cast<int>(newTempo_)) + " BPM";
}

bool SetTempoCommand::canMerge(const Command& next) const noexcept
{
    return dynamic_cast<const SetTempoCommand*>(&next) != nullptr;
}

void SetTempoCommand::mergeWith(std::unique_ptr<Command> next)
{
    newTempo_ = static_cast<SetTempoCommand*>(next.get())->newTempo_;
}

// ═══════════════════════════════════════════════════════════════════
//  SetTimeSigCommand
// ═══════════════════════════════════════════════════════════════════

SetTimeSigCommand::SetTimeSigCommand(int numerator, int denominator)
    : newNum_(numerator), newDen_(denominator)
{}

void SetTimeSigCommand::apply(SessionDocument& doc)
{
    auto& m = doc.mutableMetadata();
    if (!captured_) { oldNum_ = m.timeSigNum; oldDen_ = m.timeSigDen; captured_ = true; }
    m.timeSigNum = juce::jmax(1, newNum_);
    m.timeSigDen = juce::jmax(1, newDen_);
    doc.fireSessionChanged();
}

void SetTimeSigCommand::revert(SessionDocument& doc)
{
    auto& m = doc.mutableMetadata();
    m.timeSigNum = oldNum_;
    m.timeSigDen = oldDen_;
    doc.fireSessionChanged();
}

std::string SetTimeSigCommand::description() const
{
    return "Set Time Signature " + std::to_string(newNum_)
         + "/" + std::to_string(newDen_);
}

// ═══════════════════════════════════════════════════════════════════
//  SetProjectNameCommand
// ═══════════════════════════════════════════════════════════════════

SetProjectNameCommand::SetProjectNameCommand(juce::String newName)
    : newName_(std::move(newName))
{}

void SetProjectNameCommand::apply(SessionDocument& doc)
{
    if (!captured_) { oldName_ = doc.mutableMetadata().projectName; captured_ = true; }
    doc.mutableMetadata().projectName = newName_;
    doc.fireSessionChanged();
}

void SetProjectNameCommand::revert(SessionDocument& doc)
{
    doc.mutableMetadata().projectName = oldName_;
    doc.fireSessionChanged();
}

std::string SetProjectNameCommand::description() const
{
    return "Rename Project";
}

// ═══════════════════════════════════════════════════════════════════
//  SetAuthorCommand
// ═══════════════════════════════════════════════════════════════════

SetAuthorCommand::SetAuthorCommand(juce::String newAuthor)
    : newAuthor_(std::move(newAuthor))
{}

void SetAuthorCommand::apply(SessionDocument& doc)
{
    if (!captured_) { oldAuthor_ = doc.mutableMetadata().author; captured_ = true; }
    doc.mutableMetadata().author = newAuthor_;
    doc.fireSessionChanged();
}

void SetAuthorCommand::revert(SessionDocument& doc)
{
    doc.mutableMetadata().author = oldAuthor_;
    doc.fireSessionChanged();
}

std::string SetAuthorCommand::description() const { return "Set Author"; }

// ═══════════════════════════════════════════════════════════════════
//  SetSampleRateCommand
// ═══════════════════════════════════════════════════════════════════

SetSampleRateCommand::SetSampleRateCommand(int newSampleRate)
    : newRate_(newSampleRate)
{}

void SetSampleRateCommand::apply(SessionDocument& doc)
{
    if (!captured_) { oldRate_ = doc.mutableMetadata().sampleRate; captured_ = true; }
    doc.mutableMetadata().sampleRate = newRate_;
    doc.fireSessionChanged();
}

void SetSampleRateCommand::revert(SessionDocument& doc)
{
    doc.mutableMetadata().sampleRate = oldRate_;
    doc.fireSessionChanged();
}

std::string SetSampleRateCommand::description() const
{
    return "Set Sample Rate " + std::to_string(newRate_) + " Hz";
}

} // namespace y2k::session

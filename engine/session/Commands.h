#pragma once
// ═══════════════════════════════════════════════════════════════════
//  engine/session/Commands.h
//
//  Concrete Command subclasses for all SessionDocument mutations.
//
//  Each command:
//    • Captures old state at apply() time (first call only).
//    • apply()  — mutates document toward new state.
//    • revert() — restores document to captured old state.
//    • Fires the appropriate notification on the document.
//
//  All commands are header-declared; implementations are in Commands.cpp.
//
//  ── Included commands ────────────────────────────────────────────
//
//  Track commands:
//    AddTrackCommand          — add a new instrument track
//    RemoveTrackCommand       — remove an existing track
//    SetTrackVolumeCommand    — change fader level (supports merge)
//    SetTrackPanCommand       — change pan position (supports merge)
//    SetTrackMutedCommand     — toggle mute
//    SetTrackSoloedCommand    — toggle solo
//    SetTrackNameCommand      — rename a track
//    SetTrackInstrumentCommand — swap the instrument type
//
//  Bus commands:
//    AddBusCommand            — add an auxiliary bus
//    RemoveBusCommand         — remove a bus (after un-wiring sends)
//    SetBusVolumeCommand      — change bus fader (supports merge)
//    SetBusMutedCommand       — toggle bus mute
//    SetBusNameCommand        — rename a bus
//
//  Send commands:
//    AddSendCommand           — add a send from track to bus
//    RemoveSendCommand        — remove a send
//    ChangeSendLevelCommand   — adjust send level (supports merge)
//    SetSendPreFaderCommand   — toggle pre/post fader
//    SetSendEnabledCommand    — enable/disable send
//
//  Insert commands:
//    AddInsertCommand         — add a processor to a track's insert chain
//    RemoveInsertCommand      — remove an insert
//    SetInsertBypassedCommand — toggle insert bypass
//    MoveInsertCommand        — reorder insert within chain
//
//  Metadata / Timeline commands:
//    SetTempoCommand          — change BPM (supports merge)
//    SetTimeSigCommand        — change time signature
//    SetProjectNameCommand    — rename the project
//    SetAuthorCommand         — change author field
//    SetSampleRateCommand     — change sample rate
// ═══════════════════════════════════════════════════════════════════

#include "UndoStack.h"
#include "SessionDocument.h"
#include "../y2k_engine/mixer/track_model.h"

#include <juce_core/juce_core.h>
#include <string>
#include <cmath>

namespace y2k::session {

// ─────────────────────────────────────────────────────────────────
//  AddTrackCommand
// ─────────────────────────────────────────────────────────────────
class AddTrackCommand final : public Command {
public:
    // name         — display name shown in the mixer strip
    // instrumentId — processor type string ("BondiSynth", "SineOsc", …)
    AddTrackCommand(juce::String name, juce::String instrumentId);

    void apply(SessionDocument& doc)  override;
    void revert(SessionDocument& doc) override;
    std::string description()         const override;

private:
    juce::String name_;
    juce::String instrumentId_;
    juce::Uuid   assignedId_;   // stable across undo/redo
};

// ─────────────────────────────────────────────────────────────────
//  RemoveTrackCommand
// ─────────────────────────────────────────────────────────────────
class RemoveTrackCommand final : public Command {
public:
    explicit RemoveTrackCommand(juce::String trackId);

    void apply(SessionDocument& doc)  override;
    void revert(SessionDocument& doc) override;
    std::string description()         const override;

private:
    juce::String trackId_;
    // Captured on first apply() for undo restoration
    bool                          captured_ = false;
    y2k::engine::TrackModel       snapshot_;  // full copy for restoration
    int                           insertionIndex_ = -1; // position in vector
};

// ─────────────────────────────────────────────────────────────────
//  SetTrackVolumeCommand
// ─────────────────────────────────────────────────────────────────
class SetTrackVolumeCommand final : public Command {
public:
    // newVolume — linear 0..1
    SetTrackVolumeCommand(juce::String trackId, float newVolume);

    void apply(SessionDocument& doc)  override;
    void revert(SessionDocument& doc) override;
    std::string description()         const override;

    // Merge consecutive volume changes on the same track into one entry
    bool canMerge(const Command& next) const noexcept override;
    void mergeWith(std::unique_ptr<Command> next)     override;

private:
    juce::String trackId_;
    float        oldVolume_ = 0.f;
    float        newVolume_;
    bool         captured_  = false;
};

// ─────────────────────────────────────────────────────────────────
//  SetTrackPanCommand
// ─────────────────────────────────────────────────────────────────
class SetTrackPanCommand final : public Command {
public:
    // newPan — -1 (hard left) … 0 (centre) … +1 (hard right)
    SetTrackPanCommand(juce::String trackId, float newPan);

    void apply(SessionDocument& doc)  override;
    void revert(SessionDocument& doc) override;
    std::string description()         const override;

    bool canMerge(const Command& next) const noexcept override;
    void mergeWith(std::unique_ptr<Command> next)     override;

private:
    juce::String trackId_;
    float        oldPan_   = 0.f;
    float        newPan_;
    bool         captured_ = false;
};

// ─────────────────────────────────────────────────────────────────
//  SetTrackMutedCommand
// ─────────────────────────────────────────────────────────────────
class SetTrackMutedCommand final : public Command {
public:
    SetTrackMutedCommand(juce::String trackId, bool muted);

    void apply(SessionDocument& doc)  override;
    void revert(SessionDocument& doc) override;
    std::string description()         const override;

private:
    juce::String trackId_;
    bool         oldMuted_ = false;
    bool         newMuted_;
    bool         captured_ = false;
};

// ─────────────────────────────────────────────────────────────────
//  SetTrackSoloedCommand
// ─────────────────────────────────────────────────────────────────
class SetTrackSoloedCommand final : public Command {
public:
    SetTrackSoloedCommand(juce::String trackId, bool soloed);

    void apply(SessionDocument& doc)  override;
    void revert(SessionDocument& doc) override;
    std::string description()         const override;

private:
    juce::String trackId_;
    bool         oldSoloed_ = false;
    bool         newSoloed_;
    bool         captured_  = false;
};

// ─────────────────────────────────────────────────────────────────
//  SetTrackNameCommand
// ─────────────────────────────────────────────────────────────────
class SetTrackNameCommand final : public Command {
public:
    SetTrackNameCommand(juce::String trackId, juce::String newName);

    void apply(SessionDocument& doc)  override;
    void revert(SessionDocument& doc) override;
    std::string description()         const override;

private:
    juce::String trackId_;
    juce::String oldName_;
    juce::String newName_;
    bool         captured_ = false;
};

// ─────────────────────────────────────────────────────────────────
//  SetTrackInstrumentCommand
// ─────────────────────────────────────────────────────────────────
class SetTrackInstrumentCommand final : public Command {
public:
    SetTrackInstrumentCommand(juce::String trackId, juce::String newInstrumentId);

    void apply(SessionDocument& doc)  override;
    void revert(SessionDocument& doc) override;
    std::string description()         const override;

private:
    juce::String trackId_;
    juce::String oldInstrumentId_;
    juce::String newInstrumentId_;
    bool         captured_ = false;
};

// ─────────────────────────────────────────────────────────────────
//  AddBusCommand
// ─────────────────────────────────────────────────────────────────
class AddBusCommand final : public Command {
public:
    explicit AddBusCommand(juce::String name);

    void apply(SessionDocument& doc)  override;
    void revert(SessionDocument& doc) override;
    std::string description()         const override;

    // Returns the UUID assigned to the created bus (stable across undo/redo)
    juce::Uuid assignedId() const noexcept { return assignedId_; }

private:
    juce::String name_;
    juce::Uuid   assignedId_;
};

// ─────────────────────────────────────────────────────────────────
//  RemoveBusCommand
// ─────────────────────────────────────────────────────────────────
class RemoveBusCommand final : public Command {
public:
    explicit RemoveBusCommand(juce::String busId);

    void apply(SessionDocument& doc)  override;
    void revert(SessionDocument& doc) override;
    std::string description()         const override;

private:
    juce::String              busId_;
    bool                      captured_ = false;
    y2k::engine::BusModel     snapshot_;
    int                       insertionIndex_ = -1;
};

// ─────────────────────────────────────────────────────────────────
//  SetBusVolumeCommand
// ─────────────────────────────────────────────────────────────────
class SetBusVolumeCommand final : public Command {
public:
    // busId — empty string targets the master bus
    SetBusVolumeCommand(juce::String busId, float newVolume);

    void apply(SessionDocument& doc)  override;
    void revert(SessionDocument& doc) override;
    std::string description()         const override;

    bool canMerge(const Command& next) const noexcept override;
    void mergeWith(std::unique_ptr<Command> next)     override;

private:
    juce::String busId_;      // empty = master bus
    float        oldVolume_ = 1.f;
    float        newVolume_;
    bool         captured_  = false;

    y2k::engine::BusModel* findBus(SessionDocument& doc) const noexcept;
};

// ─────────────────────────────────────────────────────────────────
//  SetBusMutedCommand
// ─────────────────────────────────────────────────────────────────
class SetBusMutedCommand final : public Command {
public:
    SetBusMutedCommand(juce::String busId, bool muted);

    void apply(SessionDocument& doc)  override;
    void revert(SessionDocument& doc) override;
    std::string description()         const override;

private:
    juce::String busId_;
    bool         oldMuted_ = false;
    bool         newMuted_;
    bool         captured_ = false;
};

// ─────────────────────────────────────────────────────────────────
//  SetBusNameCommand
// ─────────────────────────────────────────────────────────────────
class SetBusNameCommand final : public Command {
public:
    SetBusNameCommand(juce::String busId, juce::String newName);

    void apply(SessionDocument& doc)  override;
    void revert(SessionDocument& doc) override;
    std::string description()         const override;

private:
    juce::String busId_;
    juce::String oldName_;
    juce::String newName_;
    bool         captured_ = false;
};

// ─────────────────────────────────────────────────────────────────
//  AddSendCommand
// ─────────────────────────────────────────────────────────────────
class AddSendCommand final : public Command {
public:
    // trackId    — source track
    // targetBusId — destination bus UUID
    // level       — initial send level 0..1
    // preFader    — true = pre-fader tap
    AddSendCommand(juce::String trackId,
                   juce::Uuid   targetBusId,
                   float        level    = 1.f,
                   bool         preFader = false);

    void apply(SessionDocument& doc)  override;
    void revert(SessionDocument& doc) override;
    std::string description()         const override;

private:
    juce::String trackId_;
    juce::Uuid   targetBusId_;
    float        level_;
    bool         preFader_;
    int          insertedIndex_ = -1; // recorded on apply, used by revert
};

// ─────────────────────────────────────────────────────────────────
//  RemoveSendCommand
// ─────────────────────────────────────────────────────────────────
class RemoveSendCommand final : public Command {
public:
    // sendIndex — position in TrackModel::sends
    RemoveSendCommand(juce::String trackId, int sendIndex);

    void apply(SessionDocument& doc)  override;
    void revert(SessionDocument& doc) override;
    std::string description()         const override;

private:
    juce::String               trackId_;
    int                        sendIndex_;
    bool                       captured_ = false;
    y2k::engine::SendSlot      snapshot_;
};

// ─────────────────────────────────────────────────────────────────
//  ChangeSendLevelCommand
// ─────────────────────────────────────────────────────────────────
class ChangeSendLevelCommand final : public Command {
public:
    ChangeSendLevelCommand(juce::String trackId, int sendIndex, float newLevel);

    void apply(SessionDocument& doc)  override;
    void revert(SessionDocument& doc) override;
    std::string description()         const override;

    bool canMerge(const Command& next) const noexcept override;
    void mergeWith(std::unique_ptr<Command> next)     override;

private:
    juce::String trackId_;
    int          sendIndex_;
    float        oldLevel_  = 1.f;
    float        newLevel_;
    bool         captured_  = false;
};

// ─────────────────────────────────────────────────────────────────
//  SetSendPreFaderCommand
// ─────────────────────────────────────────────────────────────────
class SetSendPreFaderCommand final : public Command {
public:
    SetSendPreFaderCommand(juce::String trackId, int sendIndex, bool preFader);

    void apply(SessionDocument& doc)  override;
    void revert(SessionDocument& doc) override;
    std::string description()         const override;

private:
    juce::String trackId_;
    int          sendIndex_;
    bool         oldPreFader_ = false;
    bool         newPreFader_;
    bool         captured_    = false;
};

// ─────────────────────────────────────────────────────────────────
//  SetSendEnabledCommand
// ─────────────────────────────────────────────────────────────────
class SetSendEnabledCommand final : public Command {
public:
    SetSendEnabledCommand(juce::String trackId, int sendIndex, bool enabled);

    void apply(SessionDocument& doc)  override;
    void revert(SessionDocument& doc) override;
    std::string description()         const override;

private:
    juce::String trackId_;
    int          sendIndex_;
    bool         oldEnabled_ = true;
    bool         newEnabled_;
    bool         captured_   = false;
};

// ─────────────────────────────────────────────────────────────────
//  AddInsertCommand
// ─────────────────────────────────────────────────────────────────
class AddInsertCommand final : public Command {
public:
    // trackId — target track; typeId — insert type ("GrapeComp", etc.)
    // position — insertion index (appended at end if == -1)
    AddInsertCommand(juce::String trackId, juce::String typeId,
                     int position = -1);

    void apply(SessionDocument& doc)  override;
    void revert(SessionDocument& doc) override;
    std::string description()         const override;

private:
    juce::String trackId_;
    juce::String typeId_;
    int          requestedPos_;
    int          actualPos_ = -1;  // resolved on apply
};

// ─────────────────────────────────────────────────────────────────
//  RemoveInsertCommand
// ─────────────────────────────────────────────────────────────────
class RemoveInsertCommand final : public Command {
public:
    RemoveInsertCommand(juce::String trackId, int insertIndex);

    void apply(SessionDocument& doc)  override;
    void revert(SessionDocument& doc) override;
    std::string description()         const override;

private:
    juce::String               trackId_;
    int                        insertIndex_;
    bool                       captured_ = false;
    y2k::engine::InsertSlot    snapshot_;
};

// ─────────────────────────────────────────────────────────────────
//  SetInsertBypassedCommand
// ─────────────────────────────────────────────────────────────────
class SetInsertBypassedCommand final : public Command {
public:
    SetInsertBypassedCommand(juce::String trackId, int insertIndex, bool bypassed);

    void apply(SessionDocument& doc)  override;
    void revert(SessionDocument& doc) override;
    std::string description()         const override;

private:
    juce::String trackId_;
    int          insertIndex_;
    bool         oldBypassed_ = false;
    bool         newBypassed_;
    bool         captured_    = false;
};

// ─────────────────────────────────────────────────────────────────
//  MoveInsertCommand  — reorder inserts within a chain
// ─────────────────────────────────────────────────────────────────
class MoveInsertCommand final : public Command {
public:
    // Moves the insert at fromIndex to toIndex.
    MoveInsertCommand(juce::String trackId, int fromIndex, int toIndex);

    void apply(SessionDocument& doc)  override;
    void revert(SessionDocument& doc) override;
    std::string description()         const override;

private:
    juce::String trackId_;
    int          fromIndex_;
    int          toIndex_;
};

// ─────────────────────────────────────────────────────────────────
//  SetTempoCommand
// ─────────────────────────────────────────────────────────────────
class SetTempoCommand final : public Command {
public:
    // newTempo — BPM, clamped to [20, 999] on apply
    explicit SetTempoCommand(double newTempo);

    void apply(SessionDocument& doc)  override;
    void revert(SessionDocument& doc) override;
    std::string description()         const override;

    bool canMerge(const Command& next) const noexcept override;
    void mergeWith(std::unique_ptr<Command> next)     override;

private:
    double oldTempo_ = 120.0;
    double newTempo_;
    bool   captured_ = false;
};

// ─────────────────────────────────────────────────────────────────
//  SetTimeSigCommand
// ─────────────────────────────────────────────────────────────────
class SetTimeSigCommand final : public Command {
public:
    SetTimeSigCommand(int numerator, int denominator);

    void apply(SessionDocument& doc)  override;
    void revert(SessionDocument& doc) override;
    std::string description()         const override;

private:
    int oldNum_ = 4, oldDen_ = 4;
    int newNum_, newDen_;
    bool captured_ = false;
};

// ─────────────────────────────────────────────────────────────────
//  SetProjectNameCommand
// ─────────────────────────────────────────────────────────────────
class SetProjectNameCommand final : public Command {
public:
    explicit SetProjectNameCommand(juce::String newName);

    void apply(SessionDocument& doc)  override;
    void revert(SessionDocument& doc) override;
    std::string description()         const override;

private:
    juce::String oldName_;
    juce::String newName_;
    bool         captured_ = false;
};

// ─────────────────────────────────────────────────────────────────
//  SetAuthorCommand
// ─────────────────────────────────────────────────────────────────
class SetAuthorCommand final : public Command {
public:
    explicit SetAuthorCommand(juce::String newAuthor);

    void apply(SessionDocument& doc)  override;
    void revert(SessionDocument& doc) override;
    std::string description()         const override;

private:
    juce::String oldAuthor_;
    juce::String newAuthor_;
    bool         captured_ = false;
};

// ─────────────────────────────────────────────────────────────────
//  SetSampleRateCommand
// ─────────────────────────────────────────────────────────────────
class SetSampleRateCommand final : public Command {
public:
    // valid values: 44100, 48000, 88200, 96000
    explicit SetSampleRateCommand(int newSampleRate);

    void apply(SessionDocument& doc)  override;
    void revert(SessionDocument& doc) override;
    std::string description()         const override;

private:
    int  oldRate_ = 48000;
    int  newRate_;
    bool captured_ = false;
};

} // namespace y2k::session

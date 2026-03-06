#pragma once
// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_engine/session/session_command_bus.h  — v10
//
//  SessionCommandBus — the wiring layer that connects:
//    • UndoManager  (records every mutation)
//    • Y2KSession   (timeline, clips, markers)
//    • ProjectRouting (tracks, buses, sends)
//    • AutoSaveManager (marks dirty after any change)
//
//  All session mutations go through the bus. Nothing mutates session
//  state directly. This is the difference between v9's stub undo
//  actions and v10's fully wired undo.
//
//  Thread model: UI thread only. Not RT-safe by design.
// ═══════════════════════════════════════════════════════════════════

#include "../session.h"
#include "../mixer/track_model.h"
#include "../undo.h"
#include "auto_save_manager.h"
#include <juce_core/juce_core.h>
#include <functional>
#include <algorithm>

namespace y2k::engine {

class SessionCommandBus {
public:
    SessionCommandBus(Y2KSession&       session,
                      ProjectRouting&   routing,
                      UndoManager&      undo,
                      AutoSaveManager*  autoSave = nullptr)
        : session_(session)
        , routing_(routing)
        , undo_(undo)
        , autoSave_(autoSave)
    {}

    // ── Track fader ───────────────────────────────────────────────
    void setTrackVolume(const juce::String& trackId, float vol) {
        if (auto* t = findTrack(trackId)) {
            const float old = t->volume;
            if (std::abs(old - vol) < 1e-4f) return;
            auto a = makeLambda("Volume: " + t->name,
                [this,trackId,old] { applyTrackVol(trackId, old); },
                [this,trackId,vol] { applyTrackVol(trackId, vol); });
            undo_.perform(std::move(a));
        }
    }

    void setTrackPan(const juce::String& trackId, float pan) {
        if (auto* t = findTrack(trackId)) {
            const float old = t->pan;
            auto a = makeLambda("Pan: " + t->name,
                [this,trackId,old] { applyTrackPan(trackId, old); },
                [this,trackId,pan] { applyTrackPan(trackId, pan); });
            undo_.perform(std::move(a));
        }
    }

    void setTrackMuted(const juce::String& trackId, bool muted) {
        if (auto* t = findTrack(trackId)) {
            const bool old = t->muted;
            auto a = makeLambda((muted ? "Mute: " : "Unmute: ") + t->name,
                [this,trackId,old]   { applyTrackMute(trackId, old); },
                [this,trackId,muted] { applyTrackMute(trackId, muted); });
            undo_.perform(std::move(a));
        }
    }

    void setTrackSoloed(const juce::String& trackId, bool soloed) {
        if (auto* t = findTrack(trackId)) {
            const bool old = t->soloed;
            auto a = makeLambda((soloed ? "Solo: " : "Unsolo: ") + t->name,
                [this,trackId,old]    { applyTrackSolo(trackId, old); },
                [this,trackId,soloed] { applyTrackSolo(trackId, soloed); });
            undo_.perform(std::move(a));
        }
    }

    // ── Timeline ──────────────────────────────────────────────────
    void setTempo(double newTempo) {
        const double old = session_.timeline.tempo;
        auto a = makeLambda("Tempo " + juce::String(newTempo, 1) + " BPM",
            [this,old]      { applyTempo(old); },
            [this,newTempo] { applyTempo(newTempo); });
        undo_.perform(std::move(a));
    }

    // ── Tracks ────────────────────────────────────────────────────
    void addTrack(const juce::String& name, const juce::String& instrumentId) {
        juce::Uuid newId;
        auto a = makeLambda("Add Track: " + name,
            [this,newId]                   { removeTrackById(newId.toString()); },
            [this,name,instrumentId,newId] { addTrackImpl(name, instrumentId, newId); });
        undo_.perform(std::move(a));
    }

    void removeTrack(const juce::String& trackId) {
        if (auto* t = findTrack(trackId)) {
            const juce::String nm = t->name, inst = t->instrumentId;
            const juce::Uuid   tid = t->id;
            auto a = makeLambda("Remove Track: " + nm,
                [this,nm,inst,tid] { addTrackImpl(nm, inst, tid); },
                [this,trackId]     { removeTrackById(trackId); });
            undo_.perform(std::move(a));
        }
    }

    // ── Clips ─────────────────────────────────────────────────────
    void moveClip(const juce::String& trackId, const juce::String& clipId, double newBeat) {
        const double old = getClipBeat(trackId, clipId);
        auto a = makeLambda("Move Clip",
            [this,trackId,clipId,old]     { applyClipBeat(trackId, clipId, old); },
            [this,trackId,clipId,newBeat] { applyClipBeat(trackId, clipId, newBeat); });
        undo_.perform(std::move(a));
    }

    // ── Buses ─────────────────────────────────────────────────────
    void setBusVolume(const juce::String& busId, float vol) {
        BusModel* b = busId.isEmpty()
                    ? &routing_.masterBus
                    : routing_.findBus(juce::Uuid(busId));
        if (!b) return;
        const float old = b->volume;
        const juce::String nm = b->name;
        auto a = makeLambda("Bus Vol: " + nm,
            [this,busId,old] { applyBusVol(busId, old); },
            [this,busId,vol] { applyBusVol(busId, vol); });
        undo_.perform(std::move(a));
    }

    // ── Notifications ─────────────────────────────────────────────
    std::function<void()> onSessionChanged;   // general redraw
    std::function<void()> onRoutingChanged;   // triggers GraphCompiler rebuild

private:
    TrackModel* findTrack(const juce::String& id) {
        for (auto& t : routing_.tracks)
            if (t.id.toString() == id) return &t;
        return nullptr;
    }

    void applyTrackVol (const juce::String& id, float v) {
        if (auto* t=findTrack(id)) { t->volume=v; fireChanged(); }
    }
    void applyTrackPan (const juce::String& id, float v) {
        if (auto* t=findTrack(id)) { t->pan=v;    fireChanged(); }
    }
    void applyTrackMute(const juce::String& id, bool v) {
        if (auto* t=findTrack(id)) { t->muted=v;  fireChanged(); }
    }
    void applyTrackSolo(const juce::String& id, bool v) {
        if (auto* t=findTrack(id)) { t->soloed=v; fireChanged(); }
    }
    void applyTempo(double t) { session_.timeline.tempo=t; fireChanged(); }

    void applyBusVol(const juce::String& busId, float v) {
        BusModel* b = busId.isEmpty()
                    ? &routing_.masterBus
                    : routing_.findBus(juce::Uuid(busId));
        if (b) { b->volume=v; fireRoutingChanged(); }
    }

    void addTrackImpl(const juce::String& name, const juce::String& inst, const juce::Uuid& id) {
        TrackModel t;
        t.id=id; t.name=name; t.instrumentId=inst;
        t.inputType = TrackInputType::Instrument;
        routing_.tracks.push_back(std::move(t));
        fireRoutingChanged();
    }

    void removeTrackById(const juce::String& id) {
        auto& v = routing_.tracks;
        v.erase(std::remove_if(v.begin(), v.end(),
            [&](const TrackModel& t){ return t.id.toString()==id; }), v.end());
        fireRoutingChanged();
    }

    double getClipBeat(const juce::String& trackId, const juce::String& clipId) {
        for (auto& t : session_.tracks) {
            if (t->id.toString() != trackId) continue;
            for (auto& c : t->clips)
                if (c->id.toString() == clipId) return c->startBeat;
        }
        return 0.0;
    }

    void applyClipBeat(const juce::String& tid, const juce::String& cid, double beat) {
        for (auto& t : session_.tracks) {
            if (t->id.toString() != tid) continue;
            for (auto& c : t->clips)
                if (c->id.toString() == cid) { c->startBeat=beat; fireChanged(); return; }
        }
    }

    void fireChanged() {
        if (autoSave_) autoSave_->markDirty();
        if (onSessionChanged) onSessionChanged();
    }
    void fireRoutingChanged() {
        if (autoSave_) autoSave_->markDirty();
        if (onRoutingChanged) onRoutingChanged();
        if (onSessionChanged) onSessionChanged();
    }

    static std::unique_ptr<UndoAction> makeLambda(
        juce::String desc,
        std::function<void()> undo,
        std::function<void()> redo) {
        return std::make_unique<LambdaUndoAction>(
            std::move(desc), std::move(undo), std::move(redo));
    }

    Y2KSession&      session_;
    ProjectRouting&  routing_;
    UndoManager&     undo_;
    AutoSaveManager* autoSave_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SessionCommandBus)
};

} // namespace y2k::engine

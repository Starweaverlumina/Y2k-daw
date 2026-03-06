#pragma once
// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_engine/session/auto_save_manager.h  — v10
//
//  AutoSaveManager — periodic auto-save on background thread.
//
//  Design:
//    • Background juce::Thread fires every intervalSeconds
//    • Only saves if dirty_ atomic flag is set
//    • Writes to <bundle>/autosave.xml (never touches project.xml)
//    • Full atomic save resets dirty_ and removes autosave.xml
//
//  Recovery:
//    On startup: call recoveryAvailable(bundleDir)
//    If true: offer "Crash recovery" dialog to user
//    Accept → loadRecovery(); Decline → discardRecovery()
//
//  Thread model:
//    UI thread:         start(), stop(), markDirty()
//    Background thread: run() → SessionSchema::autoSave()
//    Both:              dirty_ atomic (lock-free)
// ═══════════════════════════════════════════════════════════════════

#include "session_schema.h"
#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>

namespace y2k::engine {

class AutoSaveManager : private juce::Thread {
public:
    using SessionProvider = std::function<const Y2KSession*()>;
    using RoutingProvider = std::function<const ProjectRouting*()>;

    explicit AutoSaveManager(int intervalSeconds = 30)
        : juce::Thread("Y2K-AutoSave")
        , intervalMs_(intervalSeconds * 1000)
    {}

    ~AutoSaveManager() override { stop(); }

    void start(juce::File bundleDir,
               SessionProvider sessionFn,
               RoutingProvider routingFn) {
        bundleDir_ = std::move(bundleDir);
        sessionFn_ = std::move(sessionFn);
        routingFn_ = std::move(routingFn);
        startThread();
    }

    void stop() { signalThreadShouldExit(); notify(); stopThread(3000); }

    void markDirty()   noexcept { dirty_.store(true,  std::memory_order_release); }
    void cancelDirty() noexcept { dirty_.store(false, std::memory_order_release); }
    bool isDirty()     const noexcept { return dirty_.load(std::memory_order_acquire); }

    // Call after successful full atomic save
    void onFullSaveCompleted() {
        cancelDirty();
        autoSaveFile().deleteFile();
    }

    // ── Recovery ──────────────────────────────────────────────────
    static bool recoveryAvailable(const juce::File& bundleDir) noexcept {
        return SessionSchema::autoSaveIsNewer(
            bundleDir.getChildFile("project.xml"),
            bundleDir.getChildFile("autosave.xml"));
    }

    static SessionSchema::LoadResult loadRecovery(const juce::File& bundleDir) {
        const juce::File f = bundleDir.getChildFile("autosave.xml");
        auto xml = juce::XmlDocument::parse(f);
        if (!xml) {
            SessionSchema::LoadResult r;
            r.status = juce::Result::fail("Cannot parse autosave.xml");
            return r;
        }
        return SessionSchema::deserialise(juce::ValueTree::fromXml(*xml));
    }

    static void discardRecovery(const juce::File& bundleDir) {
        bundleDir.getChildFile("autosave.xml").deleteFile();
    }

    juce::File autoSaveFile() const {
        return bundleDir_.getChildFile("autosave.xml");
    }

    juce::Time lastSaveTime()   const noexcept { return lastSaveTime_; }
    bool       lastSaveFailed() const noexcept {
        return lastSaveFailed_.load(std::memory_order_acquire);
    }

    // Optional UI callbacks (called from background thread)
    std::function<void()>             onAutoSaveCompleted;
    std::function<void(juce::String)> onAutoSaveFailed;

private:
    void run() override {
        while (!threadShouldExit()) {
            wait(intervalMs_);
            if (threadShouldExit()) break;
            if (!dirty_.load(std::memory_order_acquire)) continue;
            if (!bundleDir_.isDirectory()) continue;
            const Y2KSession*    s = sessionFn_ ? sessionFn_() : nullptr;
            const ProjectRouting* r = routingFn_ ? routingFn_() : nullptr;
            if (!s || !r) continue;
            const auto res = SessionSchema::autoSave(*s, *r, autoSaveFile());
            lastSaveFailed_.store(!res.wasOk(), std::memory_order_release);
            if (res.wasOk()) {
                lastSaveTime_ = juce::Time::getCurrentTime();
                if (onAutoSaveCompleted) onAutoSaveCompleted();
            } else {
                juce::Logger::writeToLog("[AutoSave] FAILED: " + res.getErrorMessage());
                if (onAutoSaveFailed) onAutoSaveFailed(res.getErrorMessage());
            }
        }
    }

    juce::File        bundleDir_;
    SessionProvider   sessionFn_;
    RoutingProvider   routingFn_;
    int               intervalMs_      = 30000;
    std::atomic<bool> dirty_           {false};
    std::atomic<bool> lastSaveFailed_  {false};
    juce::Time        lastSaveTime_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutoSaveManager)
};

} // namespace y2k::engine

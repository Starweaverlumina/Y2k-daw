// ═══════════════════════════════════════════════════════════════════
//  engine/session/SessionDocument.cpp
// ═══════════════════════════════════════════════════════════════════

#include "SessionDocument.h"
#include "SessionSchema.h"

#include "../y2k_engine/session/session_schema.h"

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

namespace y2k::session {

// ─────────────────────────────────────────────────────────────────
//  Factory: createEmpty
// ─────────────────────────────────────────────────────────────────
std::unique_ptr<SessionDocument> SessionDocument::createEmpty()
{
    auto doc = std::unique_ptr<SessionDocument>(new SessionDocument());

    // Build a minimal default routing: one instrument track + master bus
    doc->routing_ = y2k::engine::ProjectRouting::createDefault();

    // serSession_ will be constructed lazily in syncToSession()
    return doc;
}

// ─────────────────────────────────────────────────────────────────
//  Factory: load
// ─────────────────────────────────────────────────────────────────
std::unique_ptr<SessionDocument> SessionDocument::load(
    const juce::File& projectFile,
    LoadResult&       outResult)
{
    outResult = {};

    if (!projectFile.existsAsFile()) {
        outResult.errorMessage = "File not found: " + projectFile.getFullPathName();
        return nullptr;
    }

    // 1. Read XML text from disk
    const juce::String xmlText = projectFile.loadFileAsString();
    if (xmlText.isEmpty()) {
        outResult.errorMessage = "File is empty or unreadable: "
                                 + projectFile.getFullPathName();
        return nullptr;
    }

    // 2. Parse XML → ValueTree
    if (auto xmlDoc = juce::XmlDocument::parse(xmlText)) {
        auto root = juce::ValueTree::fromXml(*xmlDoc);

        // 3. Validate schema compatibility
        if (!isCompatibleVersion(root)) {
            const SchemaVersion ver = readVersion(root);
            outResult.errorMessage  =
                "Incompatible schema version " + ver.toString()
                + ". Minimum supported: "
                + MIN_COMPATIBLE_VERSION.toString();
            return nullptr;
        }

        // 4. Migrate (v1.0 → v2.0 etc.)
        outResult.migrationLog = migrateRoot(root);

        // 5. Deserialise via engine-layer schema
        auto result = y2k::engine::SessionSchema::deserialise(root);
        if (!result.status.wasOk()) {
            outResult.errorMessage = "Deserialisation failed: "
                                     + result.status.getErrorMessage();
            return nullptr;
        }

        // 6. Build document
        auto doc = std::unique_ptr<SessionDocument>(new SessionDocument());
        doc->savePath_ = projectFile;

        // Sync metadata from loaded session
        if (result.session)
            doc->syncFromSession(*result.session);

        doc->routing_ = std::move(result.routing);
        doc->dirty_   = false;
        outResult.ok  = true;
        return doc;
    }

    outResult.errorMessage = "XML parse error in: " + projectFile.getFullPathName();
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────
//  save / saveAs
// ─────────────────────────────────────────────────────────────────
bool SessionDocument::save(juce::String* errorOut)
{
    if (!hasSavePath()) {
        if (errorOut)
            *errorOut = "No save path set. Use saveAs() first.";
        return false;
    }
    return saveAs(savePath_, errorOut);
}

bool SessionDocument::saveAs(const juce::File& newFile, juce::String* errorOut)
{
    // Build a Y2KSession from current metadata for serialisation
    syncToSession();

    const juce::Result r = y2k::engine::SessionSchema::atomicSave(
        *serSession_, routing_, newFile);

    if (!r.wasOk()) {
        if (errorOut) *errorOut = r.getErrorMessage();
        return false;
    }

    savePath_ = newFile;
    const bool wasDirty = dirty_;
    dirty_ = false;
    if (wasDirty && onDirtyChanged)
        onDirtyChanged();

    return true;
}

// ─────────────────────────────────────────────────────────────────
//  executeCommand
// ─────────────────────────────────────────────────────────────────
void SessionDocument::executeCommand(std::unique_ptr<Command> cmd)
{
    // UndoStack::execute() applies the command and adds it to history
    undoStack_.execute(std::move(cmd), *this);
    // Note: markDirty() and notification firing are performed by the
    // command's apply() / revert() implementations, which call
    // fireSessionChanged() or fireRoutingChanged() as appropriate.
}

// ─────────────────────────────────────────────────────────────────
//  undo / redo
// ─────────────────────────────────────────────────────────────────
void SessionDocument::undo()
{
    undoStack_.undo(*this);
}

void SessionDocument::redo()
{
    undoStack_.redo(*this);
}

// ─────────────────────────────────────────────────────────────────
//  Track finder
// ─────────────────────────────────────────────────────────────────
y2k::engine::TrackModel* SessionDocument::findTrack(const juce::String& trackId) noexcept
{
    for (auto& t : routing_.tracks)
        if (t.id.toString() == trackId)
            return &t;
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────
//  Dirty state
// ─────────────────────────────────────────────────────────────────
void SessionDocument::markDirty()
{
    if (!dirty_) {
        dirty_ = true;
        if (onDirtyChanged) onDirtyChanged();
    }
}

// ─────────────────────────────────────────────────────────────────
//  Notification helpers
// ─────────────────────────────────────────────────────────────────
void SessionDocument::fireSessionChanged()
{
    markDirty();
    if (onSessionChanged) onSessionChanged();
}

void SessionDocument::fireRoutingChanged()
{
    markDirty();
    // Routing change implies a general session change too
    if (onRoutingChanged)  onRoutingChanged();
    if (onSessionChanged)  onSessionChanged();
}

// ─────────────────────────────────────────────────────────────────
//  Private: syncToSession
//  Build (or rebuild) serSession_ from the current metadata_.
//  This is the view of the document that SessionSchema can serialise.
// ─────────────────────────────────────────────────────────────────
void SessionDocument::syncToSession() const
{
    if (!serSession_)
        serSession_ = y2k::engine::Y2KSession::createEmpty();

    auto& s = *serSession_;
    s.projectName       = metadata_.projectName;
    s.author            = metadata_.author;
    s.dawVersion        = metadata_.dawVersion;
    s.createdDate       = metadata_.createdDate;
    s.modifiedDate      = juce::Time::getCurrentTime();
    s.timeline.tempo    = juce::jlimit(20.0, 999.0, metadata_.tempo);
    s.timeline.timeSigNum = metadata_.timeSigNum;
    s.timeline.timeSigDen = metadata_.timeSigDen;
    s.timeline.sampleRate = metadata_.sampleRate;
    s.timeline.bitDepth   = metadata_.bitDepth;
    s.timeline.totalBeats = metadata_.totalBeats;
}

// ─────────────────────────────────────────────────────────────────
//  Private: syncFromSession
//  Populate metadata_ from a freshly deserialised Y2KSession.
// ─────────────────────────────────────────────────────────────────
void SessionDocument::syncFromSession(const y2k::engine::Y2KSession& s)
{
    metadata_.projectName  = s.projectName;
    metadata_.author       = s.author;
    metadata_.dawVersion   = s.dawVersion;
    metadata_.createdDate  = s.createdDate;
    metadata_.modifiedDate = s.modifiedDate;
    metadata_.tempo        = s.timeline.tempo;
    metadata_.timeSigNum   = s.timeline.timeSigNum;
    metadata_.timeSigDen   = s.timeline.timeSigDen;
    metadata_.sampleRate   = s.timeline.sampleRate;
    metadata_.bitDepth     = s.timeline.bitDepth;
    metadata_.totalBeats   = s.timeline.totalBeats;
}

} // namespace y2k::session

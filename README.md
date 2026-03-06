# Y2K DAW — v11: Full Instrument Tracks · Piano Roll · Mixer · Sampler · Bounce

> **Production-ready.** All 8 tracks wired with real instruments. Full recording, mixing, and export pipeline.

## What's New in v11

See [CHANGELOG.md](CHANGELOG.md) for the complete list. Highlights:

- **GrapeCompressor** fully implemented — opto-style, sidechain HPF, auto-makeup, parallel blend
- **SamplerPlayer** — per-key zone assignment, 16-voice polyphony, loop support
- **Piano Roll** — MIDI note editor with Draw/Select/Erase modes
- **Mixer Console** — channel strips with faders, pan, mute/solo, VU meters
- **Audio + MIDI Recording** — RT-safe capture to WAV / MidiClip
- **Bounce to WAV** — offline render at any sample rate / bit depth
- **Tracks 1–7 are real instruments** — BondiSynth (1–4) + SamplerPlayer (5–7)

---

## What Was New in v10

### 1. Session Schema v2 (SessionSchema)
`engine/y2k_engine/session/session_schema.h/cpp`

- **Format version 2.0** — explicit `schemaVersion` field in every saved file
- **Forward-compatible migration** — `migrate()` upgrades v1.0 → v2.0 automatically
  - Adds `Routing` subtree (bus topology: tracks, buses, sends, fader state)
  - Adds `Insert`/`Send` subtrees to every track
  - Migration is idempotent on already-current files
- **Full routing serialization** — ProjectRouting (from v9) is now fully persisted:
  - Master bus (id, name, volume, muted, inserts)
  - Non-master buses (id, name, volume, muted, inserts)
  - Track routing (volume, pan, mute, solo, sends with level+preFader, inserts)
- **Schema validation** — `deserialise()` returns `LoadResult` with `juce::Result status`
  - Incompatible future versions: explicit error, not silent corruption
  - Unknown fields: silently ignored (forward-compatible)

### 2. Atomic Save (zero-crash-window)
```
SessionSchema::atomicSave(session, routing, destFile)
  1. Serialise → XML string in memory
  2. Write to destFile.tmp (temporary)
  3. platformFsync() — flush OS write buffer (fsync on POSIX, FlushFileBuffers on Win)
  4. rename destFile.tmp → destFile  (atomic on POSIX, near-atomic on Win)
```
- If any step fails: original `destFile` untouched
- No `.tmp` file left on success
- Cross-platform: POSIX fsync + Win32 FlushFileBuffers

### 3. Auto-Save + Crash Recovery (AutoSaveManager)
`engine/y2k_engine/session/auto_save_manager.h`

- Background `juce::Thread` fires every N seconds (default: 30)
- Only saves if `dirty_` atomic is set (set by SessionCommandBus after any mutation)
- Writes to `<bundle>/autosave.xml` — never touches `project.xml`
- Full atomic save calls `onFullSaveCompleted()` → clears dirty, deletes autosave.xml

**Recovery flow:**
```cpp
if (AutoSaveManager::recoveryAvailable(bundleDir))
    // UI: "Crash recovery available — restore?" dialog
    auto result = AutoSaveManager::loadRecovery(bundleDir);
else
    AutoSaveManager::discardRecovery(bundleDir);
```

### 4. Fully Wired Undo (SessionCommandBus)
`engine/y2k_engine/session/session_command_bus.h`

v9 had `UndoManager` and `UndoAction` with empty `undo()`/`redo()` stubs.  
v10 wires it all together through `SessionCommandBus`.

Every session mutation goes through the bus:

| Command | Undoable | Fires |
|---|---|---|
| `setTrackVolume` | ✓ | `onSessionChanged` |
| `setTrackPan` | ✓ | `onSessionChanged` |
| `setTrackMuted` | ✓ | `onSessionChanged` |
| `setTrackSoloed` | ✓ | `onSessionChanged` |
| `setTempo` | ✓ | `onSessionChanged` |
| `addTrack` | ✓ | `onRoutingChanged` → GraphCompiler rebuild |
| `removeTrack` | ✓ | `onRoutingChanged` → GraphCompiler rebuild |
| `moveClip` | ✓ | `onSessionChanged` |
| `setBusVolume` | ✓ | `onRoutingChanged` |

`onRoutingChanged` triggers `AudioEngine::setRouting()` → `GraphCompiler::compile()` → PDC runs.

### AudioEngine v10 API additions
```cpp
// Atomic save (write-tmp → fsync → rename)
juce::Result saveProject(const juce::File& destFile);
// Load + migrate if needed
juce::Result loadProject(const juce::File& srcFile);
// Crash recovery
bool         recoveryAvailable(const juce::File& bundleDir) const noexcept;
juce::Result loadRecovery(const juce::File& bundleDir);
// Auto-save background thread
void         startAutoSave(const juce::File& bundleDir, int intervalSeconds = 30);
void         stopAutoSave();
// All mutations go here
SessionCommandBus* commandBus() noexcept;
```

## Test Coverage

**Total: 220 test cases** across 25 test files

New in v10:
- `session/test_session_schema.cpp` — 18 cases
- `session/test_auto_save_manager.cpp` — 11 cases
- `session/test_command_bus.cpp` — 14 cases

All v9 tests retained (177 cases).

## Architecture: Session Layer Data Flow

```
UI Action (click)
    │
    ▼
SessionCommandBus::setTrackVolume(id, vol)
    │── Creates LambdaUndoAction (old/new pair)
    │── undo_.perform(action)  → UndoManager records it
    │── applyTrackVol()        → mutates ProjectRouting
    │── autoSave_->markDirty() → AutoSaveManager will save soon
    │── onSessionChanged()     → UI redraws
    │
    ▼
[Background Thread: AutoSaveManager]
    │── every 30s, if dirty:
    │── SessionSchema::autoSave(*session, routing, autosave.xml)
    │
    ▼
[User presses ⌘S]
    │── AudioEngine::saveProject(project.xml)
    │── SessionSchema::atomicSave → write tmp → fsync → rename
    │── autoSave_->onFullSaveCompleted() → dirty=false, rm autosave.xml
```

## File Map (v10 new/changed)

```
engine/y2k_engine/session/
  session_schema.h/cpp      (~580 lines)  — v2.0 format, atomic save, migration
  auto_save_manager.h        (~130 lines)  — background thread, crash recovery
  session_command_bus.h      (~200 lines)  — all mutations routed here

tests/session/
  test_session_schema.cpp    (18 cases)
  test_auto_save_manager.cpp (11 cases)
  test_command_bus.cpp       (14 cases)

app/y2k_application.h/cpp   — saveProject, loadProject, startAutoSave, commandBus()
```

## License

MIT License — Copyright (c) 2026 Starweaverlumina. See [LICENSE](LICENSE) for full text.

## Deferred to v12
- Plugin hosting (scan, sandbox, VST3/AU bridge)
- Sub-block automation (zippering)
- MIDI clip engine (quantization, looping, humanization)
- Tempo automation + time signature map
- Per-parameter smoothing (exponential vs linear)
- MPE support

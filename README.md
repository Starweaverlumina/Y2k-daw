# Y2K DAW — v10

A real-time digital audio workstation engine built in **C++20 / JUCE**.
Ships as a self-extracting installer that unpacks 92 source files and builds
with a single CMake invocation.

> **92 files · 220 test cases · Atomic save · Wired undo · Crash recovery**

---

## Quick Start

```bash
# Extract + build + run tests
bash build_y2k_daw_v10.sh y2k-daw --test

# Extract only
bash build_y2k_daw_v10.sh y2k-daw

# Then build manually
cmake -B build -S y2k-daw -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Run

| Platform | Command |
|---|---|
| macOS   | `open build/Y2KDAW_artefacts/Release/Y2K\ DAW.app` |
| Linux   | `./build/Y2KDAW_artefacts/Release/Y2K\ DAW` |
| Windows | `build\Y2KDAW_artefacts\Release\Y2K DAW.exe` |

### Offline / Local JUCE

```bash
cmake -B build -S y2k-daw -DJUCE_DIR=/path/to/JUCE
```

---

## Requirements

| Dependency | Version |
|---|---|
| CMake | 3.22+ |
| C++ compiler | C++20 (Clang 14+, GCC 12+, MSVC 2022+) |
| JUCE | 7+ (auto-fetched via CMake if `JUCE_DIR` not set) |
| Catch2 | 3+ (required only for `--test` / `-DY2K_BUILD_TESTS=ON`) |

---

## Installer Script Usage

```
bash build_y2k_daw_v10.sh [target-dir] [flags]

Flags:
  (none)    Extract source tree only
  --build   Extract + configure + build
  --test    Extract + configure + build + run all 220 tests
```

If `target-dir` already exists you will be prompted before it is overwritten.

---

## What's New in v10

### 1 — Session Schema v2

`engine/y2k_engine/session/session_schema.h/.cpp`

- `schemaVersion = "2.0"` embedded in every saved file.
- `migrate()` auto-upgrades v1.0 → v2.0 at load time (idempotent).
  - Adds `Routing` subtree (buses, sends, fader state).
  - Adds `Insert` / `Send` subtrees per track.
- Full `ProjectRouting` serialization: master bus, non-master buses, per-track
  routing (vol / pan / mute / solo), send slots (level + preFader), insert chains.
- `deserialise()` returns a typed `LoadResult { status, session, routing, migrationLog }` — no silent corruption.

### 2 — Atomic Save

`SessionSchema::atomicSave(session, routing, destFile)` follows a four-step
safe-write sequence:

1. Serialise → XML in memory.
2. Write to `destFile.tmp`.
3. `platformFsync()` — flush OS write buffer (`fsync` / `FlushFileBuffers`).
4. `rename()` `.tmp` → `destFile` (atomic on POSIX).

The original file is never touched on failure, and no `.tmp` orphan is left on success.

### 3 — Auto-Save + Crash Recovery

`engine/y2k_engine/session/auto_save_manager.h`

- `juce::Thread` background save every **30 s** (configurable).
- Fires only when the `dirty_` atomic flag is set.
- Writes to `<bundle>/autosave.xml` — **never touches `project.xml`**.
- `recoveryAvailable(bundleDir)` for startup crash detection.
- `loadRecovery()` / `discardRecovery()` — full control from UI code.
- `onFullSaveCompleted()` → clears dirty flag, removes `autosave.xml`.

### 4 — Fully Wired Undo

`engine/y2k_engine/session/session_command_bus.h`

v9 had `UndoAction` with empty stubs. v10 routes **all mutations** through
`SessionCommandBus`, each creating a `LambdaUndoAction` that:

- Calls `undo_.perform()`.
- Mutates state.
- Marks auto-save dirty.
- Fires `onSessionChanged` / `onRoutingChanged` → `AudioEngine::setRouting()` → `GraphCompiler` → PDC.

| Mutation | Command |
|---|---|
| Volume | `setTrackVolume` |
| Pan | `setTrackPan` |
| Mute / Solo | `setTrackMuted` · `setTrackSoloed` |
| Tempo | `setTempo` |
| Track lifecycle | `addTrack` · `removeTrack` |
| Clip | `moveClip` |
| Bus | `setBusVolume` |

### 5 — AudioEngine v10 API

```cpp
engine.saveProject(file);       // atomicSave + clear dirty
engine.loadProject(file);       // deserialise + migrate + rewire
engine.startAutoSave(dir);      // starts background thread
engine.commandBus();            // SessionCommandBus* for UI mutations
engine.recoveryAvailable();     // startup crash check
engine.loadRecovery();          // restore from autosave.xml
```

---

## Retained Systems

### v8 (all present)

| System | Detail |
|---|---|
| Immutable `GraphState` | Atomic swap, zero audio dropout |
| `ParameterBank` 4-layer model | base + automation + mod + midi |
| `TransportEngine` | Sample-accurate beat position |
| `VoiceManager` | 16-voice ADSR pool |
| Lock-free MIDI queue | Zero alloc in RT path |
| Automation hardening | Auto layer never writes to base |
| `ClipScheduler` | Exact BPM scheduling, `atomic<shared_ptr>` swap |
| `BondiSynth` | `ParameterBank` integrated, no `setSize` in RT |

---

## Real-Time Safety Audit

Every audio-thread code path satisfies these constraints:

- ✅ No `setSize()` in any `process()` method
- ✅ No `std::mutex` in any RT path
- ✅ PDC injection: non-RT only (inside `commitState`)
- ✅ `LatencyCompensator::process()` — vector read only, no resize
- ✅ `MeterTap::process()` — stack-only, atomic stores
- ✅ `SumNode::process()` — `FloatVectorOperations`, no alloc
- ✅ `TrackFaderNode::process()` — smoothed ramps, no alloc
- ✅ `BondiSynth::process()` — `bank_.resolved()` reads, no alloc

---

## Test Coverage — 220 TEST_CASEs

| Module | File | Cases |
|---|---|:---:|
| graph | `test_pdc.cpp` | 11 |
| graph | `test_processor_graph.cpp` | 11 |
| graph | `test_graph_state.cpp` | 6 |
| meter | `test_meter_tap.cpp` | 13 |
| mixer | `test_graph_compiler.cpp` | 10 |
| mixer | `test_sum_node.cpp` | 8 |
| mixer | `test_track_fader_node.cpp` | 10 |
| mixer | `test_track_model.cpp` | 9 |
| mixer | `test_track_gain.cpp` | 9 |
| dsp | `test_parameter_state.cpp` | 10 |
| dsp | `test_voice_manager.cpp` | 9 |
| dsp | `test_smooth_parameter.cpp` | 3 |
| dsp | `test_sine_osc.cpp` | 8 |
| engine | `test_transport_engine.cpp` | 9 |
| engine | `test_automation.cpp` | 7 |
| engine | `test_autoplay.cpp` | 6 |
| engine | `test_midi_learn.cpp` | 8 |
| engine | `test_session.cpp` | 6 |
| engine | `test_undo.cpp` | 8 |
| record | `test_audio_recorder.cpp` | 6 |
| record | `test_midi_recorder.cpp` | 6 |
| record | `test_audio_file_importer.cpp` | 4 |

---

## Parameter Architecture

```
UI          ──[pushParameterEvent  layer=0]──▶  ParameterBank.base
Automation  ──[setParameterAutomation]──────▶  ParameterBank.automation
MIDI        ──[setParameterMidi]────────────▶  ParameterBank.midi

DSP reads:  resolvedParameter() = clamp(base + auto + mod + midi)
```

---

## On Launch (proof-of-life)

- **440 Hz sine** plays immediately via `VoiceManager`.
- `TransportEngine` drives beat position from sample count.
- Automation sweeps frequency 220 → 880 Hz (automation layer, base untouched).
- Track volume breathes via automation layer only.
- Kontrol keyboard → lock-free MIDI queue → `VoiceManager`.
- Right-click any knob → MIDI Learn (writes to midi layer, not base).
- Graph edits swap `GraphState` atomically — no audio dropout.

---

## Source Tree (extracted)

```
y2k-daw/
├── CMakeLists.txt
├── app/                          # JUCE app entry point
├── cmake/                        # CMake helpers
├── engine/
│   ├── y2k_dsp/                  # DSP primitives
│   │   ├── effects/
│   │   ├── generators/
│   │   ├── graph/
│   │   ├── instruments/
│   │   ├── meter/
│   │   ├── mixer/
│   │   ├── param/
│   │   ├── recording/
│   │   └── voice/
│   └── y2k_engine/               # High-level engine
│       ├── graph_synth/
│       ├── mixer/
│       └── session/              # Schema v2 · AutoSave · CommandBus
├── prototype/
├── tests/                        # 220 Catch2 test cases
│   ├── dsp/  engine/  graph/
│   ├── meter/  mixer/  recording/
│   └── session/
└── ui/
    └── y2k_ui/
        ├── components/
        └── screens/
```

---

## License

MIT © 2026 Starweaverlumina — see [LICENSE](LICENSE) for details.

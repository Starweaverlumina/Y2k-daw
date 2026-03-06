# Changelog

All notable changes are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

---

## [v10] — Current

### Added
- **Session Schema v2** — `schemaVersion = "2.0"`, `Routing` subtree,
  `Insert`/`Send` subtrees per track, typed `LoadResult` from `deserialise()`.
- **`migrate()`** — automatic v1.0 → v2.0 upgrade at load time (idempotent).
- **Atomic Save** — `atomicSave()` write-to-tmp → fsync → rename, zero crash window.
- **`AutoSaveManager`** — background save every 30 s, `autosave.xml` recovery,
  `recoveryAvailable()` / `loadRecovery()` / `discardRecovery()`.
- **`SessionCommandBus`** — fully wired undo/redo for all session mutations:
  `setTrackVolume`, `setTrackPan`, `setTrackMuted`, `setTrackSoloed`,
  `setTempo`, `addTrack`, `removeTrack`, `moveClip`, `setBusVolume`.
- **AudioEngine v10 API** — `saveProject`, `loadProject`, `startAutoSave`,
  `commandBus()`, `recoveryAvailable()`, `loadRecovery()`.
- 43 new test cases (session, undo, recovery).

### Changed
- `UndoAction` stubs from v9 replaced with `LambdaUndoAction` via `SessionCommandBus`.
- All session mutations now mark auto-save dirty and fire `onRoutingChanged` → PDC pipeline.

---

## [v9]

- Mixer: `SumNode`, `TrackFaderNode`, `GraphCompiler`, bus model.
- Metering: `MeterTap`, `MeterBridge`, peak / RMS atomic stores.
- PDC: `LatencyCompensator`, `PdcGraph`, `processorLatency()`.
- `ProjectRouting` model: master bus + non-master buses + per-track routing.
- `UndoAction` infrastructure (stubs wired in v10).
- 177 test cases.

---

## [v8]

- Immutable `GraphState` + atomic swap.
- Unified `ParameterBank` 4-layer model (base + automation + mod + midi).
- Sample-accurate `TransportEngine`.
- `VoiceManager` 16-voice ADSR pool.
- Lock-free MIDI queue.
- Automation hardening (auto layer isolated from base).
- `ClipScheduler` exact BPM scheduling.
- `BondiSynth` — `ParameterBank` integrated, RT-safe `process()`.

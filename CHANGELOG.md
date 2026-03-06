# Changelog

All notable changes are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

---

## [v11] — Current

### Added
- **GrapeCompressor** — Full opto-style stereo compressor (`effects/grape_compressor.cpp`):
  soft-knee, auto-makeup, sidechain HPF (20–500 Hz), parallel dry/wet, atomic VU metering.
- **SamplerPlayer** — 16-voice polyphonic sample/loop player with per-key zone assignment
  (128 zones, rootNote/lo-hi range, loop points, pitch-shift by playback rate). Wired on Tracks 5–7.
- **Piano Roll** (`ui/y2k_ui/screens/piano_roll.h/cpp`) — Draw/Select/Erase modes,
  88-key sidebar, scrollable/zoomable beat grid, edits committed via `SessionCommandBus`.
- **Effects chain UI** (`ui/y2k_ui/components/effects_chain.h/cpp`) — per-track insert
  slot list with bypass toggle and remove button.
- **Mixer console** (`ui/y2k_ui/screens/mixer_view.h/cpp`) — `ChannelStrip` per track
  with vertical fader, pan knob, mute/solo, VU bar; all mutations go through command bus.
- **Audio recording** — `AudioEngine::arm/start/stop/flushAudioRecord()`, SPSC ring buffer
  written in RT callback, flushed to WAV off-thread.
- **MIDI recording** — `AudioEngine::arm/start/stop/flushMidiRecord()`, `MidiRecorder`
  called in RT callback, `buildClip()` post-stop with orphan-note fallback.
- **Bounce to WAV** (`engine/y2k_engine/bounce_engine.h/cpp`) — offline render of full
  session graph to stereo 24-bit WAV with progress callback.

### Fixed
- **Tracks 1–7** were empty stub gain nodes. Now: Tracks 1–4 = `BondiSynth`, Tracks 5–7 = `SamplerPlayer`.
  All 8 tracks routed through `GrapeCompressor → TrackGainProcessor → OUTPUT`.
- **ArrangeView** wired to `SessionCommandBus` — mute/solo header buttons and clip drag
  fire undoable commands; double-click opens Piano Roll.
- **SessionValidator migration registry** — v1.0→v1.1 (meterTap) and v1.1→v2.0 (inputType, sends/inserts).
- **README license section** filled in (MIT).

### Changed
- `AudioEngine` v11: full instrument chain, recording in RT callback, new sampler/bounce API.
- All three CMakeLists updated to register new source files.

---

## [v10] — Previous

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

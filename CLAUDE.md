# CLAUDE.md — Y2K DAW

Instructions for Claude Code when working in this repository.

---

## Project Overview

Y2K DAW is a real-time digital audio workstation engine in **C++20 / JUCE**.
The entry point for the repo is `build_y2k_daw_v10.sh` — a self-extracting
installer that unpacks 92 source files into a `y2k-daw/` directory.

---

## First-Time Setup

```bash
# 1. Extract the source tree
bash build_y2k_daw_v10.sh y2k-daw

# 2. Configure
cmake -B build -S y2k-daw -DCMAKE_BUILD_TYPE=Release

# 3. Build
cmake --build build -j$(nproc)

# 4. Run tests (optional)
cmake -B build -S y2k-daw -DY2K_BUILD_TESTS=ON
cmake --build build && ctest --test-dir build --output-on-failure
```

The extracted tree lives at `y2k-daw/` (gitignored). All source edits happen
there after extraction.

---

## Project Structure (after extraction)

```
y2k-daw/
├── CMakeLists.txt
├── app/                      # JUCE app + main window
├── cmake/                    # CMake helpers / FetchContent
├── engine/
│   ├── y2k_dsp/              # Low-level DSP (graph, meter, mixer, param, voice, recording)
│   └── y2k_engine/           # High-level engine (session, undo, auto-save, command bus)
│       └── session/          # SessionSchema v2 · AutoSaveManager · SessionCommandBus
├── prototype/                # Experimental / scratch code
├── tests/                    # 220 Catch2 test cases
└── ui/
    └── y2k_ui/               # JUCE UI components and screens
```

---

## Key Architectural Rules

### Real-Time Safety (STRICT)
- **Never** call `setSize()`, `new`, `delete`, or `malloc` inside any `process()` method.
- **Never** use `std::mutex` or any blocking primitive in the audio thread.
- PDC injection must happen only inside `commitState()` (non-RT path).
- All inter-thread communication uses atomics or lock-free queues.

### Parameter System
- All parameter mutations go through `ParameterBank` layers:
  `base` (UI) → `automation` → `mod` → `midi`.
- DSP reads `resolvedParameter() = clamp(base + auto + mod + midi)`.
- Never write directly to the base layer from the automation or MIDI systems.

### Session / Undo
- All session mutations must go through `SessionCommandBus` — never mutate
  session state directly from UI code.
- Every `SessionCommandBus` call must create a `LambdaUndoAction` and call
  `undo_.perform()` before mutating state.
- After any mutation: mark `dirty_`, fire `onSessionChanged` and
  `onRoutingChanged` callbacks.

### Saving
- Always use `SessionSchema::atomicSave()`. Never write directly to
  `project.xml`.
- Auto-save writes only to `autosave.xml` — it must never overwrite the main
  project file.

---

## CMake Options

| Option | Default | Description |
|---|---|---|
| `Y2K_BUILD_TESTS` | `OFF` | Build Catch2 test suite |
| `Y2K_USE_ASAN` | `OFF` | Enable AddressSanitizer |
| `Y2K_ENABLE_ML` | `OFF` | Enable ML features (requires ONNX) |
| `JUCE_DIR` | *(auto)* | Path to local JUCE checkout |

---

## Running Tests

```bash
# All tests
ctest --test-dir build --output-on-failure

# Single suite
ctest --test-dir build -R test_undo --output-on-failure
```

---

## Common Tasks

### Add a new session command
1. Declare the method in `session_command_bus.h`.
2. Implement in `session_command_bus.cpp` — wrap in `LambdaUndoAction`,
   call `undo_.perform()`, mutate state, mark dirty, fire callbacks.
3. Add test cases to `tests/session/test_undo.cpp`.

### Add a new DSP node
1. Create `h`/`cpp` pair under `engine/y2k_dsp/<subsystem>/`.
2. Inherit from the appropriate base (e.g., `ProcessorNode`).
3. Ensure `process()` contains zero allocations and zero mutex usage.
4. Register the node in `GraphCompiler` if it participates in the mix graph.
5. Add test cases under `tests/`.

### Modify session schema
1. Bump `schemaVersion` in `session_schema.h`.
2. Add migration logic in `SessionSchema::migrate()`.
3. Ensure migration is idempotent.
4. Add test cases to `tests/session/test_session.cpp`.

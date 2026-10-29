# Decisions

## ADR-001: DPF built-in WebView, not dpfwebui
DPF @ 4238e1c7 (2025-10-23) has `UI_TYPE=webview` (WKWebView on macOS). dpfwebui was last updated 2024-05 and is tied to an older DPF.

## ADR-002: Integer tick timing
PPQ = 3840 per quarter; all resolutions (incl. triplets) are exact integers. No float-seconds accumulation.

## ADR-003: Bridge transport
JS→C++: DPF-injected `setState('cmd', json)`; arrives on the main thread via `Plugin::setState` (undeclared keys are not rejected on that path).
C++→JS: `PulsUI::uiIdle` drains a main-thread outbox (reached through `getPluginInstancePointer()`), then calls `UI::stateChanged('evt', base64(json))`
(DPF does not escape the string it injects into JS, hence base64). Playhead telemetry uses atomics read in `uiIdle`.

## ADR-004: Resources location
macOS CLAP: `puls.clap/Contents/Resources/index.html` (DPF `getResourcePath`); Linux/Windows: `puls.clap/resources/`. The DPF WebMeters example uses `resources/` and would not resolve on macOS.

## ADR-005: Host quarter position
DPF's CLAP adapter derives bbt from `song_pos_beats` (quarters). Absolute quarter pos = (bar-1)*beatsPerBar + (beat-1) + tick/ticksPerBeat; only valid when `bbt.valid`.

## ADR-006: RT handoff is a wait-free triple buffer of whole snapshots
`engine/SnapshotExchange.hpp`. The main thread copies the Model into `EngineSnapshot` (pattern + settings, ~7.5 KB POD) and publishes; `run()` acquires once per block. No allocation, locks or deletes on the audio thread; tested with a producer thread for tearing/ordering. Sounding-note bookkeeping lives in `Sequencer`, so replacing the pattern mid-note never leaves stuck notes.

## ADR-007: nlohmann/json 3.11.3 for non-RT code only
Vendored single header (`third_party/`, sha256 in its README), used by `state/` and `bridge/`, never by `engine/` or `run()`. Plugin builds with `-fno-fast-math`: DPF's default `-ffast-math` removes `isfinite` guards in the engine and breaks json.hpp's infinity handling, and the tests are built without it.

## ADR-008: Bridge validation is atomic and strict
Commands/props are applied to a copy and committed only if every present key is valid; invalid commands yield an `error` event and change nothing (the UI then re-requests `init`). Persisted state is stricter still: any invalid value rejects the whole state, the model keeps its previous value and an `error` event is queued. Steps are serialized sparsely (defaults omitted). Tracks are addressed by stable id, never index.

## ADR-009: Phase 2 scope vs. the plan
`selectPattern` and the pattern bank are deferred to Phase 4 (undo/redo was pulled forward, see ADR-010) (there is exactly one pattern now; the toolbar shows its name). Velocity drag is coalesced to one command per animation frame plus the final value. `settings` is an added event. Toolbar resolution/swing act on the selected track; "−" removes the selected track.

## ADR-010: Undo/redo is snapshot-based, pattern-only, coalesced by gesture id
`model/UndoStack.hpp` keeps up to 100 whole-`Pattern` snapshots (main thread, heap, ~7.5 KB each); the Bridge copies the pattern before each command and records it if the command changed anything (`setSettings` excluded: enabled/anchor are not undoable). `undo`/`redo` answer with `patternChanged` + `history {canUndo, canRedo}` (also in `init`). `setStepVelocity` may carry a non-zero `gesture` id; consecutive commands with the same id/track/step keep only the first "before" state, so one drag = one undo step. History is not persisted and is cleared when state is restored. Track ids are never reused, so undoing a removal brings the same id back. Ctrl/Cmd+Z, Shift+Ctrl/Cmd+Z and Ctrl+Y work only while the WebView has keyboard focus (unverified in Bitwig).

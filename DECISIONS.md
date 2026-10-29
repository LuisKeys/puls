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
the pattern bank and `selectPattern` were deferred to Phase 4 (done, ADR-011); undo/redo was pulled forward (ADR-010) (there is exactly one pattern now; the toolbar shows its name). Velocity drag is coalesced to one command per animation frame plus the final value. `settings` is an added event. Toolbar resolution/swing act on the selected track; "−" removes the selected track.

## ADR-010: Undo/redo is snapshot-based, pattern-only, coalesced by gesture id
`model/UndoStack.hpp` keeps up to 100 whole-`Pattern` snapshots (main thread, heap, ~7.5 KB each); the Bridge copies the pattern before each command and records it if the command changed anything (`setSettings` excluded: enabled/anchor are not undoable). `undo`/`redo` answer with `patternChanged` + `history {canUndo, canRedo}` (also in `init`). `setStepVelocity` may carry a non-zero `gesture` id; consecutive commands with the same id/track/step keep only the first "before" state, so one drag = one undo step. History is not persisted and is cleared when state is restored. Track ids are never reused, so undoing a removal brings the same id back. Ctrl/Cmd+Z, Shift+Ctrl/Cmd+Z and Ctrl+Y work only while the WebView has keyboard focus (unverified in Bitwig).

## ADR-011: Pattern bank, selected vs playing, quantized switching
The Model holds 16 patterns; `selected` is the one viewed/edited, the *playing* one lives on the audio thread (`PatternSwitcher`) and comes back through telemetry (`playheads.pattern`), so the UI can show a queued switch. Every snapshot carries the whole bank (~120 KB memcpy per edit on the main thread); `selectPattern` goes through a wait-free `SpscQueue<SwitchRequest,16>`. The switcher resolves a request into an absolute target in musical units (next beat = next whole quarter, next bar = next multiple of the host's beats-per-bar; a block starting exactly on the boundary switches at once) and the Sequencer applies it per event: events with time >= target come from the new pattern, so the result is identical for any block size (tested 1..4096). The step grid is NOT re-anchored on a switch (tracks keep their phase, like a pattern that had always been there). Stopped or disabled: immediate. A stale target (transport jumped back) is re-resolved; selecting the playing pattern again cancels a pending switch. Default: bar while playing (setting `patternSwitch`: bar/beat/now; Alt+click = now). Undo restores the pattern the entry belongs to and shows it, but does not switch playback. Playheads are hidden while a non-playing pattern is viewed. Time-signature changes mid-song are not tracked (the signature of the current block is used).

## ADR-012: Schema v2 (whole bank) with v1 migration
`{"schemaVersion":2,"selected":n,"patterns":[<=16 patterns],"settings":{...}}`; missing slots become blank default patterns. v1 (`"pattern"`) is migrated in `migrate()` into slot 0 before validation (exercised by `ClapLoadTest`, which still restores a v1 document). Loading stays strict and atomic. History, clipboard and playheads are never persisted. Track ids are unique per pattern; `nextTrackId` is the global maximum + 1.

## ADR-013: Step selection, clipboard and keyboard
Selection is a rectangle (tracks x steps) between an anchor and a cursor; Shift+click or Shift+arrows extend it. Space/Enter toggles, Delete clears, Ctrl/Cmd+C/X/V copy/cut/paste the block (UI-side clipboard, full step data) at the selection's top-left. Multi-cell edits are one atomic `editSteps` command (<=1024 cells, one undo step). Keys are handled only while the WebView has focus and never while an input/select/button is focused or a menu is open. Not verified in Bitwig.

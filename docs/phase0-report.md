# Phase 0 report (feasibility spike)

> Historical record. The WebView/TypeScript UI described here was replaced by a native DGL/NanoVG editor and `ui/` was removed (DECISIONS.md, ADR-014..017).

Pinned: DPF `4238e1c7f0351bbe488d79f0899c540543ac7583` (+ nested pugl `5e2621d7`), Vite 6.3.5, TypeScript 5.8.3.

## Verified (macOS arm64, headless)
- `scripts/build.sh` builds the UI and `bin/puls.clap` (with `Contents/Resources/index.html`).
- `scripts/validate.sh` runs `tests/ClapLoadTest.cpp`, a minimal CLAP host that dlopens the bundle and checks:
  id `com.accelone.puls`, feature `note-effect`, 1 note output port, 0 audio ports, GUI extension present,
  state save/load/save round-trip (versioned JSON), and note-on @0 / note-off @5512 / note-on @22050 at 120 bpm / 44.1 kHz.

## Pending (not verified)
- Bitwig runtime: loads, listed as note FX, UI opens, WebView renders, ping round-trip, notes reach an instrument,
  project save/reopen restores counter, closing UI keeps notes playing.
- `clap-validator`: not installed; building it from source was not permitted in this session.
- JS<->C++ round trip is implemented (`ui/src/bridge.ts`, `plugin/PulsUI.cpp`) but only exercised inside a real host.
- Linux/Windows: not built.

## Integration notes / limitations
- Build needs the Xcode SDK: the CLT SDK (27.0) is newer than the Xcode linker (tapi "unknown architecture").
- DGL stub lib is built by a sub-make; `export USE_WEB_VIEW` is required or linking fails on a DGL sentinel symbol.
- Resources path differs per OS (see DECISIONS ADR-004). Module scripts do not work from file://, so Vite emits one inline classic script.
- `UI::stateChanged` does not escape its payload: base64 only.
- Zero audio ports accepted by DPF's CLAP adapter; whether Bitwig accepts it as a note-effect is pending.

## Bitwig results (macOS, user-verified)
Loads and appears as a device; added to a track; UI opens; ping increments the counter; notes play; saved project
restores the counter (UI now requests state with `hello` on load, since DPF pushes restored state before the page is ready).

# Phase 1 notes (engine)
- `engine/`: Step/Track/Pattern models, `Sequencer` (scheduling + note-off pool folded into one class instead of a separate Scheduler),
  integer units timing (see Timing.hpp), deterministic probability rolls. Probability, ratchets, microtiming, swing, gate, phase,
  mute/solo are already implemented in the engine; Phase 3 adds their UI.
- Event ownership: an event is emitted at the first sample at/after its time and belongs to the block whose window
  [t0-0.99 sample, t1-0.99 sample) contains it, so output is identical for any block size (tested: 1..4096).
- `RestartOnStart` anchors the grids at the start position and re-anchors after a position jump or loop wrap; `HostAbsolute` anchors at position 0.
- Not yet: pattern switching (Phase 4); notes of a track muted mid-note end at their gate rather than being cut.

# Phase 2 notes (model, bridge, minimal UI)
- New layers: `model/` (Model), `state/` (versioned JSON, strict load), `bridge/` (typed commands -> events), `engine/Snapshot*.hpp` (RT handoff). Persisted state is now `{"schemaVersion":1,"pattern":...,"settings":...}`; the Phase 0 counter/ping are gone.
- `Sequencer::playhead(i)` reports each track's step index; the UI side sends `playheads` (<=30 Hz, only on change) and `transport`.
- UI: sticky label column, 64-step scrolling grid (16 visible), per-cell DOM updates, click toggles, vertical drag edits velocity, toolbar (enable, resolution, swing, track +/-, anchor setting).
- Verified (macOS arm64, headless): clean `scripts/build.sh`; engine + bridge unit tests (state round trip, corrupt-state rejection, command validation/atomicity, track add/remove, playheads, triple-buffer under a producer thread); `ClapLoadTest` incl. state save/load/corrupt-load round trip and note output from the default pattern.
- **Pending (needs Bitwig, not verified):** UI renders and is usable in the WebView; clicking steps changes the notes heard; drag edits velocity; playhead animates; resolution/swing/length/add/remove track; settings panel; project save/reopen restores the edited pattern; closing the UI keeps playing; idle CPU with UI hidden. The TypeScript is type-checked and bundled but has not been run in a browser/WebView, and the UI has no automated tests.

# Phase 3 notes (advanced sequencing UI)
- The engine already implemented probability, ratchets, micro-timing, swing, gate/duration, phase, mute/solo (Phase 1, with tests: `probability_boundaries_and_determinism`, `ratchets_are_evenly_spaced...`, `negative_microtiming...`, `swing_gate_duration_phase_mute_solo_velocity`); this phase exposes them.
- UI: M/S buttons per track; right-click a step for its menu (active, velocity, probability, ratchets, micro-timing, gate, clear); right-click a track label for its menu (name, color, note, channel, resolution, length, phase, velocity %, probability %, swing %, duration %, mute, solo). Cells show markers: ratchet count, dashed top = probability < 100, side bar = early/late micro-timing. Ctrl+click is treated as a context click.
- `gesture` ids now coalesce `modifyStep` / `modifyTrack` as well (one slider drag = one undo step); tested.
- **Pending (needs Bitwig, not verified):** right-click/ctrl-click opens the menus in the WebView; sliders/inputs/color picker work there (`<input type=color>` in WKWebView in particular); audible effect of each parameter; solo/mute while playing. Not run in any browser/WebView yet; no automated UI tests.

# Phase 4 notes (workflow)
- 16-pattern bank (slots bar under the toolbar: click = view + request playback, Alt+click = now, right-click = rename/play now/copy/paste/duplicate/clear). Pattern switching is quantized (bar by default while playing; beat/now selectable in settings; immediate when stopped), stored as an absolute target, and block-size independent (see ADR-011). A green dot marks the playing slot.
- Persistence is schema v2 (whole bank + selected slot) with a v1 migration; corrupt documents are rejected without touching the live model (an `error` event is sent).
- Step workflow: shift-click/shift-arrow multi-select, arrows move the cursor, Space/Enter toggle, Delete clears, Ctrl/Cmd+C/X/V; edits to a block are one undo step.
- Verified (macOS arm64, headless): clean build; engine tests (21, incl. bar/beat/immediate/stopped switches across block sizes 1..4096 with strict note balance, switcher planning/cancel/jump recovery, SPSC queue); bridge tests (18, incl. v2 round trip, v1 migration, corrupt state, bank commands and per-slot undo, atomic batch edits); `ClapLoadTest` (v1 restore -> v2 save, and a running plugin switching to the restored pattern).
- **Pending (needs Bitwig, not verified):** slot bar usability; switching at the bar while the host plays (listen for no stuck/dropped notes), including with loops and tempo changes; selecting a pattern while stopped then pressing play; project save/reopen restoring all 16 patterns and the selected slot; keyboard shortcuts and clipboard inside the WebView (they only work if Bitwig forwards key events to the plugin window); right-click menus on slots. The UI TypeScript is type-checked and bundled but has no automated tests and has not been run in a WebView.

# Phase 5 notes (hardening)
**Done and verified (macOS arm64, headless):**
- Stress: 16 tracks x 64 steps, 8 ratchets, all resolutions, swing 75, +-50 % micro-timing, shared notes across tracks, at 20 / 120 / 999 bpm, 44.1 / 192 kHz, blocks 7..4096: strict note on/off balance, no stuck notes after stop, every event inside its block. Average audio-thread cost over all configs (blocks >= 64) is **0.64 % of the block budget** (worst config 999 bpm / 44.1 kHz / 512: ~15 us per 11.6 ms block; single-block maxima ~25 us, dominated by OS jitter). This is the engine alone, not a Bitwig CPU profile.
- Dense patterns without drops give identical output for different block sizes; beyond ~2000 candidates per block the overflow is counted and only the newest note-ons are dropped (tested with 16 tracks x 1/64 x 8 ratchets at 999 bpm / block 4096).
- New input guards in `Sequencer::process`: non-finite, negative or absurd bpm / sample rate / position (>= 1e9 quarters would overflow the int64 units) are treated as "stopped"; zero-frame and 1M-frame blocks are safe. Tested.
- Real-time rule check: a test replaces `operator new/delete` and runs the whole audio path (switcher, sequencer incl. switches, jumps, stops, snapshot exchange, request queue) for 3000 blocks: **0 heap operations**. (Locks cannot be counted this way; the audio path uses atomics only, by inspection.)
- Fuzzing: 6000 random/hostile bridge commands (wrong types, out-of-range, huge, truncated JSON) and ~4800 damaged/truncated state documents: the model always stays in range and round-trips, rejected documents leave the model untouched, 200,000-deep JSON nesting and 1 MB strings are rejected without crashing.
- `scripts/sanitize.sh`: all 26 engine + 20 bridge tests pass under ASan+UBSan and under TSan (including the snapshot-exchange producer/consumer thread test).
- Idle review: the page has no intervals or animation loops (rAF only while an update is pending; two one-shot timers). The native UI now sends `transport` only when play state or tempo changes (it used to fire ~30x/s while playing); `playheads` only on change; nothing is sent while stopped.
- Packaging: `scripts/package-mac.sh` builds, ad-hoc signs, verifies (`codesign --verify --deep --strict`) and zips to `dist/puls-macos-arm64.zip` + sha256; the extracted bundle verifies and loads in the headless host. arm64 only. Not notarized.

**Not done / pending:**
- **clap-validator: still not run.** Building it from source (`cargo install --git ... --tag 0.3.2`) was blocked again by this environment's permission policy. To do on a machine where you allow it: `cargo install --git https://github.com/free-audio/clap-validator --tag 0.3.2 --locked`, then `scripts/validate.sh` (it runs it automatically when on PATH).
- **CPU profile and idle-CPU check in Bitwig** (UI open/closed/hidden, 16x64 worst case at 999 bpm) need the real host; the numbers above are the engine in isolation.
- Linux and Windows builds: no host available; nothing is claimed for them (see the table in `README.md`). Resource paths for those platforms are implemented in `plugin/Makefile` but untested.
- Universal (x86_64) macOS binary, notarization.

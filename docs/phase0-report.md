# Phase 0 report (feasibility spike)

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

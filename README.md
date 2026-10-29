# Puls

A polyrhythmic **CLAP MIDI drum step sequencer** (note effect). It emits MIDI notes to the instrument that follows it
(e.g. Bitwig's Drum Machine); it is not a synth or sampler. Built with [DPF](https://github.com/DISTRHO/DPF) and its
built-in WebView; the UI is plain TypeScript + CSS.

## Features
- Up to 16 tracks x 64 steps per pattern, each track with its own length, resolution (1/4 to 1/64, triplets), phase, swing, note and channel, so tracks drift against each other (polyrhythm).
- Per step: velocity, probability, ratchets (1-8), micro-timing (+-50 %), gate.
- Per track: mute, solo, velocity / probability / duration scaling.
- 16-pattern bank; switching is quantized (next bar by default while playing, next beat, or immediate).
- Undo/redo, multi-select, copy/cut/paste of step blocks.
- Project state (all 16 patterns) is saved with the host project.

## Build and install (macOS)
Requires Xcode, CMake, Node 20. `git submodule update --init --recursive` first.

    scripts/build.sh          # UI (Vite) + plugin -> bin/puls.clap
    scripts/install-mac.sh    # copies to ~/Library/Audio/Plug-Ins/CLAP
    scripts/validate.sh       # unit tests + headless CLAP host test (+ clap-validator if installed)
    scripts/sanitize.sh       # the same tests under ASan/UBSan and TSan
    scripts/package-mac.sh    # ad-hoc codesign + dist/puls-macos-<arch>.zip (+ .sha256)

The ad-hoc signature is not notarization; a downloaded copy needs `xattr -dr com.apple.quarantine puls.clap`.

## Using it
Put Puls on a track *before* an instrument (note FX in Bitwig).

| Action | Gesture |
|---|---|
| Toggle a step | Click |
| Set velocity | Drag an active step up/down |
| Step menu (probability, ratchets, micro-timing, gate) | Right-click / Ctrl+click a step |
| Track menu (name, note, channel, resolution, length, phase, swing, ...) | Right-click a track label |
| Mute / Solo | M / S buttons on the label |
| Switch pattern | Click a slot (quantized); Alt+click = now; right-click for rename/copy/paste/duplicate/clear |
| Select / move | Shift+click, arrow keys, Shift+arrows |
| Toggle / clear / copy / cut / paste | Space or Enter / Delete / Ctrl-Cmd+C / X / V |
| Undo / redo | Toolbar, or Ctrl-Cmd+Z / Shift+Ctrl-Cmd+Z |

Keyboard shortcuts only work while the plugin window has keyboard focus (whether a host forwards keys to it varies).

## Architecture
`engine/` (pure C++, no DPF: sequencer, scheduler, pattern switcher, lock-free handoff) <- `model/` (editable state, undo) <-
`state/` (versioned JSON) <- `bridge/` (validated commands/events) <- `plugin/` (DPF glue) <-> `ui/` (WebView).
The audio thread never allocates, locks or parses JSON; the UI never schedules notes and closing it does not interrupt playback.
See `DECISIONS.md` for the design decisions (ADR-001...) and `docs/phase0-report.md` for per-phase results.

## Verified platforms
| Platform | Built | Automated tests | Run in a host |
|---|---|---|---|
| macOS arm64 | yes | engine, bridge/state, headless CLAP host; ASan/UBSan/TSan clean | Bitwig Studio: used by the author through Phase 4; per-feature checklists not recorded |
| macOS x86_64 / universal | no | no | no |
| Linux | no | no | no |
| Windows | no | no | no |

`clap-validator` has not been run (see `docs/phase0-report.md`). Only Bitwig has been tried; other CLAP hosts are untested.

## Known limitations
- Time-signature changes during playback are not tracked for bar-quantized pattern switches (the current block's signature is used).
- Extremely dense patterns (more than ~2000 note candidates in one block) drop the newest note-ons; note-offs are never dropped.
- No MIDI input, no per-step note/pitch, no audio output.

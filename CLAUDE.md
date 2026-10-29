# Puls — working rules

Polyrhythmic CLAP MIDI drum step sequencer (DPF + native DGL/NanoVG UI). Not a synth/sampler.

## Rules
1. Confirm DPF APIs against `external/dpf` source before using them. Never invent framework APIs/flags.
2. Engine (`engine/`) has no DPF/UI includes and is unit-testable headless.
3. Audio thread (`run()`): no allocation, locks, I/O, JSON, logging, UI calls.
4. UI never schedules musical events. Closing the UI must not interrupt playback.
5. No React/Electron/JUCE/WebView; the UI is DGL/NanoVG, minimal deps. Pin dependency versions. Keep UI geometry and command building DGL-free (`gui/`) so they stay unit-testable.
6. One testable milestone at a time; run build + tests after each; record decisions in DECISIONS.md.
7. Do not claim platform support until built and run there.

## Build (macOS)
`scripts/build.sh` — builds `plugin/` via DPF Makefile → `bin/puls.clap` (a bundle on macOS, a single file on Linux).
`scripts/install-mac.sh` copies it to `~/Library/Audio/Plug-Ins/CLAP`. `scripts/validate.sh` runs clap-validator if present.
build.sh forces the Xcode SDK: the Command Line Tools SDK here is newer than Xcode's linker (tapi "unknown architecture").
`scripts/sanitize.sh` runs the headless tests under ASan/UBSan/TSan; `scripts/package-mac.sh` signs (ad-hoc) and zips into `dist/`.

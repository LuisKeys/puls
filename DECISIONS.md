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

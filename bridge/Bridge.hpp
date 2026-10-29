#pragma once
#include "Auditioner.hpp"
#include "Model.hpp"
#include <string>
#include <vector>

namespace puls {

constexpr int kBridgeVersion = 1;
constexpr size_t kMaxBatchCells = 1024;

// Parses and validates UI commands against the Model and produces native->UI events (JSON strings).
// Main thread only. Never touches the audio thread: the caller republishes a snapshot when `changed` and
// forwards `switchRequest` and `auditions` to the audio thread's queues.
//
// Commands (all carry "version":1). They act on the selected pattern unless an "index" is given:
//   hello
//   toggleStep {track, step}                 setStepVelocity {track, step, velocity}
//   modifyStep {track, step, props}          modifyTrack {track, props}
//   editSteps {cells:[{track, step, props}]} (up to 1024 cells, atomic, one undo step)
//   addTrack {props?}                        removeTrack {track}
//   setSettings {settings:{anchorMode?, enabled?, patternSwitch?, audition?}}
//   selectPattern {index, quantize?:"now"|"beat"|"bar"}   views the pattern and requests it for playback
//   renamePattern {index, name}   copyPattern {index}   pastePattern {index}
//   duplicatePattern {index}      clearPattern {index}
//   undo, redo
// setStepVelocity, modifyStep and modifyTrack may carry a non-zero "gesture" id to coalesce a drag into one undo step.
// Events: init, stepChanged, stepsChanged, trackChanged, patternChanged, bankChanged, settings, history,
//         clipboard, error (transport and playheads are produced by the UI side from audio-thread telemetry).
class Bridge {
public:
    explicit Bridge(Model& model) : model_(model) {}

    struct Result {
        std::vector<std::string> events;
        bool changed = false;          // the Model was modified (snapshot must be republished)
        bool switchRequested = false;  // the audio thread should switch to `request`
        SwitchRequest request;
        // With settings.audition on: one note per track whose steps a step command switched on (toggleStep,
        // modifyStep, editSteps), at the velocity of the first such step.
        std::vector<AuditionNote> auditions;
    };

    Result handle(const char* json);
    std::string initEvent() const;
    static std::string errorEvent(const std::string& command, const std::string& message);

private:
    Model& model_;
};

} // namespace puls

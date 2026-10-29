#pragma once
#include "Model.hpp"
#include <string>
#include <vector>

namespace puls {

constexpr int kBridgeVersion = 1;

// Parses and validates UI commands against the Model and produces native->UI events (JSON strings).
// Main thread only. Never touches the audio thread: the caller republishes a snapshot when `changed`.
//
// Commands (all carry "version":1):
//   hello
//   toggleStep {track, step}                 setStepVelocity {track, step, velocity}
//   modifyStep {track, step, props}          modifyTrack {track, props}
//   addTrack {props?}                        removeTrack {track}
//   setSettings {settings:{anchorMode?, enabled?}}
// Events: init, stepChanged, trackChanged, patternChanged, settings, error
// (transport and playheads are produced by the UI side from audio-thread telemetry).
class Bridge {
public:
    explicit Bridge(Model& model) : model_(model) {}

    struct Result {
        std::vector<std::string> events;
        bool changed = false;   // the Model was modified (snapshot must be republished)
    };

    Result handle(const char* json);
    std::string initEvent() const;
    static std::string errorEvent(const std::string& command, const std::string& message);

private:
    Model& model_;
};

} // namespace puls

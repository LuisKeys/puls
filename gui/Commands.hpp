#pragma once
#include "Model.hpp"
#include "json.hpp"
#include <string>

// Builders for the Bridge command JSON (see bridge/Bridge.hpp for the schema). Main thread only.
namespace puls::gui::cmd {

std::string hello();
std::string undo();
std::string redo();
std::string toggleStep(uint32_t track, int step);
std::string setStepVelocity(uint32_t track, int step, int velocity, uint32_t gesture);
std::string modifyStep(uint32_t track, int step, const nlohmann::json& props, uint32_t gesture = 0);
std::string clearStep(uint32_t track, int step);   // every step property back to its default
nlohmann::json stepProps(const Step& s);           // all properties of a step, as editSteps/modifyStep props
// cells: [{track, step, props}], up to 1024; applied atomically as one undo step.
std::string editSteps(const nlohmann::json& cells);
// preview: play the track's note after setting `note`, even with the ♪ setting off.
std::string modifyTrack(uint32_t track, const nlohmann::json& props, uint32_t gesture = 0, bool preview = false);
std::string addTrack();
std::string removeTrack(uint32_t track);
std::string setSettings(const Settings& s);
std::string selectPattern(int index, bool now);
std::string renamePattern(int index, const std::string& name);
// copyPattern, pastePattern, duplicatePattern or clearPattern.
std::string patternAction(const char* type, int index);

} // namespace puls::gui::cmd

#pragma once
#include "Model.hpp"
#include "json.hpp"
#include <string>

namespace puls {

constexpr int kSchemaVersion = 2;   // v1 (single pattern) is migrated on load

// Sparse JSON: step fields equal to their default are omitted; trailing default steps are dropped.
nlohmann::json stepToJson(const Step& s);
nlohmann::json trackToJson(const Track& t, bool withSteps);
nlohmann::json patternToJson(const Pattern& p);
nlohmann::json settingsToJson(const Settings& s);
nlohmann::json bankSummaryToJson(const Model& m);   // [{name, used}] for all slots, for the pattern bank UI

// Validating readers. They apply only the keys that are present on top of `out`, and return false with a
// message (leaving `out` unspecified) if any present key has the wrong type or is out of range.
// Callers that must stay atomic pass a copy.
bool readStep(const nlohmann::json& j, Step& out, std::string& err);
bool readTrackProps(const nlohmann::json& j, Track& out, std::string& err);   // everything except id and steps
// Reads a whole pattern (name, seed, tracks with unique ids). Fills `maxId` with the largest track id.
bool readPattern(const nlohmann::json& j, Pattern& out, uint32_t& maxId, std::string& err);
bool readSettings(const nlohmann::json& j, Settings& out, std::string& err);

std::string saveState(const Model& m);
// Strict: any invalid value rejects the whole state and leaves `out` untouched.
bool loadState(const char* json, Model& out, std::string& err);

std::string dumpJson(const nlohmann::json& j);

} // namespace puls

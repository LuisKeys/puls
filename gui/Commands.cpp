#include "Commands.hpp"
#include "Bridge.hpp"
#include "Serialization.hpp"

namespace puls::gui::cmd {

using json = nlohmann::json;

namespace {
json command(const char* type) { return json{{"version", kBridgeVersion}, {"type", type}}; }
std::string str(const json& j) { return dumpJson(j); }
} // namespace

std::string hello() { return str(command("hello")); }
std::string undo() { return str(command("undo")); }
std::string redo() { return str(command("redo")); }

std::string toggleStep(uint32_t track, int step)
{
    json c = command("toggleStep");
    c["track"] = track;
    c["step"] = step;
    return str(c);
}

std::string setStepVelocity(uint32_t track, int step, int velocity, uint32_t gesture)
{
    json c = command("setStepVelocity");
    c["track"] = track;
    c["step"] = step;
    c["velocity"] = velocity;
    if (gesture != 0) c["gesture"] = gesture;
    return str(c);
}

std::string modifyStep(uint32_t track, int step, const json& props, uint32_t gesture)
{
    json c = command("modifyStep");
    c["track"] = track;
    c["step"] = step;
    c["props"] = props;
    if (gesture != 0) c["gesture"] = gesture;
    return str(c);
}

json stepProps(const Step& s)
{
    return {{"active", s.active}, {"velocity", s.velocity}, {"probability", s.probability},
            {"ratchets", s.ratchets}, {"microtiming", s.microtiming}, {"gate", s.gate}};
}

std::string clearStep(uint32_t track, int step) { return modifyStep(track, step, stepProps(Step{})); }

std::string editSteps(const json& cells)
{
    json c = command("editSteps");
    c["cells"] = cells;
    return str(c);
}

std::string modifyTrack(uint32_t track, const json& props, uint32_t gesture)
{
    json c = command("modifyTrack");
    c["track"] = track;
    c["props"] = props;
    if (gesture != 0) c["gesture"] = gesture;
    return str(c);
}

std::string addTrack() { return str(command("addTrack")); }

std::string removeTrack(uint32_t track)
{
    json c = command("removeTrack");
    c["track"] = track;
    return str(c);
}

std::string setSettings(const Settings& s)
{
    json c = command("setSettings");
    c["settings"] = settingsToJson(s);
    return str(c);
}

std::string selectPattern(int index, bool now)
{
    json c = command("selectPattern");
    c["index"] = index;
    if (now) c["quantize"] = "now";
    return str(c);
}

std::string renamePattern(int index, const std::string& name)
{
    json c = command("renamePattern");
    c["index"] = index;
    c["name"] = name;
    return str(c);
}

std::string patternAction(const char* type, int index)
{
    json c = command(type);
    c["index"] = index;
    return str(c);
}

} // namespace puls::gui::cmd

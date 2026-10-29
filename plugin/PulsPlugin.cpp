#include "PulsPlugin.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

START_NAMESPACE_DISTRHO

static constexpr const char* kStateKey = "state";
static constexpr const char* kCmdKey   = "cmd";

PulsPlugin::PulsPlugin() : Plugin(0, 0, 1)
{
    puls::makeDefaultPattern(fPattern);
}

std::vector<std::string> PulsPlugin::drainOutbox()
{
    std::lock_guard<std::mutex> lock(fOutboxMutex);
    std::vector<std::string> out;
    out.swap(fOutbox);
    return out;
}

void PulsPlugin::pushEvent(const std::string& json)
{
    std::lock_guard<std::mutex> lock(fOutboxMutex);
    if (fOutbox.size() < 256)
        fOutbox.push_back(json);
}

void PulsPlugin::initState(uint32_t index, State& state)
{
    if (index != 0)
        return;
    state.key = kStateKey;
    state.defaultValue = "{\"schemaVersion\":1,\"counter\":0}";
    state.label = "State";
    state.hints = kStateIsHostReadable | kStateIsHostWritable;
}

String PulsPlugin::getState(const char* key) const
{
    if (std::strcmp(key, kStateKey) != 0)
        return String();
    char buf[64];
    std::snprintf(buf, sizeof(buf), "{\"schemaVersion\":1,\"counter\":%d}", fCounter.load());
    return String(buf);
}

void PulsPlugin::setState(const char* key, const char* value)
{
    if (std::strcmp(key, kStateKey) == 0)
    {
        // Phase 0: minimal parse. Real implementation uses typed, validated JSON (state/).
        const char* c = std::strstr(value, "\"counter\":");
        const int parsed = c != nullptr ? std::atoi(c + 10) : 0;
        fCounter.store(parsed < 0 ? 0 : parsed);
    }
    else if (std::strcmp(key, kCmdKey) == 0)
    {
        handleCommand(value);
    }
}

void PulsPlugin::handleCommand(const char* json)
{
    if (std::strstr(json, "\"version\":1") == nullptr)
    {
        pushEvent("{\"version\":1,\"type\":\"error\",\"message\":\"unsupported version\"}");
        return;
    }
    if (std::strstr(json, "\"type\":\"hello\"") != nullptr)
    {
        // UI (re)opened: send full state, since DPF only pushes restored state before the page is ready.
        char buf[128];
        std::snprintf(buf, sizeof(buf), "{\"version\":1,\"type\":\"restored\",\"counter\":%d}", fCounter.load());
        pushEvent(buf);
        return;
    }
    if (std::strstr(json, "\"type\":\"ping\"") != nullptr)
    {
        const int n = ++fCounter;
        char buf[128];
        std::snprintf(buf, sizeof(buf), "{\"version\":1,\"type\":\"pong\",\"counter\":%d}", n);
        pushEvent(buf);
        return;
    }
    pushEvent("{\"version\":1,\"type\":\"error\",\"message\":\"unknown command\"}");
}

void PulsPlugin::activate()
{
    fSeq.reset();
}

void PulsPlugin::deactivate()
{
    fSeq.reset();
}

void PulsPlugin::run(const float**, float**, uint32_t frames)
{
    const TimePosition& pos = getTimePosition();
    const double bpm = pos.bbt.beatsPerMinute > 0.0 ? pos.bbt.beatsPerMinute : 120.0;

    // Absolute quarter-note position (DPF's CLAP bbt is derived from song_pos_beats, in quarters).
    double q0 = 0.0;
    if (pos.bbt.valid)
        q0 = (pos.bbt.bar - 1) * static_cast<double>(pos.bbt.beatsPerBar) + (pos.bbt.beat - 1)
           + pos.bbt.tick / pos.bbt.ticksPerBeat;

    telPlaying.store(pos.playing, std::memory_order_relaxed);
    telBpm.store(bpm, std::memory_order_relaxed);
    telQuarterPos.store(q0, std::memory_order_relaxed);
    telBbtValid.store(pos.bbt.valid, std::memory_order_relaxed);

    fSeq.process(fPattern, puls::TransportInfo{pos.playing && pos.bbt.valid, q0, bpm, getSampleRate()}, frames);

    for (uint32_t i = 0; i < fSeq.eventCount(); ++i)
    {
        const puls::MidiMsg& m = fSeq.events()[i];
        MidiEvent ev{};
        ev.frame = m.frame;
        ev.size = 3;
        ev.data[0] = m.data[0];
        ev.data[1] = m.data[1];
        ev.data[2] = m.data[2];
        writeMidiEvent(ev);
    }
}

Plugin* createPlugin()
{
    return new PulsPlugin();
}

END_NAMESPACE_DISTRHO

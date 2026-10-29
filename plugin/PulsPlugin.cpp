#include "PulsPlugin.hpp"
#include "Serialization.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

START_NAMESPACE_DISTRHO

static constexpr const char* kStateKey = "state";
static constexpr const char* kCmdKey   = "cmd";
static constexpr size_t kMaxOutbox = 512;

PulsPlugin::PulsPlugin()
    : Plugin(0, 0, 1), fModel(puls::makeDefaultModel()), fBridge(fModel)
{
    for (auto& p : telPlayhead)
        p.store(-1, std::memory_order_relaxed);
    publish();
}

void PulsPlugin::publish()
{
    puls::EngineSnapshot& slot = fExchange.writeSlot();
    slot.pattern = fModel.pattern;
    slot.settings = fModel.settings;
    fExchange.publish();
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
    if (fOutbox.size() >= kMaxOutbox)
    {
        // The UI is not draining (hidden/closed): its view is stale anyway, so replace the backlog by a full resync.
        fOutbox.clear();
        fOutbox.push_back(fBridge.initEvent());
        return;
    }
    fOutbox.push_back(json);
}

void PulsPlugin::initState(uint32_t index, State& state)
{
    if (index != 0)
        return;
    static const std::string defaultState = puls::saveState(puls::makeDefaultModel());
    state.key = kStateKey;
    state.defaultValue = defaultState.c_str();
    state.label = "State";
    state.hints = kStateIsHostReadable | kStateIsHostWritable;
}

String PulsPlugin::getState(const char* key) const
{
    if (std::strcmp(key, kStateKey) != 0)
        return String();
    return String(puls::saveState(fModel).c_str());
}

void PulsPlugin::setState(const char* key, const char* value)
{
    if (std::strcmp(key, kStateKey) == 0)
    {
        std::string err;
        if (puls::loadState(value, fModel, err))
        {
            publish();
            pushEvent(fBridge.initEvent());   // an already-open UI shows the restored project
        }
        else
        {
            pushEvent(puls::Bridge::errorEvent("restore", err));
        }
    }
    else if (std::strcmp(key, kCmdKey) == 0)
    {
        puls::Bridge::Result r = fBridge.handle(value);
        if (r.changed)
            publish();
        for (const std::string& ev : r.events)
            pushEvent(ev);
    }
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

    bool fresh;
    const puls::EngineSnapshot& snap = fExchange.acquire(fresh);
    fSeq.setAnchorMode(snap.settings.anchorMode);

    // A disabled sequencer is a stopped one: sounding notes are released and nothing is emitted.
    const bool play = snap.settings.enabled && pos.playing && pos.bbt.valid;
    fSeq.process(snap.pattern, puls::TransportInfo{play, q0, bpm, getSampleRate()}, frames);

    telTrackCount.store(snap.pattern.trackCount, std::memory_order_relaxed);
    for (int i = 0; i < puls::kMaxTracks; ++i)
        telPlayhead[i].store(static_cast<int8_t>(fSeq.playhead(i)), std::memory_order_relaxed);

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

#include "PulsPlugin.hpp"
#include "Serialization.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

START_NAMESPACE_DISTRHO

static constexpr const char* kStateKey = "state";
static constexpr const char* kCmdKey   = "cmd";
static constexpr size_t kMaxOutbox = 512;
static constexpr double kAuditionSeconds = 0.2;   // length of a preview note

PulsPlugin::PulsPlugin()
    : Plugin(0, 0, 1), fBridge(fModel)
{
    puls::makeDefaultBank(fModel);
    for (auto& p : telPlayhead)
        p.store(-1, std::memory_order_relaxed);
    publish();

    if (const char* path = std::getenv("PULS_TRACE"); path != nullptr && *path != '\0')
    {
        fTracePath = path;
        fTraceOn = true;
    }
}

PulsPlugin::~PulsPlugin()
{
    if (fTraceFile != nullptr)
        std::fclose(fTraceFile);
}

void PulsPlugin::drainTrace()
{
    if (!fTraceOn)
        return;
    if (fTraceFile == nullptr)
    {
        fTraceFile = std::fopen(fTracePath.c_str(), "a");
        if (fTraceFile == nullptr)
        {
            fTraceOn = false;   // unwritable path: give up quietly (records stop being queued too)
            return;
        }
        std::fprintf(fTraceFile, "# puls transport trace, sample rate %.0f; kind: 0 stopped 1 started 2 continuous "
                                 "3 resynced 4 resumed 5 jumped\nframe,frames,playing,bbt_valid,quarter,bpm,kind,events,head0,pattern\n",
                     getSampleRate());
    }
    TraceRecord r;
    bool wrote = false;
    while (fTrace.pop(r))
    {
        std::fprintf(fTraceFile, "%llu,%u,%u,%u,%.9f,%.4f,%u,%u,%d,%d\n", static_cast<unsigned long long>(r.frame), r.frames,
                     r.playing, r.bbtValid, r.quarter, r.bpm, r.kind, r.events, r.head0, r.pattern);
        wrote = true;
    }
    if (const uint32_t lost = fTraceDropped.exchange(0, std::memory_order_relaxed))
        std::fprintf(fTraceFile, "# %u records lost (editor closed or busy)\n", lost);
    if (wrote)
        std::fflush(fTraceFile);
}

void PulsPlugin::publish()
{
    puls::EngineSnapshot& slot = fExchange.writeSlot();
    slot.patterns = fModel.bank;
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
    static const std::string defaultState = [] {
        auto m = std::make_unique<puls::Model>();
        puls::makeDefaultBank(*m);
        return puls::saveState(*m);
    }();
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
            fSwitchQueue.push(puls::SwitchRequest{static_cast<uint8_t>(fModel.selected), puls::SwitchQuantize::Now});
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
        if (r.switchRequested)
            fSwitchQueue.push(r.request);   // full queue (UI hammering): the newest request is dropped
        for (const puls::AuditionNote& a : r.auditions)
            fAuditionQueue.push(a);         // same policy
        for (const std::string& ev : r.events)
            pushEvent(ev);
    }
}

void PulsPlugin::activate()
{
    fSeq.reset();
    fAudition.reset();
}

void PulsPlugin::deactivate()
{
    fSeq.reset();
    fAudition.reset();
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

    puls::SwitchRequest request;
    while (fSwitchQueue.pop(request))
        fSwitcher.request(request);

    // A disabled sequencer is a stopped one: sounding notes are released and nothing is emitted.
    const bool play = snap.settings.enabled && pos.playing && pos.bbt.valid;
    fSwitcher.plan(play, q0, pos.bbt.valid ? pos.bbt.beatsPerBar : 4.0);
    const puls::Pattern& current = snap.patterns[fSwitcher.current()];
    fSeq.process(current, puls::TransportInfo{play, q0, bpm, getSampleRate()}, frames,
                 fSwitcher.pending() ? &snap.patterns[fSwitcher.pendingIndex()] : nullptr, fSwitcher.switchAt());
    if (fSeq.switchCommitted())
        fSwitcher.commit();

    telPlayingPattern.store(static_cast<int8_t>(fSwitcher.current()), std::memory_order_relaxed);
    telTrackCount.store(current.trackCount, std::memory_order_relaxed);
    for (int i = 0; i < puls::kMaxTracks; ++i)
        telPlayhead[i].store(static_cast<int8_t>(fSeq.playhead(i)), std::memory_order_relaxed);

    if (fTraceOn)
    {
        const TraceRecord r{pos.frame, q0, bpm, frames, pos.playing, pos.bbt.valid, static_cast<uint8_t>(fSeq.blockKind()),
                            static_cast<uint8_t>(std::min<uint32_t>(fSeq.eventCount(), 255)),
                            static_cast<int8_t>(fSeq.playhead(0)), static_cast<int8_t>(fSwitcher.current())};
        if (!fTrace.push(r))
            fTraceDropped.fetch_add(1, std::memory_order_relaxed);
    }

    // Edit previews sound whether or not the host plays (and even with the sequencer off: they answer an edit).
    fAudition.begin();
    puls::AuditionNote a;
    const uint32_t auditionFrames = static_cast<uint32_t>(kAuditionSeconds * getSampleRate());
    while (fAuditionQueue.pop(a))
        fAudition.trigger(a, auditionFrames);
    fAudition.advance(frames);

    // Both lists are sorted by frame; merge them (previews first on equal frames).
    const puls::MidiMsg* seq = fSeq.events();
    const puls::MidiMsg* aud = fAudition.events();
    const uint32_t ns = fSeq.eventCount(), na = fAudition.eventCount();
    for (uint32_t i = 0, j = 0; i < ns || j < na;)
    {
        const puls::MidiMsg& m = (j < na && (i >= ns || aud[j].frame <= seq[i].frame)) ? aud[j++] : seq[i++];
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

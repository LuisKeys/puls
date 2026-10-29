#pragma once
#include "DistrhoPlugin.hpp"
#include "Bridge.hpp"
#include "Model.hpp"
#include "PatternSwitcher.hpp"
#include "Sequencer.hpp"
#include "SnapshotExchange.hpp"
#include "SpscQueue.hpp"
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

START_NAMESPACE_DISTRHO

// Puls: owns the Model (main thread), publishes EngineSnapshots to the audio thread, runs the Sequencer in run().
class PulsPlugin : public Plugin
{
public:
    PulsPlugin();
    ~PulsPlugin() override;

    // Telemetry (audio thread writes, UI thread reads).
    std::atomic<bool>   telPlaying{false};
    std::atomic<double> telBpm{0.0};
    std::atomic<double> telQuarterPos{0.0};
    std::atomic<bool>   telBbtValid{false};
    std::atomic<int8_t> telPlayhead[puls::kMaxTracks];   // step index per track, -1 when stopped
    std::atomic<uint8_t> telTrackCount{0};
    std::atomic<int8_t>  telPlayingPattern{0};           // bank slot the sequencer is playing

    // Main-thread outbox, drained by the UI in uiIdle.
    std::vector<std::string> drainOutbox();

    // Opt-in transport trace for diagnosing hosts: with PULS_TRACE=<file> in the environment, run() queues one
    // record per block (wait-free) and the main thread appends them as CSV here (called from the UI's idle).
    void drainTrace();

    // Main thread only (the CLAP UI runs there and sends commands through UI::setState, which is synchronous):
    // the native UI reads the Model directly instead of mirroring it from events.
    const puls::Model& model() const noexcept { return fModel; }

protected:
    const char* getLabel() const override       { return "puls"; }
    const char* getDescription() const override { return "Polyrhythmic drum step sequencer"; }
    const char* getMaker() const override       { return "AccelOne"; }
    const char* getHomePage() const override    { return "https://accelone.com"; }
    const char* getLicense() const override     { return "Proprietary"; }
    uint32_t    getVersion() const override     { return d_version(0, 0, 1); }
    int64_t     getUniqueId() const override    { return d_cconst('P', 'u', 'l', 's'); }

    void   initState(uint32_t index, State& state) override;
    String getState(const char* key) const override;
    void   setState(const char* key, const char* value) override;

    void activate() override;
    void deactivate() override;
    void run(const float**, float**, uint32_t frames) override;

private:
    void pushEvent(const std::string& json);
    void publish();

    // Main thread only.
    puls::Model  fModel;
    puls::Bridge fBridge;
    std::mutex   fOutboxMutex;           // guards fOutbox only; never taken in run()
    std::vector<std::string> fOutbox;

    // Main thread -> audio thread.
    puls::SnapshotExchange<puls::EngineSnapshot> fExchange;
    puls::SpscQueue<puls::SwitchRequest, 16>     fSwitchQueue;

    // Audio thread only.
    puls::Sequencer      fSeq;
    puls::PatternSwitcher fSwitcher;

    // Transport trace (audio thread -> main thread). Off unless PULS_TRACE is set when the plugin is created.
    struct TraceRecord {
        uint64_t frame;      // host steady time of the block's first sample
        double   quarter;    // position the engine derived from the host's bbt
        double   bpm;
        uint32_t frames;
        uint8_t  playing, bbtValid, kind, events;   // kind = Sequencer::BlockKind
        int8_t   head0, pattern;
    };
    bool fTraceOn = false;
    puls::SpscQueue<TraceRecord, 8192> fTrace;     // ~40 s of 512-sample blocks at 48 kHz
    std::atomic<uint32_t> fTraceDropped{0};
    std::string fTracePath;                        // main thread
    std::FILE*  fTraceFile = nullptr;
};

END_NAMESPACE_DISTRHO

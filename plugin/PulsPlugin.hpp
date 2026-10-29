#pragma once
#include "DistrhoPlugin.hpp"
#include "DefaultPattern.hpp"
#include "Sequencer.hpp"
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

START_NAMESPACE_DISTRHO

// Phase 0 spike plugin: one note per quarter, ping round-trip, persisted counter.
class PulsPlugin : public Plugin
{
public:
    PulsPlugin();

    // Telemetry (audio thread writes, UI thread reads).
    std::atomic<bool>   telPlaying{false};
    std::atomic<double> telBpm{0.0};
    std::atomic<double> telQuarterPos{0.0};
    std::atomic<bool>   telBbtValid{false};

    // Main-thread outbox, drained by the UI in uiIdle.
    std::vector<std::string> drainOutbox();

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
    void handleCommand(const char* json);

    std::atomic<int>  fCounter{0};
    std::mutex        fOutboxMutex;           // main thread only, never taken in run()
    std::vector<std::string> fOutbox;

    // audio-thread state
    puls::Sequencer fSeq;
    puls::Pattern   fPattern;  // Phase 1: fixed default pattern until the UI/bridge edits it
};

END_NAMESPACE_DISTRHO

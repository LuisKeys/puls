#include "DistrhoUI.hpp"
#include "PulsPlugin.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

namespace {
// DPF's UI::stateChanged() injects "key" and "value" into a JS string literal without escaping
// (see DistrhoUI.cpp "TODO escape"), so every payload crosses the bridge as base64.
std::string base64(const std::string& in)
{
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3)
    {
        const uint32_t n = (uint8_t(in[i]) << 16) | (uint8_t(in[i + 1]) << 8) | uint8_t(in[i + 2]);
        out += tbl[n >> 18]; out += tbl[(n >> 12) & 63]; out += tbl[(n >> 6) & 63]; out += tbl[n & 63];
    }
    if (i + 1 == in.size())
    {
        const uint32_t n = uint8_t(in[i]) << 16;
        out += tbl[n >> 18]; out += tbl[(n >> 12) & 63]; out += "==";
    }
    else if (i + 2 == in.size())
    {
        const uint32_t n = (uint8_t(in[i]) << 16) | (uint8_t(in[i + 1]) << 8);
        out += tbl[n >> 18]; out += tbl[(n >> 12) & 63]; out += tbl[(n >> 6) & 63]; out += '=';
    }
    return out;
}
}

class PulsUI : public UI
{
public:
    PulsUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        fPlugin = static_cast<PulsPlugin*>(getPluginInstancePointer());
    }

protected:
    void uiIdle() override
    {
        if (fPlugin == nullptr)
            return;

        for (const std::string& ev : fPlugin->drainOutbox())
            UI::stateChanged("evt", base64(ev).c_str());

        // Telemetry: at most ~30 Hz, and only when something changed. Nothing is sent while the transport is idle.
        const auto now = std::chrono::steady_clock::now();
        if (now - fLastTelemetry < std::chrono::milliseconds(33))
            return;
        fLastTelemetry = now;

        char buf[192];
        const bool playing = fPlugin->telPlaying.load(std::memory_order_relaxed);
        const double q = fPlugin->telQuarterPos.load(std::memory_order_relaxed);
        const double bpm = fPlugin->telBpm.load(std::memory_order_relaxed);
        if (playing != fLastPlaying || q != fLastQ || bpm != fLastBpm)
        {
            fLastPlaying = playing;
            fLastQ = q;
            fLastBpm = bpm;
            std::snprintf(buf, sizeof(buf),
                          "{\"version\":1,\"type\":\"transport\",\"playing\":%s,\"bpm\":%.3f,\"quarter\":%.4f}",
                          playing ? "true" : "false", bpm, q);
            UI::stateChanged("evt", base64(buf).c_str());
        }

        const int count = fPlugin->telTrackCount.load(std::memory_order_relaxed);
        int8_t heads[puls::kMaxTracks];
        for (int i = 0; i < puls::kMaxTracks; ++i)
            heads[i] = fPlugin->telPlayhead[i].load(std::memory_order_relaxed);
        const int pattern = fPlugin->telPlayingPattern.load(std::memory_order_relaxed);
        if (pattern != fLastPattern || count != fLastCount || std::memcmp(heads, fLastHeads, sizeof(heads)) != 0)
        {
            fLastCount = count;
            fLastPattern = pattern;
            std::memcpy(fLastHeads, heads, sizeof(heads));
            std::string json = "{\"version\":1,\"type\":\"playheads\",\"steps\":[";
            for (int i = 0; i < count; ++i)
                json += (i ? "," : "") + std::to_string(heads[i]);
            json += "],\"pattern\":" + std::to_string(pattern) + "}";
            UI::stateChanged("evt", base64(json).c_str());
        }
    }

    void stateChanged(const char*, const char*) override
    {
        // Host-restored state is not forwarded: the page asks for the full model with `hello`, and
        // PulsPlugin::setState queues an `init` event when a restore happens while the UI is open.
    }

private:
    PulsPlugin* fPlugin = nullptr;
    std::chrono::steady_clock::time_point fLastTelemetry{};
    bool   fLastPlaying = false;
    double fLastQ = -1.0;
    double fLastBpm = -1.0;
    int    fLastCount = -1;
    int    fLastPattern = -1;
    int8_t fLastHeads[puls::kMaxTracks] = {};
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PulsUI)
};

UI* createUI()
{
    return new PulsUI();
}

END_NAMESPACE_DISTRHO

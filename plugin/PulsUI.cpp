#include "DistrhoUI.hpp"
#include "PulsPlugin.hpp"
#include <cstdio>
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

        // Telemetry at idle rate; only send on change.
        char buf[160];
        const bool playing = fPlugin->telPlaying.load(std::memory_order_relaxed);
        const double q = fPlugin->telQuarterPos.load(std::memory_order_relaxed);
        const double bpm = fPlugin->telBpm.load(std::memory_order_relaxed);
        if (playing != fLastPlaying || q != fLastQ)
        {
            fLastPlaying = playing;
            fLastQ = q;
            std::snprintf(buf, sizeof(buf),
                          "{\"version\":1,\"type\":\"transport\",\"playing\":%s,\"bpm\":%.3f,\"quarter\":%.4f}",
                          playing ? "true" : "false", bpm, q);
            UI::stateChanged("evt", base64(buf).c_str());
        }
    }

    void stateChanged(const char* key, const char* value) override
    {
        // Persisted state restored by the host: forward to the page as well.
        UI::stateChanged(key, base64(value).c_str());
    }

private:
    PulsPlugin* fPlugin = nullptr;
    bool   fLastPlaying = false;
    double fLastQ = -1.0;
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PulsUI)
};

UI* createUI()
{
    return new PulsUI();
}

END_NAMESPACE_DISTRHO

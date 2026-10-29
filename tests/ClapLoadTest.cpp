// Phase 0 headless CLAP host: loads bin/puls.clap, checks metadata, note ports, state round-trip and
// that a playing transport produces correctly timed note-on/note-off. No Bitwig required.
#include <clap/entry.h>
#include <clap/host.h>
#include <clap/plugin.h>
#include <clap/plugin-features.h>
#include <clap/plugin-factory.h>
#include <clap/process.h>
#include <clap/events.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/state.h>
#include <clap/ext/gui.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

struct Ev { uint32_t time; uint16_t type; int key; int vel; };
struct Out { std::vector<Ev> evs; };

static bool tryPush(const clap_output_events_t* l, const clap_event_header_t* h)
{
    auto* o = static_cast<Out*>(l->ctx);
    if (h->type == CLAP_EVENT_NOTE_ON || h->type == CLAP_EVENT_NOTE_OFF) {
        auto* n = reinterpret_cast<const clap_event_note_t*>(h);
        o->evs.push_back({h->time, h->type, n->key, int(n->velocity * 127 + 0.5)});
    } else if (h->type == CLAP_EVENT_MIDI) {
        auto* m = reinterpret_cast<const clap_event_midi_t*>(h);
        const bool on = (m->data[0] & 0xF0) == 0x90 && m->data[2] > 0;
        o->evs.push_back({h->time, uint16_t(on ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF), m->data[1], m->data[2]});
    }
    return true;
}
static uint32_t inSize(const clap_input_events_t*) { return 0; }
static const clap_event_header_t* inGet(const clap_input_events_t*, uint32_t) { return nullptr; }

struct Buf { std::string data; size_t rd = 0; };
static int64_t wr(const clap_ostream_t* s, const void* b, uint64_t n) { static_cast<Buf*>(s->ctx)->data.append((const char*)b, n); return (int64_t)n; }
static int64_t rd(const clap_istream_t* s, void* b, uint64_t n) {
    auto* x = static_cast<Buf*>(s->ctx);
    const size_t k = std::min<size_t>(n, x->data.size() - x->rd);
    std::memcpy(b, x->data.data() + x->rd, k); x->rd += k; return (int64_t)k;
}

static const void* hostExt(const clap_host_t*, const char*) { return nullptr; }
static void hostNoop(const clap_host_t*) {}

int main(int argc, char** argv)
{
    const std::string bundle = argc > 1 ? argv[1] : "bin/puls.clap";
    void* lib = dlopen((bundle + "/Contents/MacOS/puls").c_str(), RTLD_NOW);
    if (!lib) lib = dlopen((bundle + "/Contents/x86_64-linux/puls.so").c_str(), RTLD_NOW);
    if (!lib) { std::printf("dlopen failed: %s\n", dlerror()); return 2; }
    auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(lib, "clap_entry"));
    CHECK(entry != nullptr);
    CHECK(entry->init(bundle.c_str()));
    auto* factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    CHECK(factory && factory->get_plugin_count(factory) == 1);

    const clap_plugin_descriptor_t* d = factory->get_plugin_descriptor(factory, 0);
    CHECK(std::string(d->id) == "com.accelone.puls");
    CHECK(std::string(d->name) == "Puls");
    bool noteEffect = false;
    for (const char* const* f = d->features; f && *f; ++f) noteEffect |= std::string(*f) == CLAP_PLUGIN_FEATURE_NOTE_EFFECT;
    CHECK(noteEffect);

    clap_host_t host{};
    host.clap_version = CLAP_VERSION; host.name = "puls-test"; host.vendor = "test"; host.url = ""; host.version = "0";
    host.get_extension = hostExt; host.request_restart = hostNoop; host.request_process = hostNoop; host.request_callback = hostNoop;

    const clap_plugin_t* p = factory->create_plugin(factory, &host, d->id);
    CHECK(p != nullptr);
    CHECK(p->init(p));

    auto* np = static_cast<const clap_plugin_note_ports_t*>(p->get_extension(p, CLAP_EXT_NOTE_PORTS));
    CHECK(np && np->count(p, false) == 1);
    auto* ap = static_cast<const clap_plugin_audio_ports_t*>(p->get_extension(p, CLAP_EXT_AUDIO_PORTS));
    CHECK(!ap || (ap->count(p, true) == 0 && ap->count(p, false) == 0));
    CHECK(p->get_extension(p, CLAP_EXT_GUI) != nullptr);

    // State: restore a counter, save, compare.
    auto* st = static_cast<const clap_plugin_state_t*>(p->get_extension(p, CLAP_EXT_STATE));
    CHECK(st != nullptr);
    {
        // DPF's CLAP state format is its own; round-trip through the plugin itself.
        Buf a; clap_ostream_t os{&a, wr};
        CHECK(st->save(p, &os));
        Buf b; b.data = a.data; clap_istream_t is{&b, rd};
        CHECK(st->load(p, &is));
        Buf c; clap_ostream_t os2{&c, wr};
        CHECK(st->save(p, &os2));
        CHECK(a.data == c.data);
        CHECK(a.data.find("\"schemaVersion\":2") != std::string::npos);

        // Restore a v1 (pre-bank) document in DPF's state container format: it must migrate into slot 0 and re-save as v2.
        const std::string json = "{\"schemaVersion\":1,\"pattern\":{\"name\":\"Restored\",\"seed\":1,\"tracks\":"
                                 "[{\"id\":3,\"name\":\"Zed\",\"note\":60,\"length\":5,\"steps\":[{\"active\":true,\"velocity\":99}]}]},"
                                 "\"settings\":{\"anchorMode\":\"host\",\"enabled\":true}}";
        std::string payload = std::string("__dpf_state_begin__") + '\0' + "state" + '\0' + json + '\0' + "__dpf_state_end__" + '\0' + '\xfe';
        Buf d; d.data = payload; clap_istream_t isd{&d, rd};
        CHECK(st->load(p, &isd));
        Buf e; clap_ostream_t ose{&e, wr};
        CHECK(st->save(p, &ose));
        CHECK(e.data.find("\"name\":\"Zed\"") != std::string::npos);
        CHECK(e.data.find("\"anchorMode\":\"host\"") != std::string::npos);

        // Corrupt state is rejected: the previous (restored) model stays.
        std::string bad = std::string("__dpf_state_begin__") + '\0' + "state" + '\0' + "{\"schemaVersion\":1,\"pattern\":{\"tracks\":[{\"id\":1,\"length\":999}]}}" + '\0' + "__dpf_state_end__" + '\0' + '\xfe';
        Buf f; f.data = bad; clap_istream_t isf{&f, rd};
        st->load(p, &isf);
        Buf g; clap_ostream_t osg{&g, wr};
        CHECK(st->save(p, &osg));
        CHECK(g.data == e.data);

        // Back to the default model for the playback checks below.
        Buf h; h.data = a.data; clap_istream_t ish{&h, rd};
        CHECK(st->load(p, &ish));
    }

    const double sr = 44100.0;
    CHECK(p->activate(p, sr, 32, 4096));
    CHECK(p->start_processing(p));

    clap_input_events_t in{nullptr, inSize, inGet};
    uint64_t steady = 0;
    const uint32_t N = 512;
    Out out;
    clap_output_events_t oe{&out, tryPush};
    for (int blk = 0; blk < 100; ++blk) {
        const double quarter = (blk * N) / sr * 2.0;   // 120 bpm = 2 quarters/sec
        clap_event_transport_t tr{};
        tr.header = {sizeof(tr), 0, 0, CLAP_EVENT_TRANSPORT, 0};
        tr.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE | CLAP_TRANSPORT_HAS_TIME_SIGNATURE | CLAP_TRANSPORT_IS_PLAYING;
        tr.song_pos_beats = (clap_beattime)(quarter * CLAP_BEATTIME_FACTOR);
        tr.tempo = 120.0; tr.tsig_num = 4; tr.tsig_denom = 4;
        clap_process_t pr{};
        pr.steady_time = (int64_t)steady; pr.frames_count = N; pr.transport = &tr;
        pr.in_events = &in; pr.out_events = &oe;
        const size_t before = out.evs.size();
        CHECK(p->process(p, &pr) != CLAP_PROCESS_ERROR);
        for (size_t i = before; i < out.evs.size(); ++i) out.evs[i].time += blk * N;   // absolute frame
        steady += N;
    }
    // Default pattern: kick on steps 0,4,8,12 (1/16 = 5512.5 frames @120bpm/44.1k), closed hat every 2nd step.
    std::vector<Ev> kick;
    int ons = 0, offs = 0;
    for (auto& e : out.evs) {
        (e.type == CLAP_EVENT_NOTE_ON ? ons : offs)++;
        if (e.key == 36) kick.push_back(e);
    }
    CHECK(kick.size() >= 6);
    if (kick.size() >= 6) {
        CHECK(kick[0].type == CLAP_EVENT_NOTE_ON && kick[0].time == 0 && kick[0].vel == 100);
        CHECK(kick[1].type == CLAP_EVENT_NOTE_OFF && kick[1].time >= 2756 && kick[1].time <= 2757); // gate 50% of a 1/16
        CHECK(kick[2].type == CLAP_EVENT_NOTE_ON && kick[2].time >= 22049 && kick[2].time <= 22051);
        CHECK(kick[4].type == CLAP_EVENT_NOTE_ON && kick[4].time >= 44099 && kick[4].time <= 44101);
    }
    CHECK(ons == offs || ons == offs + 1);
    std::printf("events: %zu (on=%d off=%d)\n", out.evs.size(), ons, offs);

    // Restoring a project whose selected slot is 1 switches playback to that pattern (kick -> note 60) while running.
    {
        std::string steps60;
        for (int i = 0; i < 16; ++i) steps60 += i ? ",{\"active\":true}" : "{\"active\":true}";
        const std::string track60 = "{\"id\":1,\"note\":60,\"steps\":[" + steps60 + "]}";
        const std::string doc = "{\"schemaVersion\":2,\"selected\":1,\"patterns\":[{\"name\":\"A\",\"tracks\":[{\"id\":1,\"note\":36,\"steps\":[{\"active\":true}]}]},"
                                "{\"name\":\"B\",\"tracks\":[" + track60 + "]}]}";
        std::string payload = std::string("__dpf_state_begin__") + '\0' + "state" + '\0' + doc + '\0' + "__dpf_state_end__" + '\0' + '\xfe';
        Buf d; d.data = payload; clap_istream_t isd{&d, rd};
        CHECK(st->load(p, &isd));
        const size_t first = out.evs.size();
        for (int blk = 100; blk < 140; ++blk) {
            const double quarter = (blk * N) / sr * 2.0;
            clap_event_transport_t tr{};
            tr.header = {sizeof(tr), 0, 0, CLAP_EVENT_TRANSPORT, 0};
            tr.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE | CLAP_TRANSPORT_HAS_TIME_SIGNATURE | CLAP_TRANSPORT_IS_PLAYING;
            tr.song_pos_beats = (clap_beattime)(quarter * CLAP_BEATTIME_FACTOR);
            tr.tempo = 120.0; tr.tsig_num = 4; tr.tsig_denom = 4;
            clap_process_t pr{};
            pr.steady_time = (int64_t)steady; pr.frames_count = N; pr.transport = &tr;
            pr.in_events = &in; pr.out_events = &oe;
            CHECK(p->process(p, &pr) != CLAP_PROCESS_ERROR);
            steady += N;
        }
        int on60 = 0, on36 = 0;
        for (size_t i = first; i < out.evs.size(); ++i)
            if (out.evs[i].type == CLAP_EVENT_NOTE_ON) { on60 += out.evs[i].key == 60; on36 += out.evs[i].key == 36; }
        CHECK(on60 >= 3);
        CHECK(on36 == 0);
    }

    p->stop_processing(p);
    p->deactivate(p);
    p->destroy(p);
    entry->deinit();
    std::printf(failures ? "FAILED (%d)\n" : "OK\n", failures);
    return failures ? 1 : 0;
}

#include "Templates.hpp"
#include "Serialization.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace puls {

using json = nlohmann::json;

namespace {

const char* const kEmbedded[] = {
#include "Templates.inc"
};

bool readText(const json& j, const char* key, bool required, size_t maxBytes, std::string& out, std::string& err)
{
    const auto it = j.find(key);
    if (it == j.end()) {
        if (required) err = std::string(key) + ": required";
        return !required;
    }
    if (!it->is_string() || it->get_ref<const std::string&>().size() > maxBytes) {
        err = std::string(key) + ": expected a string of at most " + std::to_string(maxBytes) + " bytes";
        return false;
    }
    out = it->get<std::string>();
    return true;
}

char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool sameName(std::string_view a, std::string_view b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return lower(x) == lower(y); });
}

bool readFile(const std::string& path, std::string& out)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    char buf[4096];
    size_t n;
    out.clear();
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0 && out.size() < (1u << 20)) out.append(buf, n);
    std::fclose(f);
    return true;
}

// Creates `path` and its missing parents.
bool makeDirs(const std::string& path)
{
    for (size_t i = 1; i <= path.size(); ++i) {
        if (i < path.size() && path[i] != '/') continue;
        const std::string part = path.substr(0, i);
        if (::mkdir(part.c_str(), 0755) != 0 && errno != EEXIST) return false;
    }
    return true;
}

} // namespace

bool parseTemplate(const json& j, KitTemplate& out, std::string& err)
{
    if (!j.is_object()) { err = "template: expected an object"; return false; }
    KitTemplate t;
    if (!readText(j, "id", true, kMaxTemplateNameBytes, t.id, err)) return false;
    if (!validTemplateId(t.id)) { err = "id: expected 1 to 31 of a-z, 0-9 and -"; return false; }
    if (!readText(j, "name", true, kMaxTemplateNameBytes, t.name, err)
        || !readText(j, "category", false, 64, t.category, err)
        || !readText(j, "source", false, 64, t.source, err)) return false;
    if (t.name.empty()) { err = "name: expected 1 to 31 bytes"; return false; }
    const auto tit = j.find("tracks");
    if (tit == j.end() || !tit->is_array() || tit->empty() || tit->size() > static_cast<size_t>(kMaxTracks)) {
        err = "tracks: expected an array of 1 to 16";
        return false;
    }
    Pattern check;   // to find a repeated note and channel with the editor's own rule
    for (const json& tj : *tit) {
        Track tr;
        if (!readTrackProps(tj, tr, err)) return false;
        if (tj.contains("steps") || tj.contains("id")) { err = "track: templates hold no ids or steps"; return false; }
        tr.id = static_cast<uint32_t>(t.tracks.size() + 1);
        if (const Track* o = noteOwner(check, tr.note, tr.channel, 0)) {
            err = std::string("track ") + tr.name + ": note " + std::to_string(tr.note) + " is already used by " + o->name;
            return false;
        }
        check.tracks[check.trackCount++] = tr;
        t.tracks.push_back(tr);
    }
    out = std::move(t);
    return true;
}

json templateToJson(const KitTemplate& t)
{
    json j;
    j["id"] = t.id;
    j["name"] = t.name;
    if (!t.category.empty()) j["category"] = t.category;
    if (!t.source.empty()) j["source"] = t.source;
    json tracks = json::array();
    for (const Track& tr : t.tracks) {
        json tj = trackToJson(tr, false);
        tj.erase("id");
        tracks.push_back(std::move(tj));
    }
    j["tracks"] = std::move(tracks);
    return j;
}

KitTemplate templateFromPattern(const Pattern& p, const std::string& id, const std::string& name)
{
    KitTemplate t;
    t.id = id;
    t.name = name;
    t.user = true;
    for (int i = 0; i < p.trackCount; ++i) {
        Track tr = p.tracks[i];
        for (Step& s : tr.steps) s = Step{};
        t.tracks.push_back(tr);
    }
    return t;
}

std::string templateSlug(std::string_view name)
{
    std::string s;
    for (const char c : name) {
        const char l = lower(c);
        if ((l >= 'a' && l <= 'z') || (l >= '0' && l <= '9')) s += l;
        else if (!s.empty() && s.back() != '-') s += '-';
        if (s.size() >= kMaxTemplateNameBytes) break;
    }
    while (!s.empty() && s.back() == '-') s.pop_back();
    return s;
}

void applyTemplate(const KitTemplate& t, Pattern& p, uint32_t& nextTrackId)
{
    for (Track& tr : p.tracks) tr = Track{};
    p.trackCount = 0;
    for (const Track& src : t.tracks) {
        if (p.trackCount >= kMaxTracks) break;
        Track& tr = p.tracks[p.trackCount++];
        tr = src;
        tr.id = nextTrackId++;
        for (Step& s : tr.steps) s = Step{};
    }
    std::memset(p.templateId, 0, sizeof(p.templateId));
    std::memcpy(p.templateId, t.id.data(), std::min(t.id.size(), sizeof(p.templateId) - 1));
}

size_t builtInTemplateSourceCount() { return sizeof(kEmbedded) / sizeof(kEmbedded[0]); }

const std::vector<KitTemplate>& builtInTemplates()
{
    static const std::vector<KitTemplate> list = [] {
        std::vector<KitTemplate> v;
        for (const char* src : kEmbedded) {
            KitTemplate t;
            std::string err;
            const json j = json::parse(src, nullptr, false);
            if (!j.is_discarded() && parseTemplate(j, t, err)) v.push_back(std::move(t));
        }
        return v;
    }();
    return list;
}

TemplateStore::TemplateStore(std::string dir) : dir_(std::move(dir)) { reload(); }

void TemplateStore::reload()
{
    list_ = builtInTemplates();
    std::vector<KitTemplate> mine;
    if (DIR* d = dir_.empty() ? nullptr : ::opendir(dir_.c_str())) {
        while (const dirent* e = ::readdir(d)) {
            const std::string fn = e->d_name;
            if (fn.size() <= 5 || fn.compare(fn.size() - 5, 5, ".json") != 0) continue;
            std::string text, err;
            if (!readFile(dir_ + "/" + fn, text)) continue;
            const json j = json::parse(text, nullptr, false);
            KitTemplate t;
            if (j.is_discarded() || !parseTemplate(j, t, err)) continue;
            if (fn != t.id + ".json" || find(t.id) != nullptr || findByName(t.name) != nullptr) continue;
            const bool dup = std::any_of(mine.begin(), mine.end(), [&](const KitTemplate& m) { return m.id == t.id || sameName(m.name, t.name); });
            if (dup) continue;
            t.user = true;
            mine.push_back(std::move(t));
        }
        ::closedir(d);
    }
    std::sort(mine.begin(), mine.end(), [](const KitTemplate& a, const KitTemplate& b) {
        return std::lexicographical_compare(a.name.begin(), a.name.end(), b.name.begin(), b.name.end(),
                                            [](char x, char y) { return lower(x) < lower(y); });
    });
    for (KitTemplate& t : mine) list_.push_back(std::move(t));
}

const KitTemplate* TemplateStore::find(std::string_view id) const noexcept
{
    for (const KitTemplate& t : list_)
        if (t.id == id) return &t;
    return nullptr;
}

const KitTemplate* TemplateStore::findByName(std::string_view name) const noexcept
{
    for (const KitTemplate& t : list_)
        if (sameName(t.name, name)) return &t;
    return nullptr;
}

bool TemplateStore::save(const std::string& name, const Pattern& p, bool overwrite, std::string& id, std::string& err)
{
    if (dir_.empty()) { err = "no folder for user templates"; return false; }
    if (name.empty() || name.size() > kMaxTemplateNameBytes) { err = "name: expected 1 to 31 bytes"; return false; }
    if (p.trackCount == 0) { err = "the pattern has no tracks"; return false; }
    if (const KitTemplate* same = findByName(name)) {
        if (!same->user) { err = "\"" + same->name + "\" is a built-in template"; return false; }
        if (!overwrite) { err = "a template named \"" + same->name + "\" already exists"; return false; }
        id = same->id;
    } else {
        const std::string base = templateSlug(name);
        if (base.empty()) { err = "name: needs a letter or digit"; return false; }
        id = base;
        for (int n = 2; find(id) != nullptr; ++n) {   // another name with the same slug, or a built-in's id
            const std::string suffix = "-" + std::to_string(n);
            id = base.substr(0, kMaxTemplateNameBytes - suffix.size()) + suffix;
        }
    }
    KitTemplate t = templateFromPattern(p, id, name);
    json j = templateToJson(t);
    KitTemplate check;
    if (!parseTemplate(j, check, err)) return false;   // e.g. an older pattern that repeats a note

    if (!makeDirs(dir_)) { err = "cannot create " + dir_ + ": " + std::strerror(errno); return false; }
    const std::string path = dir_ + "/" + id + ".json", tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (f == nullptr) { err = "cannot write " + tmp + ": " + std::strerror(errno); return false; }
    const std::string text = j.dump(2) + "\n";
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    if (std::fclose(f) != 0 || !ok || std::rename(tmp.c_str(), path.c_str()) != 0) {
        err = "cannot write " + path + ": " + std::strerror(errno);
        std::remove(tmp.c_str());
        return false;
    }
    reload();
    return true;
}

bool TemplateStore::remove(const std::string& id, std::string& err)
{
    const KitTemplate* t = find(id);
    if (t == nullptr) { err = "unknown template"; return false; }
    if (!t->user) { err = "built-in templates cannot be deleted"; return false; }
    const std::string path = dir_ + "/" + id + ".json";
    if (std::remove(path.c_str()) != 0 && errno != ENOENT) { err = "cannot delete " + path + ": " + std::strerror(errno); return false; }
    reload();
    return true;
}

std::string TemplateStore::defaultDirectory()
{
    const char* home = std::getenv("HOME");
#ifdef __APPLE__
    return home != nullptr && *home != '\0' ? std::string(home) + "/Library/Application Support/Puls/templates" : std::string();
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg == '/') return std::string(xdg) + "/puls/templates";
    return home != nullptr && *home != '\0' ? std::string(home) + "/.config/puls/templates" : std::string();
#endif
}

} // namespace puls

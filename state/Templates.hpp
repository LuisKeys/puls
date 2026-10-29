#pragma once
#include "Model.hpp"
#include "json.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace puls {

// A named track layout (names, notes, channels, colors, lengths, ...; never steps) that can be loaded into a
// pattern. Built-in templates are the files in templates/, embedded at build time; user templates are files of
// the same schema in a directory (see TemplateStore). Main thread only: never used by the audio thread.
//   {"id":"bitwig-v8-edrums-01", "name":"...", "category"?:"drums", "source"?:"Bitwig Studio",
//    "tracks":[{"name":"E-Tom3Hi", "note":40, "color":"#ff6256", ...track props}, ...]}
struct KitTemplate {
    std::string id;         // validTemplateId
    std::string name;       // 1..31 bytes, shown in the selector
    std::string category, source;
    bool        user = false;
    std::vector<Track> tracks;   // 1..16, no two with the same note and channel; ids and steps unused
};

constexpr size_t kMaxTemplateNameBytes = 31;

// Strict: any invalid value rejects the template.
bool parseTemplate(const nlohmann::json& j, KitTemplate& out, std::string& err);
nlohmann::json templateToJson(const KitTemplate& t);

// The track layout of `p` (steps dropped) as a template named `name`.
KitTemplate templateFromPattern(const Pattern& p, const std::string& id, const std::string& name);

// The id a user template named `name` gets: lower-case letters and digits, other runs become '-'; empty if none.
std::string templateSlug(std::string_view name);

// Replaces the tracks of `p` with the template's, in order, with fresh ids from `nextTrackId` and empty steps.
// Keeps the pattern's name and seed, and records the template's id.
void applyTemplate(const KitTemplate& t, Pattern& p, uint32_t& nextTrackId);

// Whether loading a template into `p` would lose something worth asking about (active steps).
inline bool templateNeedsConfirm(const Pattern& p) { return !isBlank(p); }

// The embedded templates, parsed once. An invalid one is left out (and fails the tests).
const std::vector<KitTemplate>& builtInTemplates();
size_t builtInTemplateSourceCount();

// Built-in templates plus the user's, kept in `dir` as one <id>.json file each. An empty dir means built-ins only.
class TemplateStore {
public:
    explicit TemplateStore(std::string dir = {});

    // Re-reads the directory; files that are not valid templates are skipped.
    void reload();
    // Built-ins first (in file order), then the user's by name.
    const std::vector<KitTemplate>& list() const noexcept { return list_; }
    const KitTemplate* find(std::string_view id) const noexcept;
    const KitTemplate* findByName(std::string_view name) const noexcept;   // case-insensitive (ASCII)
    const std::string& directory() const noexcept { return dir_; }

    // Saves the tracks of `p` as a user template. Fails if the name is empty, too long, belongs to a built-in
    // template, or to a user template and `overwrite` is false. On success `id` is the saved template's id.
    bool save(const std::string& name, const Pattern& p, bool overwrite, std::string& id, std::string& err);
    bool remove(const std::string& id, std::string& err);   // user templates only

    // $XDG_CONFIG_HOME/puls/templates (default ~/.config), or ~/Library/Application Support/Puls/templates on macOS.
    static std::string defaultDirectory();

private:
    std::string dir_;
    std::vector<KitTemplate> list_;
};

} // namespace puls

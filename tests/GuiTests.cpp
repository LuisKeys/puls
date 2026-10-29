#include "TestFramework.hpp"
#include "Bridge.hpp"
#include "Commands.hpp"
#include "EditorLayout.hpp"
#include "Menu.hpp"
#include "Selection.hpp"
#include "TextEdit.hpp"
#include "Serialization.hpp"

using namespace puls;
using namespace puls::gui;
using json = nlohmann::json;

static Model defaultModel() { Model m; makeDefaultBank(m); return m; }
static bool ok(const Bridge::Result& r)
{
    for (const std::string& e : r.events)
        if (json::parse(e).value("type", "") == "error") return false;
    return true;
}

static EditorLayout layout(float w = 900, float h = 360, int tracks = 8)
{
    EditorLayout l;
    l.update(w, h, tracks, kMaxSteps, false);
    return l;
}

TEST(cells_hit_test_to_their_track_and_step)
{
    const EditorLayout l = layout();
    for (int t : {0, 3, 7})
        for (int s : {0, 5, 15}) {
            const Rect c = l.cell(t, s);
            const Hit h = l.hitTest(c.cx(), c.cy(), Popup::None);
            CHECK(h.item == Item::Cell);
            CHECK_EQ(h.index, t);
            CHECK_EQ(h.step, s);
        }
    // Below the last track there is nothing.
    const Rect last = l.row(7);
    CHECK(l.hitTest(l.cells.x + 10, last.bottom() + 5, Popup::None).item == Item::None);
}

TEST(sixteen_steps_fit_the_default_width_and_the_rest_scrolls)
{
    EditorLayout l = layout();
    CHECK(l.cell(0, 15).right() <= l.width);
    CHECK(l.hscroll.w > 0);
    CHECK(l.maxScrollX() > 0);
    l.scrollBy(1e6f, 0);
    CHECK(l.scrollX == l.maxScrollX());
    const Rect c = l.cell(2, 63);
    CHECK(c.right() <= l.width);
    const Hit h = l.hitTest(c.cx(), c.cy(), Popup::None);
    CHECK(h.item == Item::Cell);
    CHECK_EQ(h.step, 63);
    l.scrollBy(-1e6f, -1e6f);
    CHECK(l.scrollX == 0.0f);
    CHECK(l.scrollY == 0.0f);   // eight rows fit: nothing to scroll vertically
}

TEST(labels_stay_put_and_hide_scrolled_cells)
{
    EditorLayout l = layout();
    l.scrollBy(200, 0);
    // A point in the label column hits the label controls, never a cell scrolled underneath it.
    const Rect m = l.mute(1), s = l.solo(1), n = l.length(1);
    CHECK(l.hitTest(m.cx(), m.cy(), Popup::None).item == Item::Mute);
    CHECK(l.hitTest(s.cx(), s.cy(), Popup::None).item == Item::Solo);
    CHECK(l.hitTest(n.cx(), n.cy(), Popup::None).item == Item::Length);
    const Hit lab = l.hitTest(l.name(1).cx(), l.name(1).cy(), Popup::None);
    CHECK(lab.item == Item::Label);
    CHECK_EQ(lab.index, 1);
}

TEST(many_tracks_scroll_vertically)
{
    EditorLayout l = layout(900, 360, kMaxTracks);
    CHECK(l.maxScrollY() > 0);
    l.scrollBy(0, 1e6f);
    const Rect c = l.cell(kMaxTracks - 1, 0);
    CHECK(c.bottom() <= l.grid.bottom());
    const Hit h = l.hitTest(c.cx(), c.cy(), Popup::None);
    CHECK(h.item == Item::Cell);
    CHECK_EQ(h.index, kMaxTracks - 1);
}

TEST(toolbar_and_bank_fit_the_minimum_width)
{
    const EditorLayout l = layout(EditorLayout::kMinWidth, EditorLayout::kMinHeight);
    CHECK(l.settings.right() <= l.width);
    CHECK(l.bankName.right() <= l.transport.x);
    for (int i = 0; i < 16; ++i) {
        const Rect r = l.slot(i);
        const Hit h = l.hitTest(r.cx(), r.cy(), Popup::None);
        CHECK(h.item == Item::Slot);
        CHECK_EQ(h.index, i);
    }
    CHECK(l.hitTest(l.undo.cx(), l.undo.cy(), Popup::None).item == Item::Undo);
    CHECK(l.hitTest(l.addTrack.cx(), l.addTrack.cy(), Popup::None).item == Item::AddTrack);
}

TEST(open_popups_own_the_pointer)
{
    const EditorLayout l = layout();
    const Rect o = l.resolutionOption(5);
    const Hit h = l.hitTest(o.cx(), o.cy(), Popup::Resolution);
    CHECK(h.item == Item::ResolutionOption);
    CHECK_EQ(h.index, 5);
    const Rect c = l.cell(7, 15);   // outside the popup: closes it instead of toggling a step
    CHECK(l.hitTest(c.cx(), c.cy(), Popup::Resolution).item == Item::None);
    const Rect a = l.anchorOption(1), sw = l.switchOption(2);
    CHECK(l.hitTest(a.cx(), a.cy(), Popup::Settings).item == Item::AnchorOption);
    CHECK(l.hitTest(sw.cx(), sw.cy(), Popup::Settings).item == Item::SwitchOption);
    CHECK(l.settingsPanel().right() <= l.width);
}

TEST(confirmation_bar_takes_clicks_over_the_grid)
{
    EditorLayout l;
    l.update(900, 360, 8, kMaxSteps, true);
    CHECK(l.hitTest(l.confirmYes.cx(), l.confirmYes.cy(), Popup::None).item == Item::ConfirmYes);
    CHECK(l.hitTest(l.confirmNo.cx(), l.confirmNo.cy(), Popup::None).item == Item::ConfirmNo);
    CHECK(l.hitTest(l.confirmText.cx(), l.confirmText.cy(), Popup::None).item == Item::None);
}

TEST(ui_commands_are_accepted_by_the_bridge)
{
    Model m = defaultModel(); Bridge b(m);
    CHECK(ok(b.handle(cmd::hello().c_str())));
    CHECK(ok(b.handle(cmd::toggleStep(1, 1).c_str())));
    CHECK(m.pattern().tracks[0].steps[1].active);
    CHECK(ok(b.handle(cmd::setStepVelocity(1, 1, 33, 7).c_str())));
    CHECK(ok(b.handle(cmd::setStepVelocity(1, 1, 44, 7).c_str())));
    CHECK_EQ(m.pattern().tracks[0].steps[1].velocity, 44);
    CHECK(ok(b.handle(cmd::modifyTrack(2, {{"length", 7}}, 9).c_str())));
    CHECK(ok(b.handle(cmd::modifyTrack(2, {{"mute", true}}).c_str())));
    CHECK(ok(b.handle(cmd::modifyTrack(2, {{"swing", 40}}).c_str())));
    CHECK(ok(b.handle(cmd::modifyTrack(2, {{"resolution", resolutionName(Resolution::SixteenthTriplet)}}).c_str())));
    const Track& t = m.pattern().tracks[1];
    CHECK_EQ(t.length, 7);
    CHECK(t.mute);
    CHECK_EQ(t.swing, 40);
    CHECK(t.resolution == Resolution::SixteenthTriplet);

    Settings s = m.settings;
    s.enabled = false;
    s.anchorMode = AnchorMode::HostAbsolute;
    s.patternSwitch = SwitchQuantize::Beat;
    CHECK(ok(b.handle(cmd::setSettings(s).c_str())));
    CHECK(!m.settings.enabled);
    CHECK(m.settings.anchorMode == AnchorMode::HostAbsolute);
    CHECK(m.settings.patternSwitch == SwitchQuantize::Beat);

    CHECK(ok(b.handle(cmd::addTrack().c_str())));
    CHECK_EQ(m.pattern().trackCount, 9);
    CHECK(ok(b.handle(cmd::removeTrack(m.pattern().tracks[8].id).c_str())));
    CHECK_EQ(m.pattern().trackCount, 8);

    const Bridge::Result r = b.handle(cmd::selectPattern(3, true).c_str());
    CHECK(ok(r));
    CHECK(r.switchRequested);
    CHECK(r.request.quantize == SwitchQuantize::Now);
    CHECK_EQ(m.selected, 3);
    CHECK(b.handle(cmd::selectPattern(0, false).c_str()).request.quantize == SwitchQuantize::Beat);   // the setting

    CHECK(ok(b.handle(cmd::undo().c_str())));
    CHECK(ok(b.handle(cmd::redo().c_str())));
}

TEST(a_velocity_drag_is_one_undo_step)
{
    Model m = defaultModel(); Bridge b(m);
    const int before = m.pattern().tracks[3].steps[0].velocity;
    for (int v = 50; v <= 60; ++v)
        b.handle(cmd::setStepVelocity(4, 0, v, 42).c_str());
    CHECK_EQ(m.pattern().tracks[3].steps[0].velocity, 60);
    b.handle(cmd::undo().c_str());
    CHECK_EQ(m.pattern().tracks[3].steps[0].velocity, before);
}

// ---- text editing ----

TEST(text_edit_replaces_on_first_key_and_respects_utf8_and_the_byte_limit)
{
    TextEdit e;
    e.begin("Kick", 15, false);
    CHECK(e.allSelected());
    e.insert("B");
    CHECK(e.text() == "B");
    e.insert("ombo");
    e.left(); e.left();
    e.insert("\xC3\xA9");             // é (2 bytes) in the middle
    CHECK(e.text() == "Bom\xC3\xA9" "bo");
    e.backspace();                    // removes the whole é, not half of it
    CHECK(e.text() == "Bombo");
    e.end();
    e.insert("\x08\x1b\t");           // control characters are dropped
    CHECK(e.text() == "Bombo");
    e.insert("0123456789abcdef");     // stops at 15 bytes
    CHECK_EQ(e.text().size(), 15u);
    e.begin("x", 15, false);
    e.del();                          // deleting over the selection clears it
    CHECK(e.text().empty());
}

TEST(numeric_text_edit_accepts_digits_and_one_leading_minus)
{
    TextEdit e;
    e.begin("12", 4, true);
    e.insert("-a5-0");
    CHECK(e.text() == "-50");
    e.home();
    e.insert("-");                    // already negative
    CHECK(e.text() == "-50");
    e.end();
    e.insert("99");                   // 4 bytes max
    CHECK(e.text() == "-509");
}

// ---- menus ----

static MenuTarget stepTarget(const Model& m, uint32_t track, int step) { return {MenuKind::Step, track, step, m.selected}; }
static MenuTarget trackTarget(const Model& m, uint32_t track) { return {MenuKind::Track, track, 0, m.selected}; }

TEST(menus_fit_the_minimum_window_and_hit_their_controls)
{
    const Model m = defaultModel();
    for (MenuKind k : {MenuKind::Step, MenuKind::Track, MenuKind::Pattern}) {
        Menu menu;
        menu.open({k, 1, 3, 0}, 790, 230, EditorLayout::kMinWidth, EditorLayout::kMinHeight);   // opened near the corner
        CHECK(menu.isOpen());
        CHECK(menu.panel.x >= 0 && menu.panel.right() <= EditorLayout::kMinWidth);
        CHECK(menu.panel.y >= 0 && menu.panel.bottom() <= EditorLayout::kMinHeight);
        for (int i = 0; i < menu.count; ++i) {
            CHECK(menu.row(i).bottom() <= menu.panel.bottom());
            const FieldDef& f = menu.fields[i];
            if (f.type == FieldType::Range) {
                const Rect n = menu.number(i), s = menu.slider(i);
                CHECK(menu.hitTest(n.cx(), n.cy()).part == MenuPart::Number);
                CHECK(menu.hitTest(s.cx(), s.cy()).part == MenuPart::Slider);
                CHECK_EQ(menu.sliderValueAt(i, s.x), f.min);
                CHECK_EQ(menu.sliderValueAt(i, s.right()), f.max);
            }
            if (f.type == FieldType::Color) {
                const Rect sw = menu.swatch(i, kPaletteSize - 1);
                CHECK(sw.right() <= menu.row(i).right());
                const MenuHit h = menu.hitTest(sw.cx(), sw.cy());
                CHECK(h.part == MenuPart::Swatch);
                CHECK_EQ(h.sub, kPaletteSize - 1);
            }
        }
        CHECK(menu.hitTest(menu.panel.x - 2, menu.panel.y).part == MenuPart::None);
    }
}

TEST(step_menu_fields_round_trip_through_the_bridge)
{
    Model m = defaultModel(); Bridge b(m);
    const MenuTarget t = stepTarget(m, 1, 0);
    CHECK(targetValid(m, t));
    CHECK(menuTitle(m, t) == "Kick \xC2\xB7 step 1");
    const std::pair<FieldKey, int> sets[] = {
        {FieldKey::StepVelocity, 77}, {FieldKey::StepProbability, 40}, {FieldKey::StepRatchets, 3},
        {FieldKey::StepMicro, -20}, {FieldKey::StepGate, 90}, {FieldKey::StepActive, 0},
    };
    for (const auto& [k, v] : sets) {
        CHECK(ok(b.handle(fieldCommand(m, t, k, v, "", 0).c_str())));
        CHECK_EQ(fieldValue(m, t, k), v);
    }
    CHECK(ok(b.handle(fieldCommand(m, t, FieldKey::StepVelocity, 999, "", 0).c_str())));   // clamped
    CHECK_EQ(fieldValue(m, t, FieldKey::StepVelocity), 127);
    CHECK(ok(b.handle(fieldCommand(m, t, FieldKey::StepClear, 0, "", 0).c_str())));
    const Step& st = m.pattern().tracks[0].steps[0];
    const Step d;
    CHECK(!st.active);
    CHECK_EQ(st.velocity, d.velocity);
    CHECK_EQ(st.ratchets, d.ratchets);
    CHECK_EQ(st.microtiming, d.microtiming);
}

TEST(track_menu_fields_round_trip_through_the_bridge)
{
    Model m = defaultModel(); Bridge b(m);
    const MenuTarget t = trackTarget(m, 2);
    const std::pair<FieldKey, int> sets[] = {
        {FieldKey::TrackNote, 40}, {FieldKey::TrackChannel, 10}, {FieldKey::TrackResolution, 5}, {FieldKey::TrackLength, 12},
        {FieldKey::TrackPhase, 3}, {FieldKey::TrackVelocityPct, 150}, {FieldKey::TrackProbabilityPct, 60},
        {FieldKey::TrackSwing, 25}, {FieldKey::TrackDurationPct, 30}, {FieldKey::TrackMute, 1}, {FieldKey::TrackSolo, 1},
        {FieldKey::TrackColor, static_cast<int>(kPalette[12])},
    };
    for (const auto& [k, v] : sets) {
        CHECK(ok(b.handle(fieldCommand(m, t, k, v, "", 0).c_str())));
        CHECK_EQ(fieldValue(m, t, k), v);
    }
    CHECK_EQ(m.pattern().tracks[1].channel, 9);   // shown 1-based
    CHECK(fieldValueText(FieldKey::TrackResolution, 5) == "1/8T");

    CHECK(ok(b.handle(fieldCommand(m, t, FieldKey::TrackName, 0, "  Rim  ", 0).c_str())));
    CHECK(fieldText(m, t, FieldKey::TrackName) == "Rim");                 // trimmed
    CHECK(fieldCommand(m, t, FieldKey::TrackName, 0, "   ", 0).empty());  // nothing valid to send
    CHECK(fieldCommand(m, t, FieldKey::TrackName, 0, "0123456789abcdef", 0).empty());

    // A drag on one slider (same gesture id) is a single undo step.
    for (int v = 1; v <= 9; ++v) b.handle(fieldCommand(m, t, FieldKey::TrackPhase, v, "", 77).c_str());
    CHECK_EQ(fieldValue(m, t, FieldKey::TrackPhase), 9);
    b.handle(cmd::undo().c_str());
    CHECK_EQ(fieldValue(m, t, FieldKey::TrackPhase), 3);
}

TEST(menus_go_stale_when_their_target_disappears)
{
    Model m = defaultModel(); Bridge b(m);
    const MenuTarget t = trackTarget(m, 8);
    b.handle(cmd::removeTrack(8).c_str());
    CHECK(!targetValid(m, t));
    CHECK(fieldCommand(m, t, FieldKey::TrackNote, 50, "", 0).empty());
    const MenuTarget s = stepTarget(m, 1, 0);
    b.handle(cmd::selectPattern(2, true).c_str());   // track 1 exists in slot 2 too, but the menu belongs to slot 0
    CHECK(!targetValid(m, s));
}

TEST(pattern_menu_actions_round_trip_through_the_bridge)
{
    Model m = defaultModel(); Bridge b(m);
    const MenuTarget t{MenuKind::Pattern, 0, 0, 0};
    CHECK(!fieldEnabled(m, t, FieldKey::PatternPaste));
    CHECK(fieldCommand(m, t, FieldKey::PatternPaste, 0, "", 0).empty());
    CHECK(ok(b.handle(fieldCommand(m, t, FieldKey::PatternName, 0, "Verse", 0).c_str())));
    CHECK(fieldText(m, t, FieldKey::PatternName) == "Verse");
    CHECK(ok(b.handle(fieldCommand(m, t, FieldKey::PatternCopy, 0, "", 0).c_str())));
    CHECK(fieldEnabled(m, t, FieldKey::PatternPaste));
    const MenuTarget t5{MenuKind::Pattern, 0, 0, 5};
    CHECK(ok(b.handle(fieldCommand(m, t5, FieldKey::PatternPaste, 0, "", 0).c_str())));
    CHECK(std::string(m.bank[5].name) == "Verse");
    CHECK(ok(b.handle(fieldCommand(m, t5, FieldKey::PatternClear, 0, "", 0).c_str())));
    CHECK(isBlank(m.bank[5]));
    CHECK(ok(b.handle(fieldCommand(m, t, FieldKey::PatternDuplicate, 0, "", 0).c_str())));
    CHECK_EQ(m.selected, 1);                         // the next free slot, now shown
    const Bridge::Result r = b.handle(fieldCommand(m, t5, FieldKey::PatternPlayNow, 0, "", 0).c_str());
    CHECK(ok(r));
    CHECK(r.switchRequested);
    CHECK(r.request.quantize == SwitchQuantize::Now);
}

// ---- selection and clipboard ----

TEST(arrows_place_then_move_and_shift_extends)
{
    Selection s;
    s.move(0, 1, false, 8, 3);                 // first press: only places the cursor on the selected track
    CHECK(s.active());
    CHECK_EQ(s.cursor().track, 3);
    CHECK_EQ(s.cursor().step, 0);
    s.move(1, 2, true, 8, 0);                  // Shift: grows from the anchor
    GridRect r = s.rect();
    CHECK_EQ(r.t0, 3); CHECK_EQ(r.t1, 4); CHECK_EQ(r.s0, 0); CHECK_EQ(r.s1, 2);
    CHECK(s.multi());
    CHECK_EQ(r.cells(), 6);
    s.move(-9, -9, true, 8, 0);                // clamped at the edges, anchor kept
    r = s.rect();
    CHECK_EQ(r.t0, 0); CHECK_EQ(r.t1, 3); CHECK_EQ(r.s0, 0); CHECK_EQ(r.s1, 0);
    s.move(0, 99, false, 8, 0);                // plain arrow collapses onto the cursor
    CHECK(!s.multi());
    CHECK_EQ(s.cursor().step, kMaxSteps - 1);
    s.extendTo({5, 60});
    s.clamp(2);                                // tracks removed under the selection
    CHECK_EQ(s.rect().t1, 1);
    s.clamp(0);
    CHECK(!s.active());
}

TEST(toggle_and_clear_act_on_the_whole_block_as_one_undo_step)
{
    Model m = defaultModel(); Bridge b(m);
    const GridRect r{0, 1, 0, 3};              // Kick has step 0 on, the rest is off: not all on -> all on
    CHECK(ok(b.handle(toggleCommand(m.pattern(), r).c_str())));
    for (int t = 0; t <= 1; ++t)
        for (int st = 0; st <= 3; ++st) CHECK(m.pattern().tracks[t].steps[st].active);
    CHECK(ok(b.handle(toggleCommand(m.pattern(), r).c_str())));   // all on -> all off
    CHECK(!m.pattern().tracks[0].steps[0].active);
    b.handle(cmd::undo().c_str());
    CHECK(m.pattern().tracks[1].steps[2].active);

    m.pattern().tracks[3].steps[0].ratchets = 5;
    CHECK(ok(b.handle(clearCommand(m.pattern(), {3, 3, 0, 1}).c_str())));
    CHECK(!m.pattern().tracks[3].steps[0].active);
    CHECK_EQ(m.pattern().tracks[3].steps[0].ratchets, 1);
    CHECK(m.pattern().tracks[3].steps[2].active);   // outside the block
    CHECK(toggleCommand(m.pattern(), {}).empty());
    CHECK(toggleCommand(m.pattern(), {9, 12, 0, 0}).empty());   // no such tracks
}

TEST(copy_and_paste_keep_every_step_property_and_clip_at_the_edges)
{
    Model m = defaultModel(); Bridge b(m);
    Step& src = m.pattern().tracks[3].steps[2];
    src.ratchets = 3; src.probability = 40; src.microtiming = -10; src.gate = 80; src.velocity = 33;
    const StepBlock blk = copyBlock(m.pattern(), {3, 4, 2, 5});
    CHECK_EQ(blk.size(), 2u);
    CHECK_EQ(blk[0].size(), 4u);

    GridRect pasted;
    CHECK(ok(b.handle(pasteCommand(m.pattern(), blk, {6, 62}, pasted).c_str())));
    CHECK_EQ(pasted.t0, 6); CHECK_EQ(pasted.t1, 7); CHECK_EQ(pasted.s0, 62); CHECK_EQ(pasted.s1, 63);   // clipped
    const Step& dst = m.pattern().tracks[6].steps[62];
    CHECK(dst.active);
    CHECK_EQ(dst.ratchets, 3); CHECK_EQ(dst.probability, 40); CHECK_EQ(dst.microtiming, -10);
    CHECK_EQ(dst.gate, 80); CHECK_EQ(dst.velocity, 33);
    b.handle(cmd::undo().c_str());             // the whole paste is one undo step
    CHECK(!m.pattern().tracks[6].steps[62].active);

    CHECK(pasteCommand(m.pattern(), blk, {8, 0}, pasted).empty());   // below the last track
    CHECK(pasted.empty());

    // The largest block (16 tracks x 64 steps) is within the Bridge's 1024-cell batch limit.
    for (int i = 0; i < 8; ++i) b.handle(cmd::addTrack().c_str());
    CHECK_EQ(m.pattern().trackCount, kMaxTracks);
    CHECK(ok(b.handle(toggleCommand(m.pattern(), {0, kMaxTracks - 1, 0, kMaxSteps - 1}).c_str())));
}

TEST(the_cursor_is_scrolled_into_view)
{
    EditorLayout l;
    l.update(900, 240, kMaxTracks, kMaxSteps, false);
    l.ensureVisible(kMaxTracks - 1, kMaxSteps - 1);
    Rect c = l.cell(kMaxTracks - 1, kMaxSteps - 1);
    CHECK(c.right() <= l.cells.right() + 0.01f);
    CHECK(c.y + EditorLayout::kRowH <= l.grid.bottom() + 0.01f);
    l.ensureVisible(0, 0);
    c = l.cell(0, 0);
    CHECK(c.x >= l.cells.x - 0.01f);
    CHECK(c.y >= l.grid.y - 0.01f);
}

int main() { return testfw::runAll(); }

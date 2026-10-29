#include "TestFramework.hpp"
#include "Bridge.hpp"
#include "Commands.hpp"
#include "EditorLayout.hpp"
#include "Help.hpp"
#include "Keyboard.hpp"
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
    CHECK(l.hitTest(l.audition.cx(), l.audition.cy(), Popup::None).item == Item::Audition);
    CHECK(l.enable.right() <= l.audition.x && l.audition.right() <= l.patternName.x);
    CHECK(l.hitTest(l.keys.cx(), l.keys.cy(), Popup::None).item == Item::Keys);
    CHECK(l.trackName.right() <= l.keys.x && l.keys.right() <= l.resLabel.x);
    CHECK(l.help.right() <= l.width);
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

// ---- help ----

TEST(help_panel_is_centered_and_scrolls_its_measured_content)
{
    HelpLayout h;
    h.update(EditorLayout::kMinWidth, EditorLayout::kMinHeight);
    CHECK(h.panel.w <= HelpLayout::kMaxW);
    CHECK(h.panel.x >= 0 && h.panel.right() <= EditorLayout::kMinWidth);
    CHECK(h.panel.bottom() <= EditorLayout::kMinHeight);
    CHECK(h.body.bottom() <= h.panel.bottom());
    CHECK(h.hitTest(h.close.cx(), h.close.cy()) == HelpHit::Close);
    CHECK(h.hitTest(h.body.cx(), h.body.cy()) == HelpHit::Panel);
    CHECK(h.hitTest(2, 2) == HelpHit::Outside);

    h.scrollBy(100);
    CHECK(h.scroll == 0.0f);                   // nothing measured yet: nothing to scroll
    h.setContentHeight(h.body.h + 300);
    h.scrollBy(1000);
    CHECK(h.scroll == 300.0f);
    h.update(900, 2000);                       // a taller window shows everything: the scroll resets
    CHECK(h.scroll == 0.0f);

    int rows = 0;
    for (int i = 0; i < kHelpSectionCount; ++i) {
        CHECK(kHelpSections[i].count > 0);
        rows += kHelpSections[i].count;
    }
    CHECK(rows > 20);
}

// ---- note keyboard ----

TEST(note_names_follow_c3_is_60_and_gm_drums_are_named)
{
    CHECK(noteName(60) == "C3");
    CHECK(noteName(36) == "C1");
    CHECK(noteName(37) == "C#1");
    CHECK(noteName(0) == "C-2");
    CHECK(noteName(127) == "G8");
    CHECK(noteName(128).empty());
    CHECK(std::string(gmDrumName(36)) == "Bass Drum 1");
    CHECK(std::string(gmDrumName(42)) == "Closed Hi-Hat");
    CHECK(std::string(gmDrumName(81)) == "Open Triangle");
    CHECK(gmDrumName(34) == nullptr && gmDrumName(82) == nullptr);
    int black = 0;
    for (int n = 0; n < 12; ++n) black += isBlackKey(n);
    CHECK_EQ(black, 5);
    CHECK(isBlackKey(61) && !isBlackKey(64) && !isBlackKey(65));
}

TEST(keyboard_fits_the_minimum_window_with_sixteen_tracks)
{
    KeyboardLayout k;
    k.update(EditorLayout::kMinWidth, EditorLayout::kMinHeight, kMaxTracks);
    CHECK(k.panel.x >= 0 && k.panel.right() <= EditorLayout::kMinWidth);
    CHECK(k.panel.y >= 0 && k.panel.bottom() <= EditorLayout::kMinHeight);
    CHECK(k.octaves >= 3);
    CHECK(k.keys.h >= 60);
    CHECK(k.chip(kMaxTracks - 1).bottom() <= k.info.y);
    CHECK(k.chip(KeyboardLayout::kChipsPerRow - 1).right() <= k.panel.right());
    for (int t = 0; t < kMaxTracks; ++t) {
        const Rect c = k.chip(t);
        const KeysHit h = k.hitTest(c.cx(), c.cy());
        CHECK(h.part == KeysPart::Track);
        CHECK_EQ(h.index, t);
    }
    CHECK(k.hitTest(k.close.cx(), k.close.cy()).part == KeysPart::Close);
    CHECK(k.hitTest(k.prev.cx(), k.prev.cy()).part == KeysPart::Prev);
    CHECK(k.hitTest(k.next.cx(), k.next.cy()).part == KeysPart::Next);
    CHECK(k.hitTest(k.info.cx(), k.info.cy()).part == KeysPart::Panel);
    CHECK(k.hitTest(2, 2).part == KeysPart::Outside);
    for (int n = 0; n < 128; ++n)   // even the smallest window has room for one dot per key
        if (k.key(n).w > 0) CHECK(k.markerCapacity(n) >= 1);
}

TEST(every_visible_key_hits_itself_and_black_keys_lie_on_top)
{
    for (float w : {EditorLayout::kMinWidth, 1400.0f, 3000.0f}) {
        KeyboardLayout k;
        k.update(w, 400, 8);
        for (int first = 0; first <= k.maxFirstOctave(); ++first) {
            k.firstOctave = first;
            int shown = 0;
            for (int n = 0; n < 128; ++n) {
                const Rect r = k.key(n);
                if (r.w <= 0) continue;
                ++shown;
                CHECK(r.x >= k.keys.x - 0.01f && r.right() <= k.keys.right() + 0.01f);
                // White keys are hit below the black ones; black keys anywhere.
                const float y = isBlackKey(n) ? r.cy() : r.bottom() - 4;
                const KeysHit h = k.hitTest(r.cx(), y);
                CHECK(h.part == KeysPart::Key);
                CHECK_EQ(h.index, n);
                for (int m = 0; m < k.markerCapacity(n); ++m) {
                    const Rect d = k.marker(n, m);
                    const KeysHit dh = k.hitTest(d.cx(), d.cy());   // a dot never sits on another key
                    CHECK(dh.part == KeysPart::Key);
                    CHECK_EQ(dh.index, n);
                }
                CHECK(k.marker(n, k.markerCapacity(n)).w == 0);
            }
            CHECK(shown > 0);
        }
        CHECK(k.key(128).w == 0 && k.key(-1).w == 0);
    }
    KeyboardLayout k;
    k.update(EditorLayout::kMinWidth, 400, 8);
    k.firstOctave = 3;
    const Rect cs = k.key(37), c = k.key(36);       // C#1 overlaps the top right of C1
    CHECK(c.contains(cs.x + 1, cs.cy()));
    CHECK_EQ(k.hitTest(cs.x + 1, cs.cy()).index, 37);
}

TEST(keyboard_scrolls_by_octave_and_follows_the_note)
{
    KeyboardLayout k;
    k.update(EditorLayout::kMinWidth, 400, 8);
    k.scrollOctaves(-100);
    CHECK_EQ(k.firstOctave, 0);
    k.scrollOctaves(100);
    CHECK_EQ(k.firstOctave, KeyboardLayout::kOctaves - k.octaves);
    CHECK(k.key(127).w > 0);
    k.ensureVisible(36);
    CHECK(k.key(36).w > 0);
    CHECK_EQ(k.firstOctave, 3);                     // scrolled the least: C1 is in the first octave shown
    k.ensureVisible(40);
    CHECK_EQ(k.firstOctave, 3);                     // already visible: nothing moves
    k.update(4000, 400, 8);                         // the panel stops growing at kMaxW
    CHECK(k.panel.w == KeyboardLayout::kMaxW);
    CHECK_EQ(k.octaves, 6);
    CHECK(k.key(36).w > 0 && k.key(40).w > 0);
}

TEST(track_menu_has_a_keyboard_button_next_to_the_midi_note)
{
    const Model m = defaultModel();
    Menu menu;
    menu.open(trackTarget(m, m.pattern().tracks[0].id), 10, 10, EditorLayout::kMinWidth, EditorLayout::kMinHeight);
    int found = 0;
    for (int i = 0; i < menu.count; ++i) {
        const Rect k = menu.keys(i);
        if (menu.fields[i].key != FieldKey::TrackNote) {
            CHECK(k.w == 0);
            continue;
        }
        ++found;
        CHECK(k.right() <= menu.row(i).right());
        CHECK(menu.number(i).right() <= k.x);
        CHECK(menu.hitTest(k.cx(), k.cy()).part == MenuPart::Keys);
        const Rect n = menu.number(i);
        CHECK(menu.hitTest(n.cx(), n.cy()).part == MenuPart::Number);
    }
    CHECK_EQ(found, 1);
}

TEST(choosing_a_note_previews_it_and_the_same_note_adds_no_undo_step)
{
    Model m = defaultModel();
    Bridge b(m);
    Track& t = m.pattern().tracks[0];
    t.velocityPct = 50;
    const int other = t.note == 40 ? 41 : 40;
    Bridge::Result r = b.handle(cmd::modifyTrack(t.id, {{"note", other}}).c_str());
    CHECK(ok(r) && r.changed);
    CHECK_EQ((int)t.note, other);
    CHECK_EQ(r.auditions.size(), 1u);
    CHECK_EQ((int)r.auditions[0].note, other);
    CHECK_EQ((int)r.auditions[0].velocity, 50);
    CHECK(m.history.canUndo());

    CHECK(ok(b.handle(cmd::undo().c_str())));
    CHECK(!m.history.canUndo());
    CHECK(m.history.canRedo());
    const int same = m.pattern().tracks[0].note;
    r = b.handle(cmd::modifyTrack(t.id, {{"note", same}}).c_str());
    CHECK(ok(r) && !r.changed);
    CHECK_EQ(r.auditions.size(), 1u);               // still heard
    CHECK(!m.history.canUndo());                    // but nothing to undo
    CHECK(m.history.canRedo());                     // and the redo is kept

    r = b.handle(cmd::modifyTrack(t.id, {{"channel", 3}}).c_str());
    CHECK(ok(r) && r.changed && r.auditions.empty());   // other track edits are silent
    m.settings.audition = false;
    CHECK(b.handle(cmd::modifyTrack(t.id, {{"note", 50}}).c_str()).auditions.empty());

    // The note keyboard asks for the preview explicitly: heard with the setting off, also for the current note.
    r = b.handle(cmd::modifyTrack(t.id, {{"note", 50}}, 0, true).c_str());
    CHECK(ok(r) && !r.changed);
    CHECK_EQ(r.auditions.size(), 1u);
    CHECK_EQ((int)r.auditions[0].note, 50);
    CHECK_EQ((int)r.auditions[0].channel, 3);
    r = b.handle(cmd::modifyTrack(t.id, {{"channel", 4}}, 0, true).c_str());
    CHECK(ok(r) && r.auditions.empty());            // only a note choice plays
    r = b.handle(("{\"version\":1,\"type\":\"modifyTrack\",\"track\":" + std::to_string(t.id) +
                  ",\"props\":{\"note\":51},\"preview\":1}").c_str());
    CHECK(!ok(r) && r.auditions.empty());
    CHECK_EQ((int)t.note, 50);                      // rejected atomically
}

int main() { return testfw::runAll(); }

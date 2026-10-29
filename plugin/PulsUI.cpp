#include "DistrhoUI.hpp"
#include "PulsPlugin.hpp"
#include "Commands.hpp"
#include "EditorLayout.hpp"
#include "Help.hpp"
#include "MenuView.hpp"
#include "Paint.hpp"
#include "Selection.hpp"
#include "Serialization.hpp"
#include "TextEdit.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

using puls::gui::Hit;
using puls::gui::Item;
using puls::gui::EditorLayout;
using puls::gui::MenuKind;
using puls::gui::MenuPart;
using puls::gui::MenuTarget;
using puls::gui::Popup;
using puls::gui::Rect;
using Clock = std::chrono::steady_clock;
namespace cmd = puls::gui::cmd;
namespace col = paint::col;
using paint::rgb;

constexpr float kDragThresholdPx = 4.0f;
constexpr float kVelocityPerPx = 0.8f;
constexpr float kLengthPxPerStep = 6.0f;
constexpr float kScrollPxPerLine = 30.0f;
constexpr auto  kConfirmTimeout = std::chrono::seconds(6);
constexpr auto  kStatusTimeout = std::chrono::seconds(4);

} // namespace

// Native NanoVG editor. Reads the Model directly (main thread) and sends Bridge commands through
// UI::setState("cmd"), which the CLAP wrapper delivers synchronously to PulsPlugin::setState. Playback state
// comes from the audio thread's telemetry atomics, polled in uiIdle; the window repaints only when something
// it shows has changed, so an idle editor costs nothing and a playing one repaints at most once per step.
class PulsUI : public UI
{
public:
    PulsUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        fPlugin = static_cast<PulsPlugin*>(getPluginInstancePointer());
        loadSharedResources();
        fFont = findFont(NANOVG_DEJAVU_SANS_TTF);
        std::memset(fHeads, -1, sizeof(fHeads));
        applyScale(getScaleFactor(), true);
        keepSelection();
    }

protected:
    // ---------------------------------------------------------------------------------------------------------
    // Plugin side

    void uiIdle() override
    {
        if (fPlugin == nullptr)
            return;
        bool dirty = false;

        for (const std::string& ev : fPlugin->drainOutbox())
        {
            dirty = true;
            if (ev.find("\"error\"") == std::string::npos)
                continue;
            const nlohmann::json j = nlohmann::json::parse(ev, nullptr, false);
            if (j.is_object() && j.value("type", "") == "error")
                showStatus(j.value("message", "error"));
        }
        if (dirty)
        {
            keepSelection();
            fMenu.sync(model());
        }

        flushVelocity();
        fMenu.idle(model());

        const bool playing = fPlugin->telPlaying.load(std::memory_order_relaxed);
        const double bpm = fPlugin->telBpm.load(std::memory_order_relaxed);
        const int pattern = fPlugin->telPlayingPattern.load(std::memory_order_relaxed);
        int8_t heads[puls::kMaxTracks];
        for (int i = 0; i < puls::kMaxTracks; ++i)
            heads[i] = fPlugin->telPlayhead[i].load(std::memory_order_relaxed);
        if (playing != fPlaying || bpm != fBpm || pattern != fPlayingPattern || std::memcmp(heads, fHeads, sizeof(heads)) != 0)
        {
            fPlaying = playing;
            fBpm = bpm;
            fPlayingPattern = pattern;
            std::memcpy(fHeads, heads, sizeof(heads));
            dirty = true;
        }

        const Clock::time_point now = Clock::now();
        if (fConfirmTrack != 0 && now >= fConfirmUntil)
        {
            fConfirmTrack = 0;
            dirty = true;
        }
        if (!fStatus.empty() && now >= fStatusUntil)
        {
            fStatus.clear();
            dirty = true;
        }

        if (dirty)
            repaint();
    }

    void stateChanged(const char*, const char*) override
    {
        // Nothing to mirror: the Model is read directly, and a host restore queues an `init` event that marks the view dirty.
    }

    void uiScaleFactorChanged(double scaleFactor) override
    {
        applyScale(scaleFactor, false);
        repaint();
    }

    // ---------------------------------------------------------------------------------------------------------
    // Drawing

    void onNanoDisplay() override
    {
        if (fPlugin == nullptr)
            return;
        relayout();
        fMenu.sync(model());
        syncSelection();
        scale(fScale, fScale);
        fontFaceId(fFont);

        beginPath();
        rect(0, 0, fL.width, fL.height);
        fillColor(rgb(col::bg));
        fill();

        drawGrid();
        drawToolbar();
        drawBank();
        drawConfirm();
        drawStatus();
        drawPopup();
        fMenu.draw(*this, model());
        drawHelp();
    }

    // ---------------------------------------------------------------------------------------------------------
    // Input

    bool onMouse(const MouseEvent& ev) override
    {
        if (fPlugin == nullptr)
            return false;
        relayout();
        const float x = ev.pos.getX() / fScale, y = ev.pos.getY() / fScale;

        if (!ev.press)
        {
            if (ev.button == kMouseButtonLeft)
            {
                if (fMenu.dragging())
                    fMenu.mouseUp(model());
                else
                    endDrag();
            }
            repaint();
            return true;
        }
        if (fDrag != Drag::None || fMenu.dragging())
            return true;
        if (fHelpOpen)
        {
            // The help is modal: a click outside the panel or on × closes it, anything else is ignored.
            if (fHelp.hitTest(x, y) != puls::gui::HelpHit::Panel)
                fHelpOpen = false;
            repaint();
            return true;
        }

        // Right-click, or Ctrl+click (the macOS context click), opens a menu for what is under the pointer.
        const bool context = ev.button == kMouseButtonRight || (ev.button == kMouseButtonLeft && (ev.mod & kModifierControl));
        if (fLenEditTrack != 0)
        {
            const int idx = indexOf(fLenEditTrack);
            if (context || idx < 0 || !fL.length(idx).contains(x, y))
                commitLengthEdit();
        }
        if (fMenu.isOpen())
        {
            if (fMenu.mouseDown(x, y, model()) || !context)
            {
                repaint();   // handled by the menu, or a plain click outside it, which only closes it
                return true;
            }
        }
        if (context)
        {
            fPopup = Popup::None;
            openContextMenu(fL.hitTest(x, y, Popup::None), x, y);
            repaint();
            return true;
        }
        if (ev.button != kMouseButtonLeft)
            return true;

        const Hit hit = fL.hitTest(x, y, fPopup);
        if (fPopup != Popup::None)
        {
            handlePopupClick(hit);
            repaint();
            return true;
        }
        handleClick(hit, x, y, ev.mod);
        repaint();
        return true;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (fPlugin == nullptr)
            return false;
        relayout();
        const float x = ev.pos.getX() / fScale, y = ev.pos.getY() / fScale;

        if (fHelpOpen)
        {
            const bool hot = fHelp.hitTest(x, y) == puls::gui::HelpHit::Close;
            if (hot != fHelpCloseHot)
            {
                fHelpCloseHot = hot;
                repaint();
            }
            return true;
        }
        if (fMenu.dragging())
        {
            fMenu.motion(x, y);
            repaint();
            return true;
        }
        if (fDrag == Drag::None && fMenu.contains(x, y))
        {
            if (fMenu.hover(x, y))
            {
                const MenuPart p = fMenu.hoveredPart();
                setCursor(p == MenuPart::Slider ? kMouseCursorLeftRight
                          : (p == MenuPart::Text || p == MenuPart::Number) ? kMouseCursorCaret : kMouseCursorArrow);
                fHover = {};
                repaint();
            }
            return true;
        }
        if (fMenu.hover(x, y))
            repaint();

        switch (fDrag)
        {
        case Drag::Cell: dragCell(y); break;
        case Drag::Swing: fSwingValue = swingAt(x); repaint(); break;
        case Drag::Length: dragLength(y); break;
        case Drag::Thumb:
            fL.scrollX = fThumbStartScroll;
            fL.scrollBy((x - fDragStartX) * fL.scrollPerThumbPx(), 0);
            repaint();
            break;
        case Drag::None: hover(x, y); break;
        }
        return true;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        if (fPlugin == nullptr)
            return false;
        relayout();
        const float x = ev.pos.getX() / fScale, y = ev.pos.getY() / fScale;
        if (fHelpOpen)
        {
            fHelp.scrollBy(-static_cast<float>(ev.delta.getY()) * kScrollPxPerLine);
            repaint();
            return true;
        }
        if (fMenu.isOpen() && fMenu.scroll(x, y, static_cast<float>(ev.delta.getY()), model()))
        {
            repaint();
            return true;
        }
        const Hit hit = fL.hitTest(x, y, fPopup);

        if (hit.item == Item::Length)
        {
            if (const puls::Track* t = trackAt(hit.index))
            {
                const int v = std::clamp(t->length + (ev.delta.getY() > 0 ? 1 : ev.delta.getY() < 0 ? -1 : 0), 1, puls::kMaxSteps);
                if (v != t->length)
                    send(cmd::modifyTrack(t->id, {{"length", v}}));
            }
            return true;
        }
        if (!fL.grid.contains(x, y) && !fL.hscroll.contains(x, y))
            return false;

        float dx = static_cast<float>(ev.delta.getX()), dy = static_cast<float>(ev.delta.getY());
        if (ev.mod & kModifierShift)
            std::swap(dx, dy);
        fL.scrollBy(dx * kScrollPxPerLine, -dy * kScrollPxPerLine);
        repaint();
        return true;
    }

    bool onKeyboard(const KeyboardEvent& ev) override
    {
        if (!ev.press || fPlugin == nullptr)
            return false;
        if (fHelpOpen)
        {
            if (ev.key == kKeyEscape)
                fHelpOpen = false;
            else if (ev.key == kKeyUp || ev.key == kKeyDown)
                fHelp.scrollBy(ev.key == kKeyDown ? kScrollPxPerLine : -kScrollPxPerLine);
            else if (ev.key == kKeyPageUp || ev.key == kKeyPageDown)
                fHelp.scrollBy((ev.key == kKeyPageDown ? 1.0f : -1.0f) * fHelp.body.h);
            repaint();
            return true;   // grid and global shortcuts are off while the help is open
        }
        if (fLenEditTrack != 0)
        {
            if (ev.key == kKeyEnter || ev.key == kKeyPadEnter)
                commitLengthEdit();
            else if (ev.key == kKeyEscape)
                fLenEditTrack = 0;
            else
                applyEditKey(fLenEdit, ev.key);
            repaint();
            return true;   // every key belongs to the field while it has focus
        }
        if (fMenu.key(ev.key, model()))
        {
            repaint();
            return true;
        }
        if (ev.key == kKeyEscape)
        {
            fPopup = Popup::None;
            fConfirmTrack = 0;
            fSel.collapse();
            repaint();
            return true;
        }
        const bool mod = ev.mod & (kModifierControl | kModifierSuper);
        if (mod && !(ev.mod & kModifierAlt))
        {
            if (ev.key == 'z' || ev.key == 'Z')
                send((ev.mod & kModifierShift) ? cmd::redo() : cmd::undo());
            else if (ev.key == 'y' || ev.key == 'Y')
                send(cmd::redo());
            else
                return fPopup == Popup::None && selectionShortcut(ev.key);
            return true;
        }
        if (fPopup == Popup::None && !(ev.mod & kModifierAlt) && selectionKey(ev.key, (ev.mod & kModifierShift) != 0))
        {
            repaint();
            return true;
        }
        return false;
    }

    bool onCharacterInput(const CharacterInputEvent& ev) override
    {
        if (fPlugin == nullptr)
            return false;
        if (fLenEditTrack != 0)
            fLenEdit.insert(ev.string);
        else if (!fMenu.editing() && std::strcmp(ev.string, "?") == 0)
            toggleHelp();
        else if (!fMenu.character(ev.string))
            return false;
        repaint();
        return true;
    }

private:
    enum class Drag : uint8_t { None, Cell, Swing, Length, Thumb };

    // ---------------------------------------------------------------------------------------------------------
    // Step selection and clipboard (keys arrive only while the plugin window has keyboard focus)

    // The selection belongs to the pattern on screen: switching patterns drops it, removing tracks clamps it.
    void syncSelection()
    {
        if (model().selected != fSelPattern)
        {
            fSelPattern = model().selected;
            fSel.clear();
        }
        fSel.clamp(pattern().trackCount);
    }

    void cursorMoved()
    {
        const puls::gui::GridPos c = fSel.cursor();
        select(c.track);
        fL.ensureVisible(c.track, c.step);
    }

    // Arrows, Space/Enter, Delete/Backspace. True when handled.
    bool selectionKey(uint key, bool shift)
    {
        const puls::Pattern& p = pattern();
        switch (key)
        {
        case kKeyLeft: case kKeyRight: case kKeyUp: case kKeyDown:
        {
            const int dt = key == kKeyDown ? 1 : key == kKeyUp ? -1 : 0;
            const int ds = key == kKeyRight ? 1 : key == kKeyLeft ? -1 : 0;
            fSel.move(dt, ds, shift, p.trackCount, std::max(0, indexOf(fSelected)));
            if (fSel.active())
                cursorMoved();
            return true;
        }
        case kKeySpace:
        case kKeyEnter:
        case kKeyPadEnter:
            if (fSel.active())
                sendIf(puls::gui::toggleCommand(p, fSel.rect()));
            return fSel.active();
        case kKeyDelete:
        case kKeyBackspace:
            if (fSel.active())
                sendIf(puls::gui::clearCommand(p, fSel.rect()));
            return fSel.active();
        default:
            return false;
        }
    }

    // Ctrl/Cmd + C, X, V. True when handled.
    bool selectionShortcut(uint key)
    {
        const puls::Pattern& p = pattern();
        switch (key)
        {
        case 'c': case 'C':
        case 'x': case 'X':
            if (!fSel.active())
                return false;
            fClip = puls::gui::copyBlock(p, fSel.rect());
            if (key == 'x' || key == 'X')
                sendIf(puls::gui::clearCommand(p, fSel.rect()));
            return true;
        case 'v': case 'V':
        {
            if (!fSel.active() || fClip.empty())
                return false;
            const puls::gui::GridRect r = fSel.rect();
            puls::gui::GridRect pasted;
            if (sendIf(puls::gui::pasteCommand(p, fClip, {r.t0, r.s0}, pasted)))
            {
                fSel.select({pasted.t0, pasted.s0}, {pasted.t1, pasted.s1});   // the pasted block becomes the selection
                cursorMoved();
            }
            return true;
        }
        default:
            return false;
        }
    }

    bool sendIf(const std::string& command)
    {
        if (command.empty())
            return false;
        send(command);
        return true;
    }

    // ---------------------------------------------------------------------------------------------------------
    // Menus and the track length field

    void openContextMenu(const Hit& hit, float x, float y)
    {
        MenuTarget t;
        switch (hit.item)
        {
        case Item::Cell:
            if (const puls::Track* tr = trackAt(hit.index))
                t = {MenuKind::Step, tr->id, hit.step, model().selected};
            break;
        case Item::Label:
        case Item::Mute:
        case Item::Solo:
        case Item::Length:
            if (const puls::Track* tr = trackAt(hit.index))
                t = {MenuKind::Track, tr->id, 0, model().selected};
            break;
        case Item::Slot:
            t = {MenuKind::Pattern, 0, 0, hit.index};
            break;
        default:
            return;
        }
        if (t.kind != MenuKind::Pattern)
            select(hit.index);
        fHover = {};
        setCursor(kMouseCursorArrow);
        fMenu.open(t, x, y, fL.width, fL.height);
    }

    void beginLengthEdit(uint32_t trackId)
    {
        if (const puls::Track* t = trackAt(indexOf(trackId)))
        {
            fMenu.close(model());
            fLenEdit.begin(std::to_string(t->length), 2, true);
            fLenEditTrack = trackId;
        }
    }

    void commitLengthEdit()
    {
        const uint32_t id = fLenEditTrack;
        fLenEditTrack = 0;
        const puls::Track* t = trackAt(indexOf(id));
        const std::string& s = fLenEdit.text();
        if (t == nullptr || s.empty() || s == "-")
            return;
        const int v = std::clamp(static_cast<int>(std::strtol(s.c_str(), nullptr, 10)), 1, puls::kMaxSteps);
        if (v != t->length)
            send(cmd::modifyTrack(id, {{"length", v}}, ++fGestureSeq));
    }

    // ---------------------------------------------------------------------------------------------------------
    // Model access (main thread)

    const puls::Model& model() const { return fPlugin->model(); }
    const puls::Pattern& pattern() const { return model().pattern(); }

    const puls::Track* trackAt(int index) const
    {
        const puls::Pattern& p = pattern();
        return index >= 0 && index < p.trackCount ? &p.tracks[index] : nullptr;
    }

    int indexOf(uint32_t id) const
    {
        const puls::Pattern& p = pattern();
        for (int i = 0; i < p.trackCount; ++i)
            if (p.tracks[i].id == id)
                return i;
        return -1;
    }

    const puls::Track* selectedTrack() const { return trackAt(indexOf(fSelected)); }

    void keepSelection()
    {
        if (fPlugin == nullptr)
            return;
        if (indexOf(fSelected) < 0)
            fSelected = pattern().trackCount > 0 ? pattern().tracks[0].id : 0;
        if (fConfirmTrack != 0 && fConfirmTrack != fSelected)
            fConfirmTrack = 0;
        syncSelection();
    }

    void send(const std::string& json)
    {
        setState("cmd", json.c_str());
        keepSelection();
        repaint();
    }

    void showStatus(const std::string& message)
    {
        fStatus = message;
        fStatusUntil = Clock::now() + kStatusTimeout;
        send(cmd::hello());   // the view may have diverged: resync (also clears stale UI-side state)
    }

    // ---------------------------------------------------------------------------------------------------------
    // Geometry

    void applyScale(double scaleFactor, bool resize)
    {
        fScale = scaleFactor > 0.0 ? static_cast<float>(scaleFactor) : 1.0f;
        if (resize && d_isNotEqual(fScale, 1.0f))
            setSize(DISTRHO_UI_DEFAULT_WIDTH * fScale, DISTRHO_UI_DEFAULT_HEIGHT * fScale);
        setGeometryConstraints(EditorLayout::kMinWidth * fScale, EditorLayout::kMinHeight * fScale, false, false);
    }

    void relayout()
    {
        fL.update(getWidth() / fScale, getHeight() / fScale, pattern().trackCount, puls::kMaxSteps, fConfirmTrack != 0);
        fHelp.update(fL.width, fL.height);
    }

    void toggleHelp()
    {
        fHelpOpen = !fHelpOpen;
        if (!fHelpOpen)
            return;
        // The help is modal: finish whatever was being edited first.
        if (fLenEditTrack != 0)
            commitLengthEdit();
        fMenu.close(model());
        fPopup = Popup::None;
        fHover = {};
        fHelpCloseHot = false;
        setCursor(kMouseCursorArrow);
    }

    // ---------------------------------------------------------------------------------------------------------
    // Clicks and drags

    void handlePopupClick(const Hit& hit)
    {
        switch (hit.item)
        {
        case Item::ResolutionOption:
            if (const puls::Track* t = selectedTrack())
                send(cmd::modifyTrack(t->id, {{"resolution", puls::resolutionName(static_cast<puls::Resolution>(hit.index))}}));
            fPopup = Popup::None;
            break;
        case Item::AnchorOption:
        {
            puls::Settings s = model().settings;
            s.anchorMode = hit.index == 0 ? puls::AnchorMode::RestartOnStart : puls::AnchorMode::HostAbsolute;
            send(cmd::setSettings(s));
            break;
        }
        case Item::SwitchOption:
        {
            static constexpr puls::SwitchQuantize kOrder[] = {puls::SwitchQuantize::Bar, puls::SwitchQuantize::Beat, puls::SwitchQuantize::Now};
            puls::Settings s = model().settings;
            s.patternSwitch = kOrder[hit.index];
            send(cmd::setSettings(s));
            break;
        }
        case Item::Popup:
            break;
        default:
            fPopup = Popup::None;   // a click outside closes the popup and does nothing else
            break;
        }
    }

    void handleClick(const Hit& hit, float x, float y, uint mods)
    {
        const puls::Pattern& p = pattern();
        switch (hit.item)
        {
        case Item::Undo:
            if (model().history.canUndo()) send(cmd::undo());
            break;
        case Item::Redo:
            if (model().history.canRedo()) send(cmd::redo());
            break;
        case Item::Enable:
        {
            puls::Settings s = model().settings;
            s.enabled = !s.enabled;
            send(cmd::setSettings(s));
            break;
        }
        case Item::Resolution:
            if (selectedTrack() != nullptr) fPopup = Popup::Resolution;
            break;
        case Item::Settings:
            fPopup = Popup::Settings;
            break;
        case Item::Help:
            toggleHelp();
            break;
        case Item::Swing:
            if (const puls::Track* t = selectedTrack())
            {
                fDrag = Drag::Swing;
                fDragTrack = t->id;
                fGesture = ++fGestureSeq;
                fSwingValue = swingAt(x);
            }
            break;
        case Item::RemoveTrack:
            if (p.trackCount > 1 && selectedTrack() != nullptr)
            {
                fConfirmTrack = fSelected;   // removal asks first; the prompt expires on its own
                fConfirmUntil = Clock::now() + kConfirmTimeout;
            }
            break;
        case Item::AddTrack:
            if (p.trackCount < puls::kMaxTracks) send(cmd::addTrack());
            break;
        case Item::ConfirmYes:
            if (fConfirmTrack != 0) send(cmd::removeTrack(fConfirmTrack));
            fConfirmTrack = 0;
            break;
        case Item::ConfirmNo:
            fConfirmTrack = 0;
            break;
        case Item::Slot:
            send(cmd::selectPattern(hit.index, (mods & kModifierAlt) != 0));
            break;
        case Item::Label:
            select(hit.index);
            break;
        case Item::Mute:
        case Item::Solo:
            if (const puls::Track* t = trackAt(hit.index))
            {
                const bool mute = hit.item == Item::Mute;
                send(cmd::modifyTrack(t->id, {{mute ? "mute" : "solo", !(mute ? t->mute : t->solo)}}));
            }
            break;
        case Item::Length:
            // Drag up/down to change the length; a click without dragging types it instead.
            if (const puls::Track* t = trackAt(hit.index); t != nullptr && t->id != fLenEditTrack)
            {
                fDrag = Drag::Length;
                fDragTrack = t->id;
                fDragStartY = y;
                fDragStartValue = t->length;
                fLengthMoved = false;
                fGesture = ++fGestureSeq;
            }
            break;
        case Item::Cell:
            if (const puls::Track* t = trackAt(hit.index))
            {
                select(hit.index);
                if ((mods & kModifierShift) && fSel.active())
                {
                    fSel.extendTo({hit.index, hit.step});   // Shift+click extends the selection instead of toggling
                    break;
                }
                fSel.set({hit.index, hit.step});
                const puls::Step& st = t->steps[hit.step];
                fDrag = Drag::Cell;
                fDragTrack = t->id;
                fDragStep = hit.step;
                fDragStartY = y;
                fDragStartValue = st.velocity;
                fDragWasActive = st.active;
                fDragging = false;
                fDragVelocity = st.velocity;
                fVelocityPending = false;
                fGesture = ++fGestureSeq;
            }
            break;
        case Item::HScrollbar:
        {
            const Rect th = fL.hthumb();
            if (th.contains(x, y) || (x >= th.x && x < th.right()))
            {
                fDrag = Drag::Thumb;
                fDragStartX = x;
                fThumbStartScroll = fL.scrollX;
            }
            else
            {
                fL.scrollBy(x < th.x ? -fL.cells.w : fL.cells.w, 0);
            }
            break;
        }
        default:
            break;
        }
    }

    void select(int index)
    {
        if (const puls::Track* t = trackAt(index))
        {
            fSelected = t->id;
            keepSelection();
        }
    }

    // Vertical drag on an active step sets its velocity (up = louder). Coalesced: at most one command per idle tick.
    void dragCell(float y)
    {
        const float dy = fDragStartY - y;
        if (!fDragging)
        {
            if (!fDragWasActive || std::fabs(dy) <= kDragThresholdPx)
                return;
            fDragging = true;
            setCursor(kMouseCursorUpDown);
        }
        const int v = std::clamp(static_cast<int>(std::lround(fDragStartValue + dy * kVelocityPerPx)), 1, 127);
        if (v == fDragVelocity)
            return;
        fDragVelocity = v;
        fVelocityPending = true;
        repaint();
    }

    void flushVelocity()
    {
        if (!fVelocityPending)
            return;
        fVelocityPending = false;
        if (indexOf(fDragTrack) >= 0)
            send(cmd::setStepVelocity(fDragTrack, fDragStep, fDragVelocity, fGesture));
    }

    void dragLength(float y)
    {
        const int idx = indexOf(fDragTrack);
        const puls::Track* t = trackAt(idx);
        if (t == nullptr)
            return;
        const int v = std::clamp(fDragStartValue + static_cast<int>(std::lround((fDragStartY - y) / kLengthPxPerStep)), 1, puls::kMaxSteps);
        if (v != t->length)
        {
            fLengthMoved = true;
            send(cmd::modifyTrack(t->id, {{"length", v}}, fGesture));
        }
    }

    int swingAt(float x) const
    {
        return std::clamp(static_cast<int>(std::lround((x - fL.swing.x) / fL.swing.w * 75.0f)), 0, 75);
    }

    void endDrag()
    {
        const Drag d = fDrag;
        fDrag = Drag::None;
        switch (d)
        {
        case Drag::Cell:
            flushVelocity();
            if (!fDragging && indexOf(fDragTrack) >= 0)
                send(cmd::toggleStep(fDragTrack, fDragStep));
            fDragging = false;
            break;
        case Drag::Swing:
            if (const puls::Track* t = trackAt(indexOf(fDragTrack)); t != nullptr && t->swing != fSwingValue)
                send(cmd::modifyTrack(t->id, {{"swing", fSwingValue}}, fGesture));
            break;
        case Drag::Length:
            if (!fLengthMoved)
                beginLengthEdit(fDragTrack);
            break;
        default:
            break;
        }
        fHover = {};
        setCursor(kMouseCursorArrow);
        repaint();
    }

    void hover(float x, float y)
    {
        const Hit hit = fL.hitTest(x, y, fPopup);
        if (hit.item == fHover.item && hit.index == fHover.index && hit.step == fHover.step)
            return;
        fHover = hit;
        MouseCursor c = kMouseCursorArrow;
        if (hit.item == Item::Length)
            c = kMouseCursorUpDown;
        else if (hit.item == Item::Swing)
            c = kMouseCursorLeftRight;
        else if (hit.item == Item::Cell)
            if (const puls::Track* t = trackAt(hit.index); t != nullptr && t->steps[hit.step].active)
                c = kMouseCursorUpDown;
        setCursor(c);
        repaint();
    }

    bool hovered(Item item, int index = -1) const { return fHover.item == item && fHover.index == index; }

    // ---------------------------------------------------------------------------------------------------------
    // Drawing helpers

    void fillRect(const Rect& r, uint32_t c, float a = 1.0f, float radius = 0.0f) { paint::fillRect(*this, r, c, a, radius); }
    void strokeRect(const Rect& r, uint32_t c, float width, float radius, float a = 1.0f) { paint::strokeRect(*this, r, c, width, radius, a); }
    void label(const Rect& r, const char* s, uint32_t c, int align = ALIGN_LEFT, float size = 12.0f, float a = 1.0f) { paint::label(*this, r, s, c, align, size, a); }
    void clippedLabel(const Rect& r, const char* s, uint32_t c, float size = 12.0f) { paint::clippedLabel(*this, r, s, c, size); }
    void button(const Rect& r, const char* s, bool enabled, bool active, bool hot, float size = 12.0f) { paint::button(*this, r, s, enabled, active, hot, size); }

    void hline(float x0, float x1, float y, uint32_t c)
    {
        fillRect({x0, y, x1 - x0, 1}, c);
    }

    // ---------------------------------------------------------------------------------------------------------
    // Sections

    void drawToolbar()
    {
        const puls::Model& m = model();
        const puls::Pattern& p = m.pattern();
        const puls::Track* t = selectedTrack();
        fillRect(fL.toolbar, col::panel);
        hline(0, fL.width, fL.toolbar.bottom() - 1, col::line);

        button(fL.undo, "\xE2\x86\xB6", m.history.canUndo(), false, hovered(Item::Undo), 15.0f);   // ↶
        button(fL.redo, "\xE2\x86\xB7", m.history.canRedo(), false, hovered(Item::Redo), 15.0f);   // ↷
        button(fL.enable, "On", true, m.settings.enabled, hovered(Item::Enable));
        clippedLabel(fL.patternName, p.name, col::text2);
        clippedLabel(fL.trackName, t ? t->name : "", col::text2);

        label(fL.resLabel, "Res", col::text2);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s \xE2\x96\xBE", t ? puls::resolutionName(t->resolution) : "-");   // ▾
        button(fL.resolution, buf, t != nullptr, fPopup == Popup::Resolution, hovered(Item::Resolution));

        label(fL.swingLabel, "Swing", col::text2);
        const int swing = (fDrag == Drag::Swing) ? fSwingValue : (t ? t->swing : 0);
        const Rect& sw = fL.swing;
        fillRect({sw.x, sw.cy() - 2, sw.w, 4}, col::cell, 1.0f, 2.0f);
        fillRect({sw.x, sw.cy() - 2, sw.w * swing / 75.0f, 4}, col::accent, 1.0f, 2.0f);
        beginPath();
        circle(sw.x + sw.w * swing / 75.0f, sw.cy(), 6);
        fillColor(rgb(hovered(Item::Swing) || fDrag == Drag::Swing ? col::white : col::text));
        fill();
        std::snprintf(buf, sizeof(buf), "%d", swing);
        label(fL.swingValue, buf, col::text, ALIGN_RIGHT);

        std::snprintf(buf, sizeof(buf), "Tracks %d", p.trackCount);
        label(fL.trackCountText, buf, col::text2);
        button(fL.removeTrack, "\xE2\x88\x92", p.trackCount > 1, false, hovered(Item::RemoveTrack), 14.0f);   // −
        button(fL.addTrack, "+", p.trackCount < puls::kMaxTracks, false, hovered(Item::AddTrack), 14.0f);
        button(fL.settings, "\xE2\x9A\x99", true, fPopup == Popup::Settings, hovered(Item::Settings), 14.0f);   // ⚙
        button(fL.help, "?", true, fHelpOpen, hovered(Item::Help), 13.0f);
    }

    void drawBank()
    {
        const puls::Model& m = model();
        fillRect(fL.bank, col::panel);
        hline(0, fL.width, fL.bank.bottom() - 1, col::line);
        char buf[32];
        for (int i = 0; i < puls::kBankSize; ++i)
        {
            const Rect r = fL.slot(i);
            const bool sel = i == m.selected;
            fillRect(r, sel ? col::accent : col::cell, 1.0f, 4.0f);
            strokeRect(r, sel ? col::accent : hovered(Item::Slot, i) ? col::text2 : col::line, 1.0f, 4.0f);
            std::snprintf(buf, sizeof(buf), "%d", i + 1);
            label(r, buf, sel ? col::bg : !puls::isBlank(m.bank[i]) ? col::text : col::text2, ALIGN_CENTER);
            if (i == fPlayingPattern)
            {
                beginPath();
                circle(r.right() - 4.5f, r.y + 4.5f, 2.5f);
                fillColor(rgb(col::playing));
                fill();
            }
        }
        clippedLabel(fL.bankName, m.pattern().name, col::text2);
        std::snprintf(buf, sizeof(buf), "%s  %.1f bpm", fPlaying ? "playing" : "stopped", fBpm);
        label(fL.transport, buf, col::text2, ALIGN_RIGHT);
    }

    void drawGrid()
    {
        const puls::Pattern& p = pattern();
        const bool showHeads = fPlayingPattern == model().selected;
        const int s0 = std::max(0, static_cast<int>(fL.scrollX / fL.cellW));
        const int s1 = std::min(puls::kMaxSteps - 1, static_cast<int>((fL.scrollX + fL.cells.w) / fL.cellW));
        const puls::gui::GridRect selRect = fSel.rect();
        char buf[8];

        save();
        scissor(fL.grid.x, fL.grid.y, fL.grid.w, fL.grid.h);
        for (int ti = 0; ti < p.trackCount; ++ti)
        {
            const Rect row = fL.row(ti);
            if (row.bottom() < fL.grid.y || row.y > fL.grid.bottom())
                continue;
            const puls::Track& t = p.tracks[ti];
            save();
            if (t.mute) globalAlpha(0.45f);

            // Cells, clipped to the scrolling area right of the labels.
            save();
            intersectScissor(fL.cells.x, fL.cells.y, fL.cells.w, fL.cells.h);
            const int head = showHeads ? fHeads[ti] : -1;
            for (int s = s0; s <= s1; ++s)
            {
                const Rect c = fL.cell(ti, s);
                const Rect in{c.x + 1, c.y + 1, c.w - 2, c.h - 2};
                const puls::Step& st = t.steps[s];
                const bool out = s >= t.length;
                fillRect(in, out ? col::cellOut : (s % 4 == 0) ? col::cellBeat : col::cell, out ? 0.55f : 1.0f, 3.0f);
                if (st.active)
                {
                    const bool live = fDrag == Drag::Cell && fDragging && fDragTrack == t.id && fDragStep == s;
                    const int vel = live ? fDragVelocity : st.velocity;
                    // Velocity is the track color's alpha (hue kept).
                    fillRect(in, t.color, (0.25f + 0.75f * vel / 127.0f) * (out ? 0.55f : 1.0f), 3.0f);
                    if (st.ratchets > 1)
                    {
                        std::snprintf(buf, sizeof(buf), "%d", st.ratchets);
                        label({in.x, in.bottom() - 9, in.w - 3, 8}, buf, col::white, ALIGN_RIGHT, 9.0f, 0.85f);
                    }
                    if (st.probability < 100)
                        for (float dx = 3; dx + 3 <= in.w - 3; dx += 6)
                            fillRect({in.x + dx, in.y + 2, 3, 2}, col::white, 0.8f);
                    if (st.microtiming < 0)
                        fillRect({in.x, in.y, 3, in.h}, col::white, 0.7f);
                    else if (st.microtiming > 0)
                        fillRect({in.right() - 3, in.y, 3, in.h}, col::white, 0.7f);
                }
                if (fSel.active() && selRect.contains(ti, s))
                {
                    const bool cur = fSel.cursor().track == ti && fSel.cursor().step == s;
                    if (cur || fSel.multi())
                        strokeRect(c, col::accent, cur ? 1.5f : 1.0f, 3.0f, cur ? 1.0f : 0.6f);
                }
                if (s == head)
                    strokeRect(in, col::white, 2.0f, 3.0f);
            }
            restore();

            // Label column (does not scroll horizontally).
            const bool sel = t.id == fSelected;
            const Rect lab = fL.label(ti);
            fillRect(lab, col::panel);
            fillRect({lab.right() - 1, lab.y, 1, lab.h}, col::line);
            if (sel)
                fillRect({lab.x, lab.y, 3, lab.h}, col::accent);
            const Rect sw = fL.swatch(ti);
            beginPath();
            circle(sw.cx(), sw.cy(), 4);
            fillColor(rgb(t.color));
            fill();
            clippedLabel(fL.name(ti), t.name, sel ? col::white : col::text);
            smallToggle(fL.mute(ti), "M", t.mute, hovered(Item::Mute, ti));
            smallToggle(fL.solo(ti), "S", t.solo, hovered(Item::Solo, ti));
            std::snprintf(buf, sizeof(buf), "%d", t.length);
            paint::textField(*this, fL.length(ti), buf, fLenEditTrack == t.id ? &fLenEdit : nullptr,
                             hovered(Item::Length, ti) || (fDrag == Drag::Length && fDragTrack == t.id), ALIGN_RIGHT);
            restore();
        }
        restore();

        if (fL.hscroll.w > 0)
        {
            fillRect(fL.hscroll, col::panel);
            fillRect(fL.hthumb(), fDrag == Drag::Thumb || hovered(Item::HScrollbar) ? col::text2 : col::line, 1.0f, 3.0f);
        }
    }

    void smallToggle(const Rect& r, const char* s, bool on, bool hot)
    {
        fillRect(r, on ? col::muteOn : col::cell, 1.0f, 4.0f);
        strokeRect(r, on ? col::muteOn : hot ? col::text2 : col::line, 1.0f, 4.0f);
        label(r, s, on ? col::bg : col::text2, ALIGN_CENTER, 10.0f);
    }

    void drawConfirm()
    {
        if (fConfirmTrack == 0)
            return;
        const puls::Track* t = trackAt(indexOf(fConfirmTrack));
        if (t == nullptr)
            return;
        fillRect(fL.confirmBar, col::panel, 1.0f, 6.0f);
        strokeRect(fL.confirmBar, col::line, 1.0f, 6.0f);
        char buf[48];
        std::snprintf(buf, sizeof(buf), "Remove \"%s\"?", t->name);
        clippedLabel(fL.confirmText, buf, col::errText);
        save();
        fillRect(fL.confirmYes, col::danger, 1.0f, 4.0f);
        label(fL.confirmYes, "Remove", col::white, ALIGN_CENTER);
        restore();
        button(fL.confirmNo, "Cancel", true, false, hovered(Item::ConfirmNo));
    }

    // Modal help: dimmed window, a panel with a header and a scrolling two-column body (key, wrapped text).
    void drawHelp()
    {
        if (!fHelpOpen)
            return;
        using puls::gui::HelpLayout;
        fillRect({0, 0, fL.width, fL.height}, 0x000000, 0.6f);
        panel(fHelp.panel);
        label({fHelp.header.x + HelpLayout::kPad, fHelp.header.y, 200, fHelp.header.h}, "Puls help", col::white, ALIGN_LEFT, 14.0f);
        button(fHelp.close, "\xC3\x97", true, false, fHelpCloseHot, 15.0f);   // ×

        const Rect& body = fHelp.body;
        const float valX = body.x + HelpLayout::kKeyW + 12, valW = body.right() - valX;
        float y = body.y - fHelp.scroll;
        save();
        scissor(body.x, body.y, body.w, body.h);
        fontSize(12.0f);
        textLineHeight(1.2f);
        for (int i = 0; i < puls::gui::kHelpSectionCount; ++i)
        {
            const puls::gui::HelpSection& sec = puls::gui::kHelpSections[i];
            y += 10;
            label({body.x, y, body.w, 18}, sec.title, col::accent);
            y += 20;
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            for (int r = 0; r < sec.count; ++r)
            {
                float kb[4], vb[4];
                textBoxBounds(body.x, y + 5, HelpLayout::kKeyW, sec.rows[r].key, nullptr, kb);
                textBoxBounds(valX, y + 5, valW, sec.rows[r].text, nullptr, vb);
                const float h = std::max({kb[3] - kb[1], vb[3] - vb[1], 15.0f}) + 10;
                if (y + h >= body.y && y <= body.bottom())   // only rows in view are drawn
                {
                    fillRect({body.x, y, body.w, 1}, col::line);
                    fillColor(rgb(col::white));
                    textBox(body.x, y + 5, HelpLayout::kKeyW, sec.rows[r].key);
                    fillColor(rgb(col::text));
                    textBox(valX, y + 5, valW, sec.rows[r].text);
                }
                y += h;
            }
        }
        restore();
        fHelp.setContentHeight(y + fHelp.scroll - body.y + 8);

        if (fHelp.scroll < 1.0f && y > body.bottom())   // hint that there is more below
            label({fHelp.header.x, fHelp.header.y, fHelp.close.x - 10 - fHelp.header.x, fHelp.header.h},
                  "scroll for more \xE2\x86\x93", col::text2, ALIGN_RIGHT, 11.0f);   // ↓
    }

    void drawStatus()
    {
        if (fStatus.empty())
            return;
        fontSize(12.0f);
        Rectangle<float> b;
        textBounds(0, 0, fStatus.c_str(), nullptr, b);
        const Rect r{EditorLayout::kPad, fL.grid.y + (fConfirmTrack != 0 ? 42.0f : 6.0f), std::min(b.getWidth() + 20.0f, fL.width - 20.0f), 26};
        fillRect(r, col::errBg, 1.0f, 4.0f);
        clippedLabel({r.x + 10, r.y, r.w - 20, r.h}, fStatus.c_str(), col::errText);
    }

    void drawPopup()
    {
        if (fPopup == Popup::Resolution)
        {
            const puls::Track* t = selectedTrack();
            const Rect pr = fL.resolutionPopup();
            panel(pr);
            for (int i = 0; i < EditorLayout::kResolutionCount; ++i)
            {
                const Rect o = fL.resolutionOption(i);
                const bool cur = t != nullptr && static_cast<int>(t->resolution) == i;
                if (hovered(Item::ResolutionOption, i))
                    fillRect(o, col::cell, 1.0f, 3.0f);
                label({o.x + 8, o.y, o.w - 8, o.h}, puls::resolutionName(static_cast<puls::Resolution>(i)), cur ? col::accent : col::text);
            }
        }
        else if (fPopup == Popup::Settings)
        {
            const puls::Settings& s = model().settings;
            const Rect pr = fL.settingsPanel();
            panel(pr);
            label({pr.x + 10, fL.settingsTitleY(0), pr.w, 20}, "Step grid anchor", col::text2);
            static const char* const kAnchor[] = {"Restart where playback starts", "Follow song position"};
            for (int i = 0; i < 2; ++i)
                radio(fL.anchorOption(i), kAnchor[i], (i == 0) == (s.anchorMode == puls::AnchorMode::RestartOnStart), hovered(Item::AnchorOption, i));
            label({pr.x + 10, fL.settingsTitleY(1), pr.w, 20}, "Pattern switch", col::text2);
            static const char* const kSwitch[] = {"At the next bar", "At the next beat", "Immediately"};
            static constexpr puls::SwitchQuantize kOrder[] = {puls::SwitchQuantize::Bar, puls::SwitchQuantize::Beat, puls::SwitchQuantize::Now};
            for (int i = 0; i < 3; ++i)
                radio(fL.switchOption(i), kSwitch[i], s.patternSwitch == kOrder[i], hovered(Item::SwitchOption, i));
        }
    }

    void panel(const Rect& r) { paint::panel(*this, r); }

    void radio(const Rect& r, const char* s, bool on, bool hot)
    {
        if (hot)
            fillRect(r, col::cell, 1.0f, 3.0f);
        beginPath();
        circle(r.x + 12, r.cy(), 5);
        strokeColor(rgb(on ? col::accent : col::text2));
        strokeWidth(1.5f);
        stroke();
        if (on)
        {
            beginPath();
            circle(r.x + 12, r.cy(), 2.5f);
            fillColor(rgb(col::accent));
            fill();
        }
        label({r.x + 24, r.y, r.w - 24, r.h}, s, on ? col::white : col::text);
    }

    // ---------------------------------------------------------------------------------------------------------

    PulsPlugin* fPlugin = nullptr;
    FontId fFont = -1;
    float  fScale = 1.0f;
    EditorLayout fL;

    uint32_t fSelected = 0;                // track id; toolbar Res/Swing act on it
    Popup    fPopup = Popup::None;
    Hit      fHover;
    uint32_t fConfirmTrack = 0;            // track id awaiting removal confirmation, 0 = none
    Clock::time_point fConfirmUntil{};
    std::string fStatus;
    Clock::time_point fStatusUntil{};

    // Drag state.
    Drag     fDrag = Drag::None;
    uint32_t fDragTrack = 0;
    int      fDragStep = 0;
    float    fDragStartX = 0, fDragStartY = 0;
    int      fDragStartValue = 0;
    bool     fDragWasActive = false, fDragging = false;
    int      fDragVelocity = 0;
    bool     fVelocityPending = false;
    int      fSwingValue = 0;
    float    fThumbStartScroll = 0;
    uint32_t fGesture = 0, fGestureSeq = 0;   // one undo step per drag
    bool     fLengthMoved = false;

    // Typing a track length in the grid (a click on the length box without dragging).
    puls::gui::TextEdit fLenEdit;
    uint32_t fLenEditTrack = 0;               // 0 = not editing

    // Telemetry snapshot (audio thread -> UI).
    bool   fPlaying = false;
    double fBpm = 0.0;
    int    fPlayingPattern = 0;
    int8_t fHeads[puls::kMaxTracks] = {};

    // Step selection (track index x step) and the block clipboard (UI-side, full step data, not persisted).
    // Help overlay (modal).
    puls::gui::HelpLayout fHelp;
    bool fHelpOpen = false, fHelpCloseHot = false;

    puls::gui::Selection fSel;
    int                  fSelPattern = -1;
    puls::gui::StepBlock fClip;

    // Right-click popovers; shares the gesture counter so menu and grid edits never coalesce by accident.
    MenuView fMenu{[this](const std::string& c) { send(c); }, fGestureSeq};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PulsUI)
};

UI* createUI()
{
    return new PulsUI();
}

END_NAMESPACE_DISTRHO

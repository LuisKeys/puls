#pragma once
#include <cstddef>
#include <string>

namespace puls::gui {

// Single-line text editing with a byte limit, UTF-8 aware cursor movement and an optional numeric filter
// (digits and one leading '-'). Starting an edit selects everything: the first keystroke replaces the text.
class TextEdit {
public:
    void begin(const std::string& text, size_t maxBytes, bool numeric)
    {
        text_ = text.substr(0, maxBytes);
        max_ = maxBytes;
        numeric_ = numeric;
        cursor_ = text_.size();
        replace_ = true;
    }

    // Inserts typed text (one or more UTF-8 characters); control characters and, when numeric, anything but
    // digits and a leading minus are dropped. Characters that would exceed the byte limit are dropped too.
    void insert(const char* s)
    {
        if (s == nullptr) return;
        takeSelection();
        for (size_t i = 0; s[i] != '\0';) {
            const size_t n = charLen(static_cast<unsigned char>(s[i]));
            std::string ch;
            for (size_t k = 0; k < n && s[i + k] != '\0'; ++k) ch += s[i + k];
            i += ch.size();
            if (ch.size() != n) break;   // truncated sequence
            const unsigned char c0 = static_cast<unsigned char>(ch[0]);
            if (n == 1 && (c0 < 0x20 || c0 == 0x7f)) continue;
            if (numeric_) {
                const bool digit = n == 1 && c0 >= '0' && c0 <= '9';
                const bool minus = n == 1 && c0 == '-' && cursor_ == 0 && (text_.empty() || text_[0] != '-');
                if (!digit && !minus) continue;
            }
            if (text_.size() + n > max_) break;
            text_.insert(cursor_, ch);
            cursor_ += n;
        }
    }

    void backspace()
    {
        if (takeSelection() || cursor_ == 0) return;
        const size_t p = prev(cursor_);
        text_.erase(p, cursor_ - p);
        cursor_ = p;
    }

    void del()
    {
        if (takeSelection() || cursor_ >= text_.size()) return;
        text_.erase(cursor_, next(cursor_) - cursor_);
    }

    void left()  { replace_ = false; if (cursor_ > 0) cursor_ = prev(cursor_); }
    void right() { replace_ = false; if (cursor_ < text_.size()) cursor_ = next(cursor_); }
    void home()  { replace_ = false; cursor_ = 0; }
    void end()   { replace_ = false; cursor_ = text_.size(); }

    const std::string& text() const noexcept { return text_; }
    size_t cursor() const noexcept { return cursor_; }
    bool   allSelected() const noexcept { return replace_; }

private:
    static size_t charLen(unsigned char c) { return c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1; }
    static bool   isCont(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }
    size_t prev(size_t p) const { do --p; while (p > 0 && isCont(text_[p])); return p; }
    size_t next(size_t p) const { do ++p; while (p < text_.size() && isCont(text_[p])); return p; }

    // The selection is "everything" or nothing; typing or deleting over it clears the text first.
    bool takeSelection()
    {
        if (!replace_) return false;
        replace_ = false;
        text_.clear();
        cursor_ = 0;
        return true;
    }

    std::string text_;
    size_t max_ = 0, cursor_ = 0;
    bool numeric_ = false, replace_ = false;
};

} // namespace puls::gui

// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "json_object.h"

#include <cstdint>

namespace bm3d {

namespace {

// Thrown inside the checker to stop at the first problem; never leaves jsonObjectError.
struct Problem {
    std::string message;
    size_t offset;
};

class Checker {
public:
    explicit Checker(std::string_view text) : text_(text) {}

    void checkDocument() {
        if (text_.substr(0, 3) == "\xEF\xBB\xBF")
            pos_ = 3;
        skipWhiteSpace();
        if (atEnd())
            fail("the text is empty");
        if (peek() != '{')
            fail("the top level value is not an object");
        value(0);
        skipWhiteSpace();
        if (!atEnd())
            fail("unexpected text after the object");
    }

private:
    [[noreturn]] void fail(const std::string& message) const { throw Problem{message, pos_}; }

    bool atEnd() const { return pos_ >= text_.size(); }
    char peek() const { return text_[pos_]; }

    void skipWhiteSpace() {
        while (!atEnd() && (peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r'))
            ++pos_;
    }

    void expect(char c, const char* what) {
        if (atEnd() || peek() != c)
            fail(std::string("expected ") + what);
        ++pos_;
    }

    void literal(std::string_view word) {
        if (text_.substr(pos_, word.size()) != word)
            fail("invalid value");
        pos_ += word.size();
    }

    void value(int depth) {
        if (atEnd())
            fail("a value is missing");
        switch (peek()) {
            case '{': object(depth + 1); break;
            case '[': array(depth + 1); break;
            case '"': string(); break;
            case 't': literal("true"); break;
            case 'f': literal("false"); break;
            case 'n': literal("null"); break;
            default: number(); break;
        }
    }

    void nest(int depth) const {
        if (depth > kJsonMaxDepth)
            fail("objects and arrays are nested too deeply");
    }

    void object(int depth) {
        nest(depth);
        ++pos_;  // '{'
        skipWhiteSpace();
        if (!atEnd() && peek() == '}') {
            ++pos_;
            return;
        }
        for (;;) {
            skipWhiteSpace();
            if (atEnd() || peek() != '"')
                fail("expected a string as the name of a member");
            string();
            skipWhiteSpace();
            expect(':', "':' after the name of a member");
            skipWhiteSpace();
            value(depth);
            skipWhiteSpace();
            if (!atEnd() && peek() == ',') {
                ++pos_;
                continue;
            }
            expect('}', "',' or '}'");
            return;
        }
    }

    void array(int depth) {
        nest(depth);
        ++pos_;  // '['
        skipWhiteSpace();
        if (!atEnd() && peek() == ']') {
            ++pos_;
            return;
        }
        for (;;) {
            skipWhiteSpace();
            value(depth);
            skipWhiteSpace();
            if (!atEnd() && peek() == ',') {
                ++pos_;
                continue;
            }
            expect(']', "',' or ']'");
            return;
        }
    }

    static bool isDigit(char c) { return c >= '0' && c <= '9'; }

    void digits() {
        if (atEnd() || !isDigit(peek()))
            fail("invalid number");
        while (!atEnd() && isDigit(peek()))
            ++pos_;
    }

    void number() {
        if (!atEnd() && peek() == '-')
            ++pos_;
        if (atEnd())
            fail("invalid number");
        if (peek() == '0')
            ++pos_;  // no leading zeros: "01" ends the number here and fails after it
        else
            digits();
        if (!atEnd() && peek() == '.') {
            ++pos_;
            digits();
        }
        if (!atEnd() && (peek() == 'e' || peek() == 'E')) {
            ++pos_;
            if (!atEnd() && (peek() == '+' || peek() == '-'))
                ++pos_;
            digits();
        }
    }

    int hexDigit() {
        if (atEnd())
            fail("a \\u escape is cut short");
        const char c = peek();
        ++pos_;
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        --pos_;
        fail("invalid hexadecimal digit in a \\u escape");
    }

    unsigned hex4() {
        unsigned v = 0;
        for (int i = 0; i < 4; ++i)
            v = v * 16 + static_cast<unsigned>(hexDigit());
        return v;
    }

    void unicodeEscape() {
        const unsigned first = hex4();
        if (first >= 0xDC00 && first <= 0xDFFF)
            fail("a low surrogate escape without a high one before it");
        if (first >= 0xD800 && first <= 0xDBFF) {
            if (text_.substr(pos_, 2) != "\\u")
                fail("a high surrogate escape without a low one after it");
            pos_ += 2;
            const unsigned second = hex4();
            if (second < 0xDC00 || second > 0xDFFF)
                fail("a high surrogate escape without a low one after it");
        }
    }

    // One UTF-8 encoded code point at pos_ that is not ASCII; the shortest form, no surrogates,
    // nothing above U+10FFFF.
    void utf8() {
        const auto lead = static_cast<unsigned char>(peek());
        int extra;
        uint32_t code;
        if (lead >= 0xC2 && lead <= 0xDF) {
            extra = 1;
            code = lead & 0x1Fu;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            extra = 2;
            code = lead & 0x0Fu;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            extra = 3;
            code = lead & 0x07u;
        } else {
            fail("invalid UTF-8");
        }
        const size_t start = pos_;
        ++pos_;
        for (int i = 0; i < extra; ++i) {
            if (atEnd() || (static_cast<unsigned char>(peek()) & 0xC0u) != 0x80u) {
                pos_ = start;
                fail("invalid UTF-8");
            }
            code = (code << 6) | (static_cast<unsigned char>(peek()) & 0x3Fu);
            ++pos_;
        }
        const uint32_t smallest = extra == 1 ? 0x80u : extra == 2 ? 0x800u : 0x10000u;
        if (code < smallest || code > 0x10FFFFu || (code >= 0xD800u && code <= 0xDFFFu)) {
            pos_ = start;
            fail("invalid UTF-8");
        }
    }

    void string() {
        ++pos_;  // '"'
        for (;;) {
            if (atEnd())
                fail("a string is not closed");
            const auto c = static_cast<unsigned char>(peek());
            if (c == '"') {
                ++pos_;
                return;
            }
            if (c < 0x20)
                fail("a control character in a string");
            if (c == '\\') {
                ++pos_;
                if (atEnd())
                    fail("a string is not closed");
                const char e = peek();
                ++pos_;
                if (e == 'u')
                    unicodeEscape();
                else if (e != '"' && e != '\\' && e != '/' && e != 'b' && e != 'f' && e != 'n' && e != 'r' && e != 't') {
                    --pos_;
                    fail("invalid escape in a string");
                }
            } else if (c < 0x80) {
                ++pos_;
            } else {
                utf8();
            }
        }
    }

    std::string_view text_;
    size_t pos_ = 0;
};

}  // namespace

std::string jsonObjectError(std::string_view text) {
    try {
        Checker(text).checkDocument();
        return {};
    } catch (const Problem& problem) {
        return problem.message + " (at byte " + std::to_string(problem.offset) + ")";
    }
}

}  // namespace bm3d

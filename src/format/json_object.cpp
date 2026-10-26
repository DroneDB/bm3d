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
    // `members`, when given, receives the members of the top-level object
    Checker(std::string_view text, std::vector<JsonMember>* members) : text_(text), members_(members) {}

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
            const size_t nameStart = pos_;
            string();
            const size_t nameEnd = pos_;
            skipWhiteSpace();
            expect(':', "':' after the name of a member");
            skipWhiteSpace();
            const size_t valueStart = pos_;
            value(depth);
            if (members_ && depth == 1)
                members_->push_back({decodeName(nameStart + 1, nameEnd - 1), std::string(text_.substr(nameStart, nameEnd - nameStart)),
                                     std::string(text_.substr(valueStart, pos_ - valueStart))});
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

    // The text between two quotes of a string that has been checked, with its escapes resolved
    std::string decodeName(size_t begin, size_t end) const {
        std::string out;
        for (size_t i = begin; i < end;) {
            if (text_[i] != '\\') {
                out += text_[i++];
                continue;
            }
            const char e = text_[i + 1];
            i += 2;
            switch (e) {
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t code = hexValue(i);
                    i += 4;
                    if (code >= 0xD800 && code <= 0xDBFF) {  // the checker guarantees a low surrogate follows
                        code = 0x10000 + ((code - 0xD800) << 10) + (hexValue(i + 2) - 0xDC00);
                        i += 6;
                    }
                    appendUtf8(out, code);
                    break;
                }
                default: out += e; break;  // a quote, a backslash or a slash
            }
        }
        return out;
    }

    uint32_t hexValue(size_t at) const {
        uint32_t v = 0;
        for (size_t i = at; i < at + 4; i++) {
            const char c = text_[i];
            v = v * 16 + static_cast<uint32_t>(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
        }
        return v;
    }

    static void appendUtf8(std::string& out, uint32_t code) {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    std::string_view text_;
    std::vector<JsonMember>* members_;
    size_t pos_ = 0;
};

}  // namespace

std::string jsonObjectMembers(std::string_view text, std::vector<JsonMember>& members) {
    members.clear();
    try {
        Checker(text, &members).checkDocument();
        return {};
    } catch (const Problem& problem) {
        members.clear();
        return problem.message + " (at byte " + std::to_string(problem.offset) + ")";
    }
}

std::string jsonObjectError(std::string_view text) {
    try {
        Checker(text, nullptr).checkDocument();
        return {};
    } catch (const Problem& problem) {
        return problem.message + " (at byte " + std::to_string(problem.offset) + ")";
    }
}

}  // namespace bm3d

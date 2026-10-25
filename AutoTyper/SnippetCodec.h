#pragma once
#include "SnippetDocument.h"
#include <limits>

namespace snippetdocument {
namespace json {
inline bool equal(const secure::Text& text, const wchar_t* literal) {
    return text.size() == wcslen(literal) && !wmemcmp(text.data(), literal, text.size());
}

// This is a bounded codec for our schema, not a general JSON DOM. It has fixed
// nesting depth and rejects unknown/duplicate fields instead of discarding data.
class Reader {
    const secure::Text& source_;
    size_t position_ = 0;
    Result error_ = Result::invalid;

    static int hex(wchar_t c) {
        if (c >= L'0' && c <= L'9')
            return c - L'0';
        if (c >= L'a' && c <= L'f')
            return c - L'a' + 10;
        if (c >= L'A' && c <= L'F')
            return c - L'A' + 10;
        return -1;
    }

    bool character(wchar_t& c) {
        if (position_ == source_.size())
            return false;
        c = source_[position_++];
        if (c < 32)
            return false;
        if (c != L'\\')
            return true;
        if (position_ == source_.size())
            return false;
        switch (source_[position_++]) {
        case L'"':
            c = L'"';
            return true;
        case L'\\':
            c = L'\\';
            return true;
        case L'/':
            c = L'/';
            return true;
        case L'b':
            c = L'\b';
            return true;
        case L'f':
            c = L'\f';
            return true;
        case L'n':
            c = L'\n';
            return true;
        case L'r':
            c = L'\r';
            return true;
        case L't':
            c = L'\t';
            return true;
        case L'u': {
            unsigned value = 0;
            for (int i = 0; i < 4; ++i) {
                if (position_ == source_.size())
                    return false;
                const int digit = hex(source_[position_++]);
                if (digit < 0)
                    return false;
                value = value * 16 + digit;
            }
            c = static_cast<wchar_t>(value);
            return true;
        }
        default:
            return false;
        }
    }

public:
    explicit Reader(const secure::Text& source) : source_(source) {}

    Result error() const {
        return error_;
    }

    bool fail(Result result) {
        error_ = result;
        return false;
    }

    void whitespace() {
        while (position_ < source_.size() && (source_[position_] == L' ' || source_[position_] == L'\t' ||
                                                 source_[position_] == L'\r' || source_[position_] == L'\n'))
            ++position_;
    }

    bool consume(wchar_t c) {
        whitespace();
        if (position_ == source_.size() || source_[position_] != c)
            return false;
        ++position_;
        return true;
    }

    bool string(secure::Text& output, size_t maximum = 64) {
        if (!consume(L'"'))
            return false;
        const size_t first = position_;
        size_t count = 0;
        while (position_ < source_.size() && source_[position_] != L'"') {
            wchar_t c = 0;
            if (!character(c))
                return false;
            if (++count > maximum)
                return fail(Result::tooLarge);
        }
        if (position_ == source_.size())
            return false;
        secure::Text value = count ? secure::Text(count) : secure::Text{};
        position_ = first;
        for (size_t i = 0; i < count; ++i)
            if (!character(value.writable()[i]))
                return false;
        ++position_; // Closing quote was checked in the first pass.
        value.length(count);
        if (!validText(value))
            return false;
        output = std::move(value);
        return true;
    }

    bool number(unsigned& output) {
        whitespace();
        if (position_ == source_.size() || source_[position_] < L'0' || source_[position_] > L'9')
            return false;
        const bool zero = source_[position_] == L'0';
        unsigned value = 0;
        size_t digits = 0;
        while (position_ < source_.size() && source_[position_] >= L'0' && source_[position_] <= L'9') {
            const unsigned digit = source_[position_++] - L'0';
            if (value > ((std::numeric_limits<unsigned>::max)() - digit) / 10)
                return false;
            value = value * 10 + digit;
            ++digits;
        }
        if (zero && digits != 1)
            return false;
        output = value;
        return true;
    }

    bool boolean(bool& output) {
        whitespace();
        for (const auto* literal : {L"true", L"false"}) {
            const size_t size = wcslen(literal);
            if (size <= source_.size() - position_ && !wmemcmp(source_.data() + position_, literal, size)) {
                position_ += size;
                output = literal[0] == L't';
                return true;
            }
        }
        return false;
    }

    bool action(Action& output) {
        if (!consume(L'{'))
            return false;
        unsigned fields = 0;
        secure::Text type, encoding;
        do {
            secure::Text name;
            if (!string(name) || !consume(L':'))
                return false;
            unsigned field = 0;
            if (equal(name, L"type"))
                field = 1;
            else if (equal(name, L"text"))
                field = 2;
            else if (equal(name, L"encoding"))
                field = 4;
            else if (equal(name, L"code"))
                field = 8;
            else if (equal(name, L"modifiers"))
                field = 16;
            else if (equal(name, L"extended"))
                field = 32;
            else if (equal(name, L"milliseconds"))
                field = 64;
            else
                return fail(Result::unsupportedAction);
            if (fields & field)
                return false;
            fields |= field;
            switch (field) {
            case 1:
                if (!string(type))
                    return false;
                break;
            case 2:
                if (!string(output.text, MaximumTextCharacters))
                    return false;
                break;
            case 4:
                if (!string(encoding))
                    return false;
                break;
            case 8:
                if (!number(output.key.code))
                    return false;
                break;
            case 16:
                if (!number(output.key.modifiers))
                    return false;
                break;
            case 32:
                if (!boolean(output.key.extended))
                    return false;
                break;
            case 64: {
                unsigned value = 0;
                if (!number(value))
                    return false;
                output.milliseconds = value;
                break;
            }
            }
        } while (consume(L','));
        if (!consume(L'}'))
            return false;
        if (equal(type, L"text")) {
            if (fields != (1 | 2))
                return false;
            output.type = ActionType::text;
        } else if (equal(type, L"key")) {
            if (fields != (1 | 4 | 8 | 16 | 32))
                return false;
            output.type = ActionType::key;
            if (equal(encoding, L"virtualKey"))
                output.key.encoding = KeyEncoding::virtualKey;
            else if (equal(encoding, L"scanCode"))
                output.key.encoding = KeyEncoding::scanCode;
            else
                return fail(Result::unsupportedAction);
        } else if (equal(type, L"delay")) {
            if (fields != (1 | 64))
                return false;
            output.type = ActionType::delay;
        } else
            return fail(Result::unsupportedAction);
        return true;
    }

    bool actions(Document& output) {
        if (!consume(L'['))
            return false;
        if (consume(L']'))
            return true;
        size_t textCount = 0;
        do {
            if (output.actions.size() == MaximumActions)
                return fail(Result::tooLarge);
            Action value;
            if (!action(value))
                return false;
            if (value.text.size() > MaximumTextCharacters - textCount)
                return fail(Result::tooLarge);
            textCount += value.text.size();
            output.actions.push_back(std::move(value));
        } while (consume(L','));
        return consume(L']');
    }

    bool document(Document& output, bool requireEnd = true) {
        if (!consume(L'{'))
            return false;
        unsigned fields = 0;
        do {
            secure::Text name;
            if (!string(name) || !consume(L':'))
                return false;
            unsigned field = 0;
            if (equal(name, L"format"))
                field = 1;
            else if (equal(name, L"version"))
                field = 2;
            else if (equal(name, L"actions"))
                field = 4;
            else
                return fail(Result::unsupportedVersion);
            if (fields & field)
                return false;
            fields |= field;
            if (field == 1) {
                secure::Text format;
                if (!string(format))
                    return false;
                if (!equal(format, L"mk-autotyper-snippet"))
                    return fail(Result::unsupportedVersion);
            } else if (field == 2) {
                unsigned version = 0;
                if (!number(version))
                    return false;
                if (version != FormatVersion)
                    return fail(Result::unsupportedVersion);
            } else if (!actions(output))
                return false;
        } while (consume(L','));
        if (!consume(L'}') || fields != 7)
            return false;
        return !requireEnd || finished();
    }

    bool finished() {
        whitespace();
        return position_ == source_.size();
    }
};

class Writer {
    wchar_t* output_;
    size_t position_ = 0;
    size_t maximum_;
    bool full_ = false;

public:
    explicit Writer(wchar_t* output = nullptr, size_t maximum = MaximumEncodedCharacters)
        : output_(output), maximum_(maximum) {}

    size_t size() const {
        return position_;
    }

    bool full() const {
        return full_;
    }

    void character(wchar_t c) {
        if (position_ == maximum_) {
            full_ = true;
            return;
        }
        if (output_)
            output_[position_] = c;
        ++position_;
    }

    void literal(const wchar_t* value) {
        while (*value)
            character(*value++);
    }

    void number(unsigned value) {
        wchar_t digits[11]{};
        swprintf_s(digits, L"%u", value);
        literal(digits);
        SecureZeroMemory(digits, sizeof(digits));
    }

    void string(const secure::Text& value) {
        character(L'"');
        for (size_t i = 0; i < value.size(); ++i) {
            const wchar_t c = value[i];
            switch (c) {
            case L'"':
                literal(L"\\\"");
                break;
            case L'\\':
                literal(L"\\\\");
                break;
            case L'\r':
                literal(L"\\r");
                break;
            case L'\n':
                literal(L"\\n");
                break;
            case L'\t':
                literal(L"\\t");
                break;
            case L'\b':
                literal(L"\\b");
                break;
            case L'\f':
                literal(L"\\f");
                break;
            default:
                if (c < 32) {
                    literal(L"\\u00");
                    character(L"0123456789abcdef"[(c >> 4) & 15]);
                    character(L"0123456789abcdef"[c & 15]);
                } else
                    character(c);
            }
        }
        character(L'"');
    }

    void document(const Document& value) {
        literal(L"{\n  \"format\": \"mk-autotyper-snippet\",\n  \"version\": ");
        number(FormatVersion);
        literal(L",\n  \"actions\": [");
        for (size_t i = 0; i < value.actions.size(); ++i) {
            const auto& action = value.actions[i];
            literal(i ? L",\n    {\"type\": " : L"\n    {\"type\": ");
            if (action.type == ActionType::text) {
                literal(L"\"text\", \"text\": ");
                string(action.text);
            } else if (action.type == ActionType::key) {
                literal(L"\"key\", \"encoding\": ");
                literal(action.key.encoding == KeyEncoding::virtualKey ? L"\"virtualKey\"" : L"\"scanCode\"");
                literal(L", \"code\": ");
                number(action.key.code);
                literal(L", \"modifiers\": ");
                number(action.key.modifiers);
                literal(L", \"extended\": ");
                literal(action.key.extended ? L"true" : L"false");
            } else {
                literal(L"\"delay\", \"milliseconds\": ");
                number(action.milliseconds);
            }
            character(L'}');
        }
        literal(L"\n  ]\n}\n");
    }
};
} // namespace json

inline Result decode(const secure::Text& encoded, Document& output) {
    output.clear();
    if (encoded.size() > MaximumEncodedCharacters)
        return Result::tooLarge;
    Document value;
    json::Reader reader(encoded);
    if (!reader.document(value))
        return reader.error();
    const auto result = validate(value);
    if (result == Result::ok)
        output = std::move(value);
    return result;
}

inline Result encode(const Document& document, secure::Text& output) {
    output.clear();
    const auto result = validate(document);
    if (result != Result::ok)
        return result;
    json::Writer counter;
    counter.document(document);
    if (counter.full())
        return Result::tooLarge;
    secure::Text encoded(counter.size());
    json::Writer writer(encoded.writable());
    writer.document(document);
    encoded.length(writer.size());
    output = std::move(encoded);
    return Result::ok;
}
} // namespace snippetdocument

#pragma once
#include "SnippetDocument.h"
#include <functional>

namespace snippetvariables {
constexpr size_t MaximumFields = 32, MaximumName = 64, MaximumValue = 65536;
enum class Result {
    ok,
    invalid,
    tooLarge,
    missing
};

struct Field {
    secure::Text name, value;
};

// A token borrows its source buffer only during the synchronous visitor call.
// Repeated placeholders therefore need no allocation or page locking.
struct Name {
    const wchar_t* first;
    size_t count;

    const wchar_t* data() const {
        return first;
    }

    size_t size() const {
        return count;
    }
};

template <class Text> inline bool equal(const Text& text, const wchar_t* literal) {
    return text.size() == wcslen(literal) && !wmemcmp(text.data(), literal, text.size());
}

inline bool builtin(Name name) {
    return equal(name, L"date") || equal(name, L"time");
}

// Escapes are {{ and }}. Substituted values are literal text, never reparsed.
template <class Literal, class Variable>
Result visit(const secure::Text& text, Literal literal, Variable variable) {
    for (size_t i = 0; i < text.size(); ++i) {
        const wchar_t c = text[i];
        if (c != L'{' && c != L'}') {
            if (!literal(c))
                return Result::tooLarge;
            continue;
        }
        if (i + 1 < text.size() && text[i + 1] == c) {
            ++i;
            if (!literal(c))
                return Result::tooLarge;
            continue;
        }
        if (c == L'}')
            return Result::invalid;
        const size_t first = ++i;
        while (i < text.size() && text[i] != L'}') {
            if (i - first == MaximumName ||
                !(IsCharAlphaNumericW(text[i]) || text[i] == L'_' || text[i] == L'-'))
                return Result::invalid;
            ++i;
        }
        if (i == text.size() || i == first)
            return Result::invalid;
        const Name name{text.data() + first, i - first};
        const auto result = variable(name);
        if (result != Result::ok)
            return result;
    }
    return Result::ok;
}

inline Result collect(const snippetdocument::Document& document, std::vector<Field>& fields) {
    fields.clear();
    if (!document.expandVariables)
        return Result::ok;
    std::vector<Field> found;
    for (const auto& action : document.actions) {
        if (action.type != snippetdocument::ActionType::text)
            continue;
        const auto result = visit(
            action.text, [](wchar_t) { return true; },
            [&](Name name) {
                if (builtin(name))
                    return Result::ok;
                for (const auto& field : found)
                    if ((field.name.size() == name.size() &&
                            !wmemcmp(field.name.data(), name.data(), name.size())))
                        return Result::ok;
                if (found.size() == MaximumFields)
                    return Result::tooLarge;
                Field field;
                field.name = secure::Text(name.data(), name.size());
                found.push_back(std::move(field));
                return Result::ok;
            });
        if (result != Result::ok)
            return result;
    }
    fields = std::move(found);
    return Result::ok;
}

inline Result resolve(
    snippetdocument::Document& document, const std::vector<Field>& fields, const SYSTEMTIME& time) {
    if (!document.expandVariables)
        return Result::ok;
    for (const auto& field : fields)
        if (field.value.size() > MaximumValue || !snippetdocument::validText(field.value))
            return Result::tooLarge;
    wchar_t formatted[32]{};
    swprintf_s(formatted, L"%04u-%02u-%02u", time.wYear, time.wMonth, time.wDay);
    secure::Text date(formatted, wcslen(formatted));
    swprintf_s(formatted, L"%02u:%02u:%02u", time.wHour, time.wMinute, time.wSecond);
    secure::Text clock(formatted, wcslen(formatted));
    SecureZeroMemory(formatted, sizeof(formatted));
    auto resolved = snippetdocument::clone(document);
    size_t total = 0;
    for (auto& action : resolved.actions) {
        if (action.type != snippetdocument::ActionType::text)
            continue;
        const auto value = [&](Name name) -> const secure::Text* {
            if (equal(name, L"date"))
                return &date;
            if (equal(name, L"time"))
                return &clock;
            for (const auto& field : fields)
                if ((field.name.size() == name.size() &&
                        !wmemcmp(field.name.data(), name.data(), name.size())))
                    return &field.value;
            return nullptr;
        };
        size_t count = 0;
        auto countOne = [&](wchar_t) { return ++count <= snippetdocument::MaximumTextCharacters - total; };
        const auto counted = visit(action.text, countOne, [&](Name name) {
            const auto* replacement = value(name);
            if (!replacement)
                return Result::missing;
            if (replacement->size() > snippetdocument::MaximumTextCharacters - total - count)
                return Result::tooLarge;
            count += replacement->size();
            return Result::ok;
        });
        if (counted != Result::ok)
            return counted;
        secure::Text output(count);
        size_t used = 0;
        const auto written = visit(
            action.text,
            [&](wchar_t c) {
                output.writable()[used++] = c;
                return true;
            },
            [&](Name name) {
                const auto* replacement = value(name);
                if (!replacement)
                    return Result::missing;
                if (replacement->size())
                    memcpy(
                        output.writable() + used, replacement->data(), replacement->size() * sizeof(wchar_t));
                used += replacement->size();
                return Result::ok;
            });
        if (written != Result::ok)
            return written;
        output.length(used);
        action.text = std::move(output);
        total += used;
    }
    resolved.expandVariables = false;
    document = std::move(resolved);
    return Result::ok;
}
} // namespace snippetvariables

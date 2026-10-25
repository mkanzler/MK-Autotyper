#pragma once
#include "SecureMemory.h"
#include <vector>

namespace snippetdocument {
constexpr unsigned FormatVersion = 1;
constexpr size_t MaximumActions = 4096, MaximumTextCharacters = 1024 * 1024;
constexpr size_t MaximumEncodedCharacters = 8 * 1024 * 1024;
constexpr DWORD MaximumDelayMilliseconds = 24 * 60 * 60 * 1000;

enum class Result {
    ok,
    invalid,
    tooLarge,
    unsupportedVersion,
    unsupportedAction,
    notText
};
enum class ActionType {
    text,
    key,
    delay
};
enum class KeyEncoding {
    virtualKey,
    scanCode
};

enum Modifier : unsigned {
    control = 1,
    alt = 2,
    shift = 4,
    windows = 8
};

struct KeyStroke {
    KeyEncoding encoding = KeyEncoding::virtualKey;
    unsigned code = 0, modifiers = 0;
    bool extended = false;
};

// Actions describe intent. Storage never sends input or interprets commands in text.
struct Action {
    Action() = default;
    Action(const Action&) = delete;
    Action& operator=(const Action&) = delete;
    Action(Action&&) noexcept = default;
    Action& operator=(Action&&) noexcept = default;

    ActionType type = ActionType::text;
    secure::Text text;
    KeyStroke key;
    DWORD milliseconds = 0;

    ~Action() {
        // A sequence of individual key actions can itself contain secret data.
        SecureZeroMemory(&key, sizeof(key));
        SecureZeroMemory(&milliseconds, sizeof(milliseconds));
    }

    static Action typeText(secure::Text value) {
        Action action;
        action.text = std::move(value);
        return action;
    }

    static Action pressKey(KeyStroke value) {
        Action action;
        action.type = ActionType::key;
        action.key = value;
        return action;
    }

    static Action delay(DWORD value) {
        Action action;
        action.type = ActionType::delay;
        action.milliseconds = value;
        return action;
    }
};

struct Document {
    Document() = default;
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
    Document(Document&&) noexcept = default;
    Document& operator=(Document&&) noexcept = default;

    std::vector<Action> actions;

    static Document fromText(secure::Text value) {
        Document document;
        document.actions.push_back(Action::typeText(std::move(value)));
        return document;
    }

    void clear() {
        actions.clear();
    }
};

inline bool validText(const secure::Text& text) {
    for (size_t i = 0; i < text.size(); ++i) {
        const wchar_t c = text[i];
        if (!c || (c >= 0xdc00 && c <= 0xdfff))
            return false;
        if (c >= 0xd800 && c <= 0xdbff) {
            if (++i == text.size() || text[i] < 0xdc00 || text[i] > 0xdfff)
                return false;
        }
    }
    return true;
}

inline Result validate(const Document& document) {
    if (document.actions.size() > MaximumActions)
        return Result::tooLarge;
    size_t characters = 0;
    for (const auto& action : document.actions) {
        if (action.type == ActionType::text) {
            if (action.text.size() > MaximumTextCharacters - characters)
                return Result::tooLarge;
            characters += action.text.size();
            if (!validText(action.text) || action.key.code || action.key.modifiers || action.key.extended ||
                action.key.encoding != KeyEncoding::virtualKey || action.milliseconds)
                return Result::invalid;
        } else if (action.type == ActionType::key) {
            if (!action.text.empty() || action.milliseconds || (action.key.modifiers & ~15u) ||
                action.key.code == 0 ||
                action.key.code > (action.key.encoding == KeyEncoding::scanCode ? 255u : 254u) ||
                (action.key.encoding != KeyEncoding::virtualKey &&
                    action.key.encoding != KeyEncoding::scanCode) ||
                (action.key.encoding == KeyEncoding::virtualKey && action.key.extended))
                return Result::invalid;
        } else if (action.type == ActionType::delay) {
            if (!action.text.empty() || action.key.code || action.key.modifiers || action.key.extended ||
                action.key.encoding != KeyEncoding::virtualKey)
                return Result::invalid;
            if (action.milliseconds > MaximumDelayMilliseconds)
                return Result::tooLarge;
        } else {
            return Result::unsupportedAction;
        }
    }
    return Result::ok;
}

// The existing editor cannot represent key/delay actions. Never flatten them or
// return partial text: that would allow a later Save to destroy those actions.
inline Result takeEditorText(Document& document, secure::Text& text) {
    text.clear();
    const auto result = validate(document);
    if (result != Result::ok)
        return result;
    if (document.actions.empty())
        return Result::ok;
    if (document.actions.size() != 1 || document.actions.front().type != ActionType::text)
        return Result::notText;
    text = std::move(document.actions.front().text);
    document.clear();
    return Result::ok;
}
} // namespace snippetdocument

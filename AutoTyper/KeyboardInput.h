#pragma once

#include <Windows.h>
#include "SecureMemory.h"
#include "SnippetDocument.h"
#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace keyboard {
enum class Result {
    completed,
    cancelled,
    unsupportedLayout,
    inputBlocked,
    shortcutConflict,
    invalidSequence
};

inline bool supported(HKL layout) {
    const WORD language = PRIMARYLANGID(LOWORD(reinterpret_cast<ULONG_PTR>(layout)));
    return language == LANG_ENGLISH || language == LANG_GERMAN;
}

inline INPUT key(WORD vk, HKL layout, bool up = false) {
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    const UINT scan = MapVirtualKeyExW(vk, MAPVK_VK_TO_VSC_EX, layout);
    input.ki.wScan = LOBYTE(scan);
    input.ki.dwFlags = KEYEVENTF_SCANCODE | (up ? KEYEVENTF_KEYUP : 0);
    if ((scan & 0xff00) == 0xe000)
        input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    return input;
}

inline void appendKey(std::vector<INPUT>& inputs, WORD vk, HKL layout) {
    inputs.push_back(key(vk, layout));
    inputs.push_back(key(vk, layout, true));
}

inline void wipeEvents(std::vector<INPUT>& inputs) {
    if (!inputs.empty())
        SecureZeroMemory(inputs.data(), inputs.size() * sizeof(INPUT));
}

struct EventGuard {
    std::vector<INPUT>& inputs;

    ~EventGuard() {
        wipeEvents(inputs);
    }
};

// Plan one UTF-16 unit. Use physical keys only when the target layout produces
// exactly the requested character; dead keys and unmapped text use VK_PACKET.
inline std::vector<INPUT> character(
    wchar_t c, HKL layout, bool unicode, bool capsLock, bool shiftEnter = false) {
    std::vector<INPUT> inputs;
    inputs.reserve(10);
    if (c == L'\r' || c == L'\n' || c == L'\t' || c == L'\b') {
        const bool shifted = shiftEnter && (c == L'\r' || c == L'\n');
        if (shifted)
            inputs.push_back(key(VK_LSHIFT, layout));
        appendKey(inputs, c == L'\t' ? VK_TAB : c == L'\b' ? VK_BACK : VK_RETURN, layout);
        if (shifted)
            inputs.push_back(key(VK_LSHIFT, layout, true));
        return inputs;
    }
    if (!unicode) {
        const SHORT mapping = VkKeyScanExW(c, layout);
        if (mapping != -1 && (HIBYTE(mapping) & ~7) == 0) {
            const WORD vk = LOBYTE(mapping);
            const BYTE modifiers = HIBYTE(mapping);
            // German @ needs genuine left Ctrl+Alt+Q for applications that
            // distinguish Ctrl+Alt from the right-Alt/AltGr key.
            const WORD altKey =
                c == L'@' && PRIMARYLANGID(LOWORD(reinterpret_cast<ULONG_PTR>(layout))) == LANG_GERMAN &&
                        vk == 'Q' && modifiers == 6
                    ? VK_LMENU
                    : VK_RMENU;
            // Caps Lock can reverse the shift state of letters, including umlauts.
            for (int attempt = 0; attempt < 2; ++attempt) {
                const bool shift = ((modifiers & 1) != 0) != (attempt != 0);
                BYTE state[256] = {};
                state[VK_CAPITAL] = capsLock ? 1 : 0;
                state[VK_SHIFT] = shift ? 0x80 : 0;
                state[VK_CONTROL] = (modifiers & 2) ? 0x80 : 0;
                state[VK_MENU] = (modifiers & 4) ? 0x80 : 0;
                wchar_t translated[8] = {};
                // Flag 4 avoids changing Windows' dead-key state (Windows 10 1607+).
                const int count = ToUnicodeEx(
                    vk, MapVirtualKeyExW(vk, MAPVK_VK_TO_VSC, layout), state, translated, 8, 4, layout);
                const bool exact = count == 1 && translated[0] == c;
                SecureZeroMemory(translated, sizeof(translated));
                if (!exact)
                    continue;
                if (shift)
                    inputs.push_back(key(VK_LSHIFT, layout));
                if (modifiers & 2)
                    inputs.push_back(key(VK_LCONTROL, layout));
                if (modifiers & 4)
                    inputs.push_back(key(altKey, layout));
                appendKey(inputs, vk, layout);
                if (modifiers & 4)
                    inputs.push_back(key(altKey, layout, true));
                if (modifiers & 2)
                    inputs.push_back(key(VK_LCONTROL, layout, true));
                if (shift)
                    inputs.push_back(key(VK_LSHIFT, layout, true));
                return inputs;
            }
        }
    }
    INPUT down = {};
    down.type = INPUT_KEYBOARD;
    down.ki.wScan = c;
    down.ki.dwFlags = KEYEVENTF_UNICODE;
    inputs.push_back(down);
    down.ki.dwFlags |= KEYEVENTF_KEYUP;
    inputs.push_back(down);
    SecureZeroMemory(&down, sizeof(down));
    return inputs;
}

inline bool modifiersDown() {
    const int keys[] = {VK_SHIFT, VK_CONTROL, VK_MENU, VK_LWIN, VK_RWIN};
    for (int vk : keys)
        if (GetAsyncKeyState(vk) & 0x8000)
            return true;
    return false;
}

inline bool pause(DWORD milliseconds, const std::atomic_bool& stop) {
    while (milliseconds && !stop) {
        const DWORD slice = milliseconds > 10 ? 10 : milliseconds;
        Sleep(slice);
        milliseconds -= slice;
    }
    return !stop;
}

enum class Stage {
    countdown,
    waitingForKeys,
    typing,
    pause
};

struct Progress {
    Stage stage = Stage::countdown;
    size_t completed = 0, total = 0;
    DWORD remaining = 0;
    ULONGLONG elapsed = 0;
    bool sequence = false;
    size_t actionIndex = 0, actionCount = 0;
};

struct Options {
    DWORD delay = 10, initialDelay = 1000;
    bool unicode = false, shiftEnter = false;
    UINT triggerKey = 'V';
    UINT hotkeyModifiers = MOD_CONTROL | MOD_ALT;
    UINT snippetHotkeyModifiers = MOD_CONTROL | MOD_ALT, snippetHotkeyKey = 'P';
    HWND target = nullptr;
};

using Reporter = std::function<void(const Progress&)>;

inline bool wouldTriggerShortcut(const std::vector<INPUT>& inputs, HKL layout, UINT modifiers, UINT keyCode) {
    UINT down = 0;
    for (const auto& input : inputs) {
        if (input.ki.dwFlags & KEYEVENTF_UNICODE)
            continue;
        const UINT scan = input.ki.wScan | ((input.ki.dwFlags & KEYEVENTF_EXTENDEDKEY) ? 0xe000 : 0);
        const UINT vk = (input.ki.dwFlags & KEYEVENTF_SCANCODE)
                            ? MapVirtualKeyExW(scan, MAPVK_VSC_TO_VK_EX, layout)
                            : input.ki.wVk;
        const bool up = (input.ki.dwFlags & KEYEVENTF_KEYUP) != 0;
        UINT modifier = 0;
        if (vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_SHIFT)
            modifier = MOD_SHIFT;
        if (vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_CONTROL)
            modifier = MOD_CONTROL;
        if (vk == VK_LMENU || vk == VK_RMENU || vk == VK_MENU)
            modifier = MOD_ALT;
        if (vk == VK_LWIN || vk == VK_RWIN)
            modifier = MOD_WIN;
        if (modifier) {
            if (up)
                down &= ~modifier;
            else
                down |= modifier;
        } else if (!up && vk == keyCode && down == modifiers)
            return true;
    }
    return false;
}

inline std::vector<INPUT> events(wchar_t characterValue, HKL layout, const Options& options, bool capsLock) {
    auto inputs = character(characterValue, layout, options.unicode, capsLock, options.shiftEnter);
    // An AltGr character can share the configured global shortcut. Send that
    // character as Unicode so the app never stops itself through SendInput.
    if (wouldTriggerShortcut(inputs, layout, options.hotkeyModifiers, options.triggerKey) ||
        wouldTriggerShortcut(inputs, layout, options.snippetHotkeyModifiers, options.snippetHotkeyKey)) {
        wipeEvents(inputs);
        inputs = character(characterValue, layout, true, false, options.shiftEnter);
    }
    return inputs;
}

inline std::wstring prepare(const std::wstring& text) {
    std::wstring result;
    result.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        wchar_t c = text[i];
        if (c == L'\n' && i && text[i - 1] == L'\r')
            continue;
        if (c == L'\n')
            c = L'\r';
        if (c < L' ' && c != L'\r' && c != L'\t' && c != L'\b')
            continue;
        result += c;
    }
    return result;
}

inline bool surrogatePair(wchar_t first, wchar_t second) {
    return first >= 0xd800 && first <= 0xdbff && second >= 0xdc00 && second <= 0xdfff;
}

template <class Text> inline size_t characterCountPrivate(const Text& text) {
    size_t count = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (i + 1 < text.size() && surrogatePair(text[i], text[i + 1]))
            ++i;
        ++count;
    }
    return count;
}

inline size_t characterCount(const std::wstring& text) {
    return characterCountPrivate(text);
}

inline size_t characterCount(const secure::Text& text) {
    return characterCountPrivate(text);
}

template <class Text> inline bool hasText(const Text& source) {
    for (size_t i = 0; i < source.size(); ++i)
        if (source[i] >= L' ' || source[i] == L'\r' || source[i] == L'\n' || source[i] == L'\t' ||
            source[i] == L'\b')
            return true;
    return false;
}

inline void normalizePrivate(secure::Text& source) {
    size_t used = 0;
    const size_t original = source.size();
    wchar_t previous = 0;
    for (size_t i = 0; i < source.size(); ++i) {
        wchar_t c = source[i];
        const bool pair = c == L'\n' && previous == L'\r';
        previous = c;
        if (pair)
            continue;
        if (c == L'\n')
            c = L'\r';
        if (c < L' ' && c != L'\r' && c != L'\t' && c != L'\b')
            continue;
        source.writable()[used++] = c;
    }
    if (original > used)
        SecureZeroMemory(source.writable() + used, (original - used) * sizeof(wchar_t));
    source.length(used);
}

inline Options optionsFor(const snippetdocument::Document& document, Options global) {
    if (document.typing.overrideGlobal) {
        global.delay = document.typing.keystrokeDelay;
        global.initialDelay = document.typing.initialDelay;
        global.unicode = document.typing.unicode;
    }
    return global;
}

inline std::vector<INPUT> chord(const snippetdocument::KeyStroke& stroke, HKL layout) {
    std::vector<INPUT> inputs;
    inputs.reserve(10);
    auto virtualKey = [&](WORD vk, bool up) {
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = vk;
        input.ki.dwFlags = (up ? KEYEVENTF_KEYUP : 0);
        if ((MapVirtualKeyExW(vk, MAPVK_VK_TO_VSC_EX, layout) & 0xff00) == 0xe000)
            input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        inputs.push_back(input);
    };
    const WORD modifiers[] = {VK_LCONTROL, VK_LMENU, VK_LSHIFT, VK_LWIN};
    unsigned mask = stroke.modifiers;
    const unsigned mainKey =
        stroke.encoding == snippetdocument::KeyEncoding::virtualKey
            ? stroke.code
            : MapVirtualKeyExW(stroke.code | (stroke.extended ? 0xe000 : 0), MAPVK_VSC_TO_VK_EX, layout);
    if (mainKey == VK_CONTROL || mainKey == VK_LCONTROL || mainKey == VK_RCONTROL)
        mask &= ~snippetdocument::control;
    if (mainKey == VK_MENU || mainKey == VK_LMENU || mainKey == VK_RMENU)
        mask &= ~snippetdocument::alt;
    if (mainKey == VK_SHIFT || mainKey == VK_LSHIFT || mainKey == VK_RSHIFT)
        mask &= ~snippetdocument::shift;
    if (mainKey == VK_LWIN || mainKey == VK_RWIN)
        mask &= ~snippetdocument::windows;
    for (unsigned i = 0; i < 4; ++i)
        if (mask & (1u << i))
            virtualKey(modifiers[i], false);
    if (stroke.encoding == snippetdocument::KeyEncoding::scanCode) {
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = static_cast<WORD>(stroke.code);
        input.ki.dwFlags = KEYEVENTF_SCANCODE | (stroke.extended ? KEYEVENTF_EXTENDEDKEY : 0);
        inputs.push_back(input);
        input.ki.dwFlags |= KEYEVENTF_KEYUP;
        inputs.push_back(input);
    } else {
        virtualKey(static_cast<WORD>(stroke.code), false);
        virtualKey(static_cast<WORD>(stroke.code), true);
    }
    for (int i = 3; i >= 0; --i)
        if (mask & (1u << i))
            virtualKey(modifiers[i], true);
    return inputs;
}

inline bool sendBatch(std::vector<INPUT>& inputs) {
    const UINT sent = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    if (sent == inputs.size())
        return true;
    // A partial batch can leave modifier keys down. Release every accepted down
    // event in reverse order, using the same encoding and extended-key flags.
    for (UINT n = sent; n > 0; --n) {
        INPUT release = inputs[n - 1];
        if (!(release.ki.dwFlags & KEYEVENTF_KEYUP)) {
            release.ki.dwFlags |= KEYEVENTF_KEYUP;
            SendInput(1, &release, sizeof(release));
        }
        SecureZeroMemory(&release, sizeof(release));
    }
    return false;
}

struct Step {
    secure::Text* text = nullptr;
    snippetdocument::KeyStroke* stroke = nullptr;
    DWORD milliseconds = 0;
};

inline Result runSteps(Step* steps, size_t count, const Options& options, const std::atomic_bool& stop,
    const Reporter& report, bool sequence) {
    if (stop)
        return Result::cancelled;
    Progress progress;
    progress.sequence = sequence;
    progress.actionCount = count;
    for (size_t n = 0; n < count; ++n) {
        if (steps[n].text) {
            normalizePrivate(*steps[n].text);
            progress.total += characterCount(*steps[n].text);
        } else if (steps[n].stroke)
            ++progress.total;
    }
    ULONGLONG lastReport = 0, typingStarted = 0;
    auto publish = [&](bool force = false) {
        const ULONGLONG now = GetTickCount64();
        progress.elapsed = typingStarted ? now - typingStarted : 0;
        if (report && (force || now - lastReport >= 100)) {
            report(progress);
            lastReport = now;
        }
    };
    HWND target = options.target;
    auto safe = [&] { return !stop && (!target || GetForegroundWindow() == target); };
    const ULONGLONG beginAt = GetTickCount64() + options.initialDelay;
    do {
        if (!safe())
            return Result::cancelled;
        const ULONGLONG now = GetTickCount64();
        progress.remaining = now < beginAt ? static_cast<DWORD>(beginAt - now) : 0;
        publish();
        if (!progress.remaining)
            break;
        Sleep(progress.remaining < 10 ? progress.remaining : 10);
    } while (true);
    progress.remaining = 0;
    auto releaseKeys = [&](bool includeTrigger) {
        const ULONGLONG deadline = GetTickCount64() + 5000;
        while (modifiersDown() || (includeTrigger && (GetAsyncKeyState(options.triggerKey) & 0x8000))) {
            progress.stage = Stage::waitingForKeys;
            publish();
            if (!safe() || GetTickCount64() >= deadline)
                return false;
            Sleep(10);
        }
        return safe();
    };
    if (!releaseKeys(true))
        return Result::cancelled;
    if (!target)
        target = GetForegroundWindow();
    if (!target)
        return Result::cancelled;
    DWORD targetProcess = 0;
    const DWORD thread = GetWindowThreadProcessId(target, &targetProcess);
    if (!thread || targetProcess == GetCurrentProcessId())
        return Result::cancelled;
    const HKL layout = GetKeyboardLayout(thread);
    if (!supported(layout))
        return Result::unsupportedLayout;
    for (size_t n = 0; n < count; ++n) {
        if (!steps[n].stroke)
            continue;
        auto planned = chord(*steps[n].stroke, layout);
        EventGuard wipe{planned};
        if (wouldTriggerShortcut(planned, layout, options.hotkeyModifiers, options.triggerKey) ||
            wouldTriggerShortcut(planned, layout, options.snippetHotkeyModifiers, options.snippetHotkeyKey))
            return Result::shortcutConflict;
    }
    typingStarted = GetTickCount64();
    progress.stage = Stage::typing;
    publish(true);
    auto wait = [&](DWORD remaining, Stage stage) {
        progress.stage = stage;
        while (remaining) {
            if (!safe() || GetKeyboardLayout(thread) != layout)
                return false;
            progress.remaining = remaining;
            publish();
            const DWORD slice = remaining < 10 ? remaining : 10;
            Sleep(slice);
            remaining -= slice;
        }
        progress.remaining = 0;
        return safe();
    };
    for (size_t n = 0; n < count; ++n) {
        auto& step = steps[n];
        progress.actionIndex = n;
        if (!safe() || !releaseKeys(false) || GetKeyboardLayout(thread) != layout)
            return Result::cancelled;
        if (!step.text && !step.stroke) {
            if (!wait(step.milliseconds, Stage::pause))
                return Result::cancelled;
        } else if (step.stroke) {
            progress.stage = Stage::typing;
            auto inputs = chord(*step.stroke, layout);
            EventGuard erase{inputs};
            if (!sendBatch(inputs))
                return Result::inputBlocked;
            SecureZeroMemory(step.stroke, sizeof(*step.stroke));
            ++progress.completed;
        } else {
            auto& text = *step.text;
            for (size_t i = 0; i < text.size(); ++i) {
                const size_t first = i;
                if (!safe() || !releaseKeys(false) || GetKeyboardLayout(thread) != layout)
                    return Result::cancelled;
                progress.stage = Stage::typing;
                auto inputs = events(text[i], layout, options, (GetKeyState(VK_CAPITAL) & 1) != 0);
                EventGuard erase{inputs};
                if (i + 1 < text.size() && surrogatePair(text[i], text[i + 1])) {
                    auto tail = character(text[++i], layout, true, false);
                    EventGuard eraseTail{tail};
                    inputs.insert(inputs.end(), tail.begin(), tail.end());
                }
                if (!sendBatch(inputs))
                    return Result::inputBlocked;
                wipeEvents(inputs);
                SecureZeroMemory(text.writable() + first, (i - first + 1) * sizeof(wchar_t));
                ++progress.completed;
                publish(progress.completed == progress.total);
                if (i + 1 < text.size() && !wait(options.delay, Stage::typing))
                    return Result::cancelled;
            }
        }
        progress.actionIndex = n + 1;
        publish(true);
        // Empty template fields do not add extra inter-key delays. Explicit
        // pause steps remain the only timing boundary before the next input.
        size_t next = n + 1;
        while (next < count && steps[next].text && steps[next].text->empty())
            ++next;
        if (next < count && (step.stroke || (step.text && !step.text->empty())) &&
            (steps[next].text || steps[next].stroke) && !wait(options.delay, Stage::typing))
            return Result::cancelled;
    }
    publish(true);
    return Result::completed;
}

inline Result runSecure(
    secure::Text& text, const Options& options, const std::atomic_bool& stop, const Reporter& report = {}) {
    Step step{&text, nullptr, 0};
    return runSteps(&step, 1, options, stop, report, false);
}

inline Result runDocument(snippetdocument::Document& document, const Options& options,
    const std::atomic_bool& stop, const Reporter& report = {}) {
    if (snippetdocument::validate(document) != snippetdocument::Result::ok || document.expandVariables)
        return Result::invalidSequence;
    std::vector<Step> steps;
    steps.reserve(document.actions.size());
    for (auto& action : document.actions) {
        if (action.type == snippetdocument::ActionType::text)
            steps.push_back({&action.text, nullptr, 0});
        else if (action.type == snippetdocument::ActionType::key)
            steps.push_back({nullptr, &action.key, 0});
        else
            steps.push_back({nullptr, nullptr, action.milliseconds});
    }
    return runSteps(steps.data(), steps.size(), optionsFor(document, options), stop, report,
        !snippetdocument::textOnly(document));
}

inline Result run(const std::wstring& source, const Options& options, const std::atomic_bool& stop,
    const Reporter& report = {}) {
    secure::Text text(source);
    return runSecure(text, options, stop, report);
}

// Compatibility entry point used by the original keyboard regression suite.
inline Result type(const std::wstring& text, DWORD delay, DWORD initialDelay, bool unicode,
    const std::atomic_bool& stop, HWND target = nullptr) {
    Options options;
    options.delay = delay;
    options.initialDelay = initialDelay;
    options.unicode = unicode;
    options.target = target;
    return run(text, options, stop);
}
} // namespace keyboard

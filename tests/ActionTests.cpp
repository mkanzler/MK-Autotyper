// Runtime APIs are substituted; this suite never injects desktop input.
#define NOMINMAX
#include "../AutoTyper/SnippetCodec.h"
#include "../AutoTyper/SnippetVariables.h"
#include <atomic>
#include <cstdio>
#include <stdexcept>

namespace fixture {
ULONGLONG now = 100, slept = 0, cancelAt = 0, moveAt = 0;
unsigned batches = 0;
bool partial = false, moved = false;
HKL layout;
std::atomic_bool* stop = nullptr;
std::vector<INPUT> accepted;

HWND WINAPI foreground() {
    return reinterpret_cast<HWND>(static_cast<INT_PTR>(moved ? 2 : 1));
}

DWORD WINAPI thread(HWND, LPDWORD process) {
    if (process)
        *process = GetCurrentProcessId() + 1;
    return 1;
}

HKL WINAPI keyboard(DWORD) {
    return layout;
}

SHORT WINAPI keyState(int) {
    return 0;
}

ULONGLONG WINAPI ticks() {
    return now;
}

void WINAPI sleep(DWORD count) {
    now += count;
    slept += count;
    if (stop && cancelAt && now >= cancelAt)
        *stop = true;
    if (moveAt && now >= moveAt)
        moved = true;
}

UINT WINAPI send(UINT count, LPINPUT inputs, int) {
    ++batches;
    const UINT sent = partial && count > 1 ? 2u : count;
    accepted.insert(accepted.end(), inputs, inputs + sent);
    return sent;
}

void reset(std::atomic_bool& cancellation) {
    now = 100;
    slept = cancelAt = moveAt = 0;
    batches = 0;
    partial = moved = false;
    accepted.clear();
    cancellation = false;
    stop = &cancellation;
}
} // namespace fixture

#define GetForegroundWindow fixture::foreground
#define GetWindowThreadProcessId fixture::thread
#define GetKeyboardLayout fixture::keyboard
#define GetAsyncKeyState fixture::keyState
#define GetKeyState fixture::keyState
#define GetTickCount64 fixture::ticks
#define Sleep fixture::sleep
#define SendInput fixture::send
#include "../AutoTyper/KeyboardInput.h"
#undef GetForegroundWindow
#undef GetWindowThreadProcessId
#undef GetKeyboardLayout
#undef GetAsyncKeyState
#undef GetKeyState
#undef GetTickCount64
#undef Sleep
#undef SendInput

void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

void metadata() {
    auto document = snippetdocument::Document::fromText(secure::Text(std::wstring(L"Hello {name}")));
    document.expandVariables = true;
    document.typing = {true, true, 23, 456};
    secure::Text encoded;
    check(
        snippetdocument::encode(document, encoded) == snippetdocument::Result::ok, "Metadata encode failed");
    snippetdocument::Document decoded;
    check(snippetdocument::decode(encoded, decoded) == snippetdocument::Result::ok &&
              decoded.expandVariables && decoded.typing.overrideGlobal && decoded.typing.unicode &&
              decoded.typing.keystrokeDelay == 23 && decoded.typing.initialDelay == 456,
        "Snippet settings or variables did not survive JSON storage");
    decoded.typing.overrideGlobal = false;
    check(snippetdocument::encode(decoded, encoded) == snippetdocument::Result::ok &&
              snippetdocument::decode(encoded, document) == snippetdocument::Result::ok &&
              !document.typing.overrideGlobal && document.typing.keystrokeDelay == 23,
        "Disabled override lost its saved custom settings");
    for (const auto* invalid :
        {LR"({"format":"mk-autotyper-snippet","version":1,"actions":[],"typing":{"overrideGlobal":true,"unicode":false,"keystrokeDelay":1,"initialDelay":0}})",
            LR"({"format":"mk-autotyper-snippet","version":1,"actions":[],"typing":{"overrideGlobal":true,"unicode":false,"keystrokeDelay":10,"initialDelay":60001}})",
            LR"({"format":"mk-autotyper-snippet","version":1,"actions":[],"expandVariables":true,"expandVariables":false})",
            LR"({"format":"mk-autotyper-snippet","version":1,"actions":[],"typing":{"overrideGlobal":false,"overrideGlobal":true}})"}) {
        check(snippetdocument::decode(secure::Text(std::wstring(invalid)), decoded) !=
                  snippetdocument::Result::ok,
            "Invalid or duplicate snippet settings accepted");
    }
}

void variables() {
    using snippetvariables::Result;
    auto document = snippetdocument::Document::fromText(
        secure::Text(std::wstring(L"Hello {name}: {date} {time} {{literal}}")));
    document.actions.push_back(
        snippetdocument::Action::pressKey({snippetdocument::KeyEncoding::virtualKey, VK_TAB, 0, false}));
    document.actions.push_back(
        snippetdocument::Action::typeText(secure::Text(std::wstring(L"Again {name}"))));
    std::vector<snippetvariables::Field> fields;
    check(snippetvariables::collect(document, fields) == Result::ok && fields.empty(),
        "Literal braces expanded without opt-in");
    document.expandVariables = true;
    check(snippetvariables::collect(document, fields) == Result::ok && fields.size() == 1 &&
              snippetvariables::equal(fields[0].name, L"name"),
        "Repeated fields requested twice or built-ins requested input");
    fields[0].value = secure::Text(std::wstring(L"{time}@value"));
    SYSTEMTIME time{};
    time.wYear = 2026;
    time.wMonth = 10;
    time.wDay = 26;
    time.wHour = 14;
    time.wMinute = 3;
    time.wSecond = 4;
    check(snippetvariables::resolve(document, fields, time) == Result::ok && !document.expandVariables &&
              secure::equal(document.actions[0].text,
                  secure::Text(std::wstring(L"Hello {time}@value: 2026-10-26 14:03:04 {literal}"))) &&
              document.actions[1].type == snippetdocument::ActionType::key &&
              secure::equal(document.actions[2].text, secure::Text(std::wstring(L"Again {time}@value"))),
        "Variable expansion changed action types, recursively expanded values or used inconsistent values");
    for (const auto* invalid : {L"{", L"}", L"{}", L"{bad name}", L"{bad\nname}"}) {
        auto broken = snippetdocument::Document::fromText(secure::Text(std::wstring(invalid)));
        broken.expandVariables = true;
        check(snippetvariables::collect(broken, fields) == Result::invalid, "Malformed variable accepted");
    }
    auto bounded = snippetdocument::Document::fromText(secure::Text(std::wstring(
        L"{value}{value}{value}{value}{value}{value}{value}{value}{value}{value}{value}{value}{value}{value}{value}{value}{value}")));
    bounded.expandVariables = true;
    check(snippetvariables::collect(bounded, fields) == Result::ok && fields.size() == 1,
        "Bounded fixture failed");
    fields[0].value = secure::Text(snippetvariables::MaximumValue);
    for (size_t i = 0; i < snippetvariables::MaximumValue; ++i)
        fields[0].value.writable()[i] = L'x';
    fields[0].value.length(snippetvariables::MaximumValue);
    check(snippetvariables::resolve(bounded, fields, time) == Result::tooLarge && bounded.expandVariables,
        "Oversized expansion succeeded or partially mutated the source");
}

void runtime() {
    fixture::layout = LoadKeyboardLayoutW(L"00000407", 0);
    check(fixture::layout != nullptr, "German layout unavailable");
    auto at = keyboard::character(L'@', fixture::layout, false, false);
    check(at.size() == 6, "German @ did not produce one Ctrl+Alt+Q chord");
    const UINT expected[] = {VK_LCONTROL, VK_LMENU, 'Q', 'Q', VK_LMENU, VK_LCONTROL};
    for (size_t i = 0; i < at.size(); ++i) {
        const UINT scan = at[i].ki.wScan | ((at[i].ki.dwFlags & KEYEVENTF_EXTENDEDKEY) ? 0xe000 : 0);
        check(MapVirtualKeyExW(scan, MAPVK_VSC_TO_VK_EX, fixture::layout) == expected[i] &&
                  bool(at[i].ki.dwFlags & KEYEVENTF_KEYUP) == (i >= 3),
            "German @ used AltGr, wrong modifier order or unbalanced events");
    }
    std::atomic_bool stop{false};
    fixture::reset(stop);
    keyboard::Options options;
    options.initialDelay = 1000;
    options.delay = 100;
    auto document = snippetdocument::Document::fromText(secure::Text(std::wstring(L"a")));
    document.typing = {true, true, 12, 50};
    document.actions.push_back(snippetdocument::Action::pressKey({snippetdocument::KeyEncoding::virtualKey,
        'Q', snippetdocument::control | snippetdocument::alt, false}));
    document.actions.push_back(snippetdocument::Action::delay(75));
    document.actions.push_back(snippetdocument::Action::typeText(secure::Text(std::wstring(L"b"))));
    keyboard::Progress progress;
    check(keyboard::runDocument(document, options, stop,
              [&](const keyboard::Progress& p) { progress = p; }) == keyboard::Result::completed &&
              fixture::slept == 137 && fixture::batches == 3 && progress.sequence &&
              progress.actionIndex == 4 && document.actions[0].text[0] == 0 &&
              document.actions[3].text[0] == 0 && options.initialDelay == 1000,
        "Sequence ordering/timing, override isolation, progress or plaintext wiping failed");
    check((fixture::accepted.front().ki.dwFlags & KEYEVENTF_UNICODE) != 0 &&
              fixture::accepted[2].ki.wVk == VK_LCONTROL && fixture::accepted[3].ki.wVk == VK_LMENU,
        "Input-mode override affected key chords or was not applied to text");
    fixture::reset(stop);
    options.initialDelay = 0;
    auto conflicting = snippetdocument::Document::fromText(secure::Text(std::wstring(L"never send")));
    conflicting.actions.push_back(snippetdocument::Action::pressKey({snippetdocument::KeyEncoding::virtualKey,
        'V', snippetdocument::control | snippetdocument::alt, false}));
    check(keyboard::runDocument(conflicting, options, stop) == keyboard::Result::shortcutConflict &&
              fixture::batches == 0,
        "Global-shortcut collision was not rejected before typing");
    fixture::reset(stop);
    fixture::cancelAt = 130;
    snippetdocument::Document pause;
    pause.actions.push_back(snippetdocument::Action::delay(snippetdocument::MaximumDelayMilliseconds));
    check(keyboard::runDocument(pause, options, stop) == keyboard::Result::cancelled &&
              fixture::slept == 30 && fixture::batches == 0,
        "Long pause ignored cancellation");
    fixture::reset(stop);
    fixture::moveAt = 120;
    auto moved = snippetdocument::clone(pause);
    moved.actions.push_back(snippetdocument::Action::typeText(secure::Text(std::wstring(L"never send"))));
    check(keyboard::runDocument(moved, options, stop) == keyboard::Result::cancelled && fixture::batches == 0,
        "Focus change during pause sent text to another app");
    fixture::reset(stop);
    fixture::partial = true;
    snippetdocument::Document partial;
    partial.actions.push_back(snippetdocument::Action::pressKey({snippetdocument::KeyEncoding::virtualKey,
        'Q', snippetdocument::control | snippetdocument::alt, false}));
    check(keyboard::runDocument(partial, options, stop) == keyboard::Result::inputBlocked &&
              fixture::batches == 3 && fixture::accepted[2].ki.wVk == VK_LMENU &&
              (fixture::accepted[2].ki.dwFlags & KEYEVENTF_KEYUP) &&
              fixture::accepted[3].ki.wVk == VK_LCONTROL &&
              (fixture::accepted[3].ki.dwFlags & KEYEVENTF_KEYUP),
        "Partial chord left modifier keys pressed");
    auto windows = keyboard::chord(
        {snippetdocument::KeyEncoding::virtualKey, 'R', snippetdocument::windows, false}, fixture::layout);
    check(windows.size() == 4 && windows[0].ki.wVk == VK_LWIN && windows[3].ki.wVk == VK_LWIN &&
              (windows[3].ki.dwFlags & KEYEVENTF_KEYUP),
        "Windows modifier was not balanced");
}

int main() {
    try {
        metadata();
        variables();
        runtime();
        std::puts(
            "PASS: snippet settings/JSON, opt-in bounded variables, German Ctrl+Alt+Q, action timing/cancellation, shortcut collisions and partial-key cleanup.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}

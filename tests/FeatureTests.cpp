#define NOMINMAX
#include "../AutoTyper/TypingSession.h"
#include "../AutoTyper/Hotkey.h"
#include "../AutoTyper/WindowPlacement.h"
#include "../AutoTyper/SnippetStore.h"
#include "../AutoTyper/SessionPanel.h"
#include <cstdio>
#include <map>
#include <stdexcept>
#include <thread>

void check(bool value, const char* reason) {
    if (!value)
        throw std::runtime_error(reason);
}

std::map<int, std::pair<UINT, UINT>> registered;
bool rejectKey = false;

BOOL WINAPI fakeRegister(HWND, int id, UINT modifiers, UINT key) {
    check((modifiers & MOD_NOREPEAT) != 0, "Repeated hotkey events allowed");
    if (rejectKey)
        return FALSE;
    registered[id] = {modifiers, key};
    return TRUE;
}

BOOL WINAPI fakeUnregister(HWND, int id) {
    registered.erase(id);
    return TRUE;
}

keyboard::Result waitRunner(
    secure::Text&, const keyboard::Options&, const std::atomic_bool& stop, const keyboard::Reporter& report) {
    keyboard::Progress p;
    p.total = 20;
    p.remaining = 1000;
    report(p);
    while (!stop)
        Sleep(1);
    return keyboard::Result::cancelled;
}

keyboard::Result completedRunner(
    secure::Text&, const keyboard::Options&, const std::atomic_bool&, const keyboard::Reporter& report) {
    keyboard::Progress p;
    p.stage = keyboard::Stage::typing;
    p.total = p.completed = 3;
    report(p);
    return keyboard::Result::completed;
}

void keyboardAndSession() {
    bool rejected = false;
    try {
        secure::Text oversized((std::numeric_limits<size_t>::max)());
    } catch (const std::length_error&) {
        rejected = true;
    }
    check(rejected, "Secret allocation size overflow was accepted");
    secure::Text bounded(2);
    rejected = false;
    try {
        bounded.length(3);
    } catch (const std::length_error&) {
        rejected = true;
    }
    check(rejected && bounded.empty(), "Secret length escaped its allocation");
    secure::Text privateLines(L"a\n\nb\r\nc\rd\x01", 11);
    keyboard::normalizePrivate(privateLines);
    secure::Text expectedLines(L"a\r\rb\rc\rd", 8);
    check(secure::equal(privateLines, expectedLines), "In-place normalization lost consecutive line breaks");
    check(keyboard::prepare(L"a\r\nb\nc\rd\x01") == L"a\rb\rc\rd",
        "Line ending normalization or control filtering failed");
    check(keyboard::characterCount(L"a\xd83d\xde00\u00e4") == 3, "Surrogate pair counted twice");
    for (const auto* name : {L"00000409", L"00000809", L"00000407"}) {
        const HKL layout = LoadKeyboardLayoutW(name, 0);
        check(layout != nullptr, "Keyboard layout unavailable");
        for (bool unicode : {false, true}) {
            const auto events = keyboard::character(L'\r', layout, unicode, false, true);
            check(events.size() == 4, "Shift+Enter event count incorrect");
            check(events[0].ki.wScan == keyboard::key(VK_LSHIFT, layout).ki.wScan &&
                      !(events[0].ki.dwFlags & KEYEVENTF_KEYUP) && (events[3].ki.dwFlags & KEYEVENTF_KEYUP),
                "Shift not balanced");
            check(events[1].ki.wScan == keyboard::key(VK_RETURN, layout).ki.wScan &&
                      (events[1].ki.dwFlags & KEYEVENTF_SCANCODE),
                "Enter not a physical key");
        }
    }
    typing_session::Session session;
    keyboard::Options options;
    options.triggerKey = 'E';
    options.hotkeyModifiers = MOD_CONTROL | MOD_ALT;
    const auto euro = keyboard::events(L'\u20ac', LoadKeyboardLayoutW(L"00000407", 0), options, false);
    check(
        (euro[0].ki.dwFlags & KEYEVENTF_UNICODE) != 0, "AltGr character can trigger the app's stop shortcut");
    options.triggerKey = 'V';
    options.snippetHotkeyKey = 'E';
    const auto searchEuro = keyboard::events(L'\u20ac', LoadKeyboardLayoutW(L"00000407", 0), options, false);
    check((searchEuro[0].ki.dwFlags & KEYEVENTF_UNICODE) != 0,
        "Snippet hotkey can consume an injected AltGr character");
    options = keyboard::Options{};
    check(session.start(L"test", options, waitRunner), "Session start failed");
    check(!session.start(L"overlap", options, waitRunner), "Concurrent session allowed");
    session.requestStop();
    session.join();
    check(session.snapshot().finished && session.snapshot().result == keyboard::Result::cancelled,
        "Stop did not finish session");
    check(session.start(L"abc", options, completedRunner), "Session could not restart");
    session.join();
    check(session.snapshot().result == keyboard::Result::completed &&
              session.snapshot().progress.completed == 3,
        "Progress not retained");
    options.initialDelay = 60000;
    check(session.start(L"must not be typed", options), "Countdown session failed");
    Sleep(25);
    const auto before = GetTickCount64();
    session.requestStop();
    session.join();
    check(GetTickCount64() - before < 1000 && session.snapshot().result == keyboard::Result::cancelled,
        "Countdown cancellation blocked");
    typing_session::Snapshot progress;
    progress.progress.stage = keyboard::Stage::typing;
    progress.progress.completed = 25;
    progress.progress.total = 100;
    progress.progress.elapsed = 1000;
    check(sessionview::summary(progress).find(L"3s") != std::wstring::npos, "Remaining time estimate wrong");
}

void hotkeyAndPlacement() {
    {
        hotkey::Registration value(fakeRegister, fakeUnregister);
        const HWND window = reinterpret_cast<HWND>(1);
        check(value.replace(window, MOD_CONTROL | MOD_ALT, 'V'), "Initial shortcut registration failed");
        rejectKey = true;
        check(!value.replace(window, MOD_CONTROL, 'X') && value.matches(1) && registered.size() == 1,
            "Conflict lost working shortcut");
        rejectKey = false;
        check(value.replace(window, MOD_CONTROL, VK_F8) && value.matches(2) && registered.size() == 1,
            "Replacement leaked registration");
        hotkey::Registration snippets(fakeRegister, fakeUnregister, 3);
        check(snippets.replace(window, MOD_CONTROL | MOD_ALT, 'P') && snippets.matches(3) &&
                  !value.matches(3) && registered.size() == 2,
            "Snippet registration overwrote the start/stop registration");
        check(snippets.replace(window, MOD_CONTROL, VK_F9) && snippets.matches(4) && registered.size() == 2,
            "Replacing snippet shortcut disturbed start/stop");
    }
    check(registered.empty(), "Hotkey registration leaked on destruction");
    check(!hotkey::valid(0, 'A') && !hotkey::valid(MOD_SHIFT, 'A') && hotkey::valid(0, VK_PAUSE),
        "Unsafe shortcut validation");
    check(!hotkey::valid(MOD_CONTROL, VK_SHIFT) && !hotkey::valid(MOD_ALT, VK_PACKET),
        "Nonphysical or modifier key accepted");
    placement::Saved saved;
    saved.bounds = {100, 100, 1100, 800};
    saved.workOrigin = {0, 40};
    RECT fit = placement::fit(saved, 144, {0, 0, 1920, 1080});
    check(fit.right - fit.left == 1500 && fit.bottom - fit.top == 1050 && fit.top >= 0 && fit.bottom <= 1080,
        "DPI placement scaling failed");
    saved.bounds = {10000, -300, 11000, 400};
    saved.workOrigin = {0, 0};
    fit = placement::fit(saved, 96, {0, 0, 1280, 720});
    check(fit.left >= 0 && fit.top >= 0 && fit.right <= 1280 && fit.bottom <= 720,
        "Disconnected monitor placement not visible");
}

void snippetFiles() {
    wchar_t executable[32768];
    GetModuleFileNameW(nullptr, executable, 32768);
    const std::wstring root =
        std::wstring(executable) + L"." + std::to_wstring(GetCurrentProcessId()) + L".data";
    snippets::Store store(root);
    std::vector<std::wstring> names;
    check(store.list(names) && names.empty(), "First-run collection failed");
    check(snippets::nameIssue(L"") == snippets::NameIssue::empty &&
              snippets::validName(std::wstring(80, L'x')) &&
              snippets::nameIssue(std::wstring(81, L'x')) == snippets::NameIssue::tooLong,
        "Name length boundary failed");
    for (const auto* name : {L"<>:\"/\\|?*", L"../escape", L"CON", L"NUL.txt", L"COM\u00b9", L"Name.",
             L"Name ", L"\u00c4pfel \xd83d\xde00 \u4e2d\u6587"})
        check(snippets::validName(name), "Display name retained filename restrictions");
    for (wchar_t c = 0; c < 32; ++c)
        check(snippets::nameIssue(std::wstring(L"Before") + c + L"After") ==
                  snippets::NameIssue::invalidCharacter,
            "Name control character accepted");
    for (const auto& name : {std::wstring(1, 0xd800), std::wstring(1, 0xdc00), std::wstring(1, 0x7f)})
        check(!snippets::validName(name), "Invalid name Unicode/control accepted");
    check(snippets::sameName(L"\u00c4PFEL", L"\u00e4pfel"), "German name comparison failed");
    const std::wstring special = L"<>:\"/\\|?* \xd83d\xde00";
    const std::wstring text = L"Hello \u00e4\u00f6\u00fc \u00df \u20ac\r\nsecond line";
    check(store.save(special, text, false) == snippets::Result::ok, "Symbol-name collection save failed");
    check(store.save(L"../escape", L"Safe inside JSON", false) == snippets::Result::ok,
        "Path-like display name failed");
    std::wstring loaded;
    check(store.load(special, loaded) == snippets::Result::ok && loaded == text, "Unicode roundtrip failed");
    check(store.save(special, L"overwrite", false) == snippets::Result::exists &&
              store.load(special, loaded) == snippets::Result::ok && loaded == text,
        "Collision damaged collection");
    check(store.save(special, L"Updated", true) == snippets::Result::ok, "Explicit update failed");
    check(store.save(L"Another", L"Other", false) == snippets::Result::ok, "Second entry save failed");
    check(store.rename(special, L"Another") == snippets::Result::exists, "Rename overwrote another entry");
    check(store.rename(special, L"Renamed: / ? *") == snippets::Result::ok &&
              store.load(L"Renamed: / ? *", loaded) == snippets::Result::ok && loaded == L"Updated",
        "Rename lost content");
    check(store.save(L"Empty", L"", false) == snippets::Result::ok &&
              store.load(L"Empty", loaded) == snippets::Result::ok && loaded.empty(),
        "Empty snippet failed");
    snippets::Result firstResult = snippets::Result::ioError, secondResult = snippets::Result::ioError;
    std::thread first([&] { firstResult = store.save(L"Writer A", L"A", false); });
    std::thread second([&] { secondResult = store.save(L"Writer B", L"B", false); });
    first.join();
    second.join();
    check(firstResult == snippets::Result::ok && secondResult == snippets::Result::ok,
        "Concurrent collection writer failed");
    check(store.load(L"Writer A", loaded) == snippets::Result::ok && loaded == L"A" &&
              store.load(L"Writer B", loaded) == snippets::Result::ok && loaded == L"B",
        "Concurrent update lost an entry");
    check(store.list(names) && names.size() == 6, "Collection enumeration failed");
    for (const auto& name : names)
        check(store.remove(name) == snippets::Result::ok, "Collection entry removal failed");
    check(store.list(names) && names.empty(), "Deleted collection entries remained");
    {
        snippets::File file(
            CreateFileW(store.path().c_str(), GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING, 0, nullptr));
        const BYTE invalid[] = {0xff, 0xfe};
        DWORD written = 0;
        check(file.handle != INVALID_HANDLE_VALUE && WriteFile(file.handle, invalid, 2, &written, nullptr),
            "Invalid UTF-8 fixture failed");
    }
    loaded = L"stale";
    check(store.load(L"Bad", loaded) == snippets::Result::invalidText && loaded.empty(),
        "Invalid UTF-8 retained output");
    check(store.save(L"New", L"text", false) == snippets::Result::invalidText,
        "Invalid collection was overwritten");
    {
        snippets::File file(
            CreateFileW(store.path().c_str(), GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING, 0, nullptr));
        LARGE_INTEGER size;
        size.QuadPart = snippets::MaximumBytes + 1;
        check(file.handle != INVALID_HANDLE_VALUE &&
                  SetFilePointerEx(file.handle, size, nullptr, FILE_BEGIN) && SetEndOfFile(file.handle),
            "Large collection fixture failed");
    }
    check(store.load(L"Large", loaded) == snippets::Result::tooLarge, "Oversized collection accepted");
    check(DeleteFileW(store.path().c_str()) && RemoveDirectoryW(root.c_str()),
        "Collection temporary files leaked");
}

int main() {
    try {
        keyboardAndSession();
        hotkeyAndPlacement();
        snippetFiles();
        std::puts(
            "PASS: normalized line breaks, balanced Shift+Enter, cancellation/restart, progress/ETA, conflict-safe shortcuts, DPI/monitor placement and atomic UTF-8 snippet CRUD.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}

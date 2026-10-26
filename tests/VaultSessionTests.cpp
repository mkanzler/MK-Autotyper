#define NOMINMAX
#include "../AutoTyper/EncryptedSnippets.h"
#include <cstdio>
#include <stdexcept>

namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

encrypted::Bytes readFile(const std::wstring& path) {
    snippets::File file(
        CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
    LARGE_INTEGER size{};
    check(file.handle != INVALID_HANDLE_VALUE && GetFileSizeEx(file.handle, &size), "Read fixture failed");
    encrypted::Bytes bytes(static_cast<size_t>(size.QuadPart));
    DWORD received = 0;
    check(ReadFile(file.handle, bytes.data(), static_cast<DWORD>(bytes.size()), &received, nullptr) &&
              received == bytes.size(),
        "Read bytes failed");
    return bytes;
}

void writeFile(const std::wstring& path, const encrypted::Bytes& bytes) {
    snippets::File file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr));
    DWORD written = 0;
    check(file.handle != INVALID_HANDLE_VALUE &&
              WriteFile(file.handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
              written == bytes.size(),
        "Write fixture failed");
}
} // namespace

int main() {
    wchar_t executable[32768];
    GetModuleFileNameW(nullptr, executable, 32768);
    const std::wstring root =
        std::wstring(executable) + L"." + std::to_wstring(GetCurrentProcessId()) + L".data";
    try {
        using encrypted::Result;
        encrypted::Store store(root);
        snippets::Store plain(root);
        const std::wstring path = root + L"\\Encrypted.dat";
        secure::Text password(std::wstring(L"Session fixture password 123"));
        secure::Text wrong(std::wstring(L"Incorrect fixture password"));
        secure::Text next(std::wstring(L"Replacement fixture password 456"));
        secure::Text message(std::wstring(L"SESSION-SENTINEL \u00e4\u00f6\u00fc\u00df \u20ac")), loaded,
            empty;
        encrypted::VaultSession session;
        encrypted::Credentials cached(empty, &session);
        check(!session.unlocked(), "A new process session was unlocked");
        check(store.save(L"First", message, password, false) == Result::ok, "Save fixture failed");
        check(store.load(L"First", cached, loaded) == Result::authentication && loaded.empty(),
            "Locked session bypassed authentication");
        check(store.unlock(wrong, session) == Result::authentication && !session.unlocked(),
            "Wrong password unlocked a session");
        check(store.unlock(password, session) == Result::ok && session.unlocked(), "Unlock failed");
        password.clear();
        check(store.load(L"First", cached, loaded) == Result::ok && secure::equal(message, loaded),
            "Session depended on password lifetime");
        check(store.save(L"Second", message, cached, false) == Result::ok &&
                  store.rename(L"Second", L"Renamed", cached) == Result::ok,
            "Session save/rename failed");
        check(store.convert(L"Renamed", message, cached, false, plain) == Result::ok &&
                  plain.load(L"Renamed", loaded) == snippets::Result::ok && secure::equal(message, loaded),
            "Session decrypt conversion failed");
        check(store.convert(L"Renamed", message, cached, true, plain) == Result::ok &&
                  store.remove(L"Renamed", cached) == Result::ok,
            "Session encrypt conversion/delete failed");
        const auto valid = readFile(path);
        auto corrupt = valid;
        corrupt.back() ^= 1;
        writeFile(path, corrupt);
        loaded = secure::Text(std::wstring(L"old plaintext"));
        check(store.load(L"First", cached, loaded) == Result::authentication && loaded.empty() &&
                  !session.unlocked() && readFile(path) == corrupt,
            "Cached key bypassed tampering or failed to lock");
        writeFile(path, valid);
        password = secure::Text(std::wstring(L"Session fixture password 123"));
        check(store.unlock(password, session) == Result::ok, "Re-unlock failed");
        check(store.changePassword(wrong, next) == Result::authentication && session.unlocked() &&
                  store.load(L"First", cached, loaded) == Result::ok,
            "Failed password change damaged the session");
        check(store.changePassword(password, next) == Result::ok, "External password change failed");
        check(store.load(L"First", cached, loaded) == Result::authentication && !session.unlocked() &&
                  loaded.empty(),
            "Old session survived a password change");
        check(store.unlock(next, session) == Result::ok && store.load(L"First", cached, loaded) == Result::ok,
            "Changed password could not unlock");
        session.lock();
        check(!session.unlocked() && store.load(L"First", cached, loaded) == Result::authentication &&
                  loaded.empty(),
            "Manual lock retained access or plaintext");
        encrypted::VaultSession independent;
        check(store.load(L"First", encrypted::Credentials(empty, &independent), loaded) ==
                  Result::authentication,
            "A separate session inherited an unlock");
        check(store.unlock(next, session) == Result::ok, "Final unlock failed");
        const auto bytes = readFile(path);
        const BYTE* secret = reinterpret_cast<const BYTE*>(message.data());
        check(std::search(bytes.begin(), bytes.end(), secret, secret + message.size() * sizeof(wchar_t)) ==
                  bytes.end(),
            "Session mode wrote plaintext to the vault");
        check(DeleteFileW(path.c_str()) != FALSE, "Delete fixture failed");
        check(
            store.load(L"First", cached, loaded) == Result::missing && !session.unlocked() && loaded.empty(),
            "Missing vault did not lock the session");
        check(DeleteFileW(plain.path().c_str()) && RemoveDirectoryW(root.c_str()),
            "Session fixture cleanup failed");
        std::puts(
            "PASS: session password lifetime, protected key reuse, all vault operations, tamper/password-change invalidation, independent sessions and locking.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}

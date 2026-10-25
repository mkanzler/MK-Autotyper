#define NOMINMAX
#include "../AutoTyper/EncryptedSnippets.h"
#include <cstdio>
#include <stdexcept>

void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

encrypted::Bytes read(const std::wstring& path) {
    snippets::File file(
        CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
    LARGE_INTEGER size{};
    check(file.handle != INVALID_HANDLE_VALUE && GetFileSizeEx(file.handle, &size), "Read fixture failed");
    encrypted::Bytes result(static_cast<size_t>(size.QuadPart));
    DWORD received = 0;
    check(ReadFile(file.handle, result.data(), static_cast<DWORD>(result.size()), &received, nullptr) &&
              received == result.size(),
        "Read fixture bytes failed");
    return result;
}

void write(const std::wstring& path, const encrypted::Bytes& data) {
    snippets::File file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr));
    DWORD written = 0;
    check(file.handle != INVALID_HANDLE_VALUE &&
              WriteFile(file.handle, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) &&
              written == data.size(),
        "Write fixture failed");
}

encrypted::Vault metadata(const encrypted::Bytes& data) {
    encrypted::Vault vault;
    encrypted::Reader reader(data);
    BYTE magic[8];
    ULONG rounds = 0, count = 0;
    check(reader.take(magic, 8) && reader.number(rounds) &&
              reader.take(vault.salt.data(), vault.salt.size()) && reader.record(vault.seal) &&
              reader.number(count),
        "Fixture metadata parsing failed");
    for (ULONG i = 0; i < count; ++i) {
        encrypted::Record record;
        check(reader.record(record), "Fixture record parsing failed");
        vault.records.push_back(std::move(record));
    }
    return vault;
}

encrypted::Bytes encodeVault(encrypted::Vault& vault, const encrypted::Key& key) {
    check(encrypted::seal(vault, key), "Fixture authentication failed");
    encrypted::Bytes data;
    encrypted::append(data, "MKATVAUL", 8);
    encrypted::number(data, encrypted::Iterations);
    encrypted::append(data, vault.salt.data(), vault.salt.size());
    encrypted::writeRecord(data, vault.seal);
    const auto records = encrypted::recordsBytes(vault);
    encrypted::append(data, records.data(), records.size());
    return data;
}

int main() {
    wchar_t executable[32768];
    GetModuleFileNameW(nullptr, executable, 32768);
    const std::wstring root =
        std::wstring(executable) + L"." + std::to_wstring(GetCurrentProcessId()) + L".data";
    try {
        using encrypted::Result;
        encrypted::Store store(root);
        const std::wstring path = root + L"\\Encrypted.dat";
        secure::Text password(L"Correct horse \u00e4 123", 19), wrong(L"Incorrect password", 18),
            changed(L"Replacement \u00d6 45678", 19);
        const std::wstring message = L"SENSITIVE-SENTINEL \u00e4\u00f6\u00fc\u00df \u20ac\r\nsecond line";
        secure::Text text(message), loaded;
        secure::Text weak(L"short", 5), nulText(L"a\0b", 3);
        loaded = secure::Text(message);
        check(store.load(L"Missing", password, loaded) == Result::missing && loaded.empty(),
            "Loading a missing vault did not clear the output");
        check(store.rename(L"Missing", L"Renamed", password) == Result::missing,
            "Renaming created a missing vault");
        check(store.remove(L"Missing", password) == Result::missing, "Deleting created a missing vault");
        check(store.changePassword(password, changed) == Result::missing,
            "Password rotation created a missing vault");
        check(store.convert(L"Missing", text, password, false, snippets::Store(root)) == Result::missing &&
                  !store.exists(),
            "Decrypting created a missing vault or plaintext destination");
        check(store.save(L"Weak", text, weak, false) == Result::weakPassword && !store.exists(),
            "Storage created a vault with a weak master password");
        check(store.save(L"Invalid", nulText, password, false) == Result::invalidText && !store.exists(),
            "Embedded null created an unreadable encrypted record");
        encrypted::Bytes truncatedRecord;
        encrypted::number(truncatedRecord, 0);
        truncatedRecord.resize(4 + 12 + 16, 0);
        encrypted::number(truncatedRecord, 2 * 1024 * 1024);
        encrypted::Reader truncatedReader(truncatedRecord);
        encrypted::Record truncatedOutput;
        check(!truncatedReader.record(truncatedOutput) && truncatedOutput.cipher.empty(),
            "Truncated record allocated its claimed ciphertext size");
        check(store.save(L"First", text, password, false) == Result::ok, "Encrypted save failed");
        auto initial = read(path);
        const auto* secret = reinterpret_cast<const BYTE*>(message.data());
        check(std::search(initial.begin(), initial.end(), secret,
                  secret + message.size() * sizeof(wchar_t)) == initial.end(),
            "Plaintext found in vault");
        check(store.load(L"First", wrong, loaded) == Result::authentication && loaded.empty(),
            "Wrong password exposed plaintext");
        check(store.load(L"First", password, loaded) == Result::ok && secure::equal(text, loaded),
            "Unicode roundtrip failed");
        loaded.clear();
        check(store.save(L"Second", text, password, false) == Result::ok, "Second encrypted save failed");
        const auto nonces = metadata(read(path));
        check(nonces.records[0].nonce != nonces.records[1].nonce, "Two records shared a GCM nonce");
        check(store.save(L"First", text, password, false) == Result::exists,
            "Encrypted collision overwrote data");
        check(store.save(L"First", text, password, true) == Result::ok && read(path) != initial,
            "Repeated encryption reused ciphertext");
        check(metadata(read(path)).records[0].nonce != nonces.records[0].nonce,
            "Updating a record reused its GCM nonce");
        check(store.rename(L"First", L"Renamed", password) == Result::ok, "Encrypted rename failed");
        snippetdocument::Document actions;
        actions.actions.push_back(snippetdocument::Action::typeText(secure::Text(message)));
        actions.actions.push_back(snippetdocument::Action::pressKey(
            {snippetdocument::KeyEncoding::virtualKey, 'R', snippetdocument::windows, false}));
        actions.actions.push_back(snippetdocument::Action::delay(500));
        actions.actions.push_back(snippetdocument::Action::pressKey({snippetdocument::KeyEncoding::scanCode,
            0x1c, snippetdocument::control | snippetdocument::alt, true}));
        check(store.save(L"Actions", actions, password, false) == Result::ok, "Encrypted action save failed");
        std::vector<std::wstring> names;
        check(store.list(names) == Result::ok && names.size() == 3, "Encrypted enumeration failed");
        const auto before = read(path);
        {
            snippets::File lock(
                CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
            check(lock.handle != INVALID_HANDLE_VALUE, "Failure fixture lock failed");
            check(store.changePassword(password, changed) == Result::ioError,
                "Blocked password rotation reported success");
        }
        check(read(path) == before && store.load(L"Renamed", password, loaded) == Result::ok,
            "Failed rotation damaged original data");
        loaded.clear();
        check(store.changePassword(wrong, changed) == Result::authentication && read(path) == before,
            "Wrong password changed the vault");
        check(store.changePassword(password, weak) == Result::weakPassword && read(path) == before,
            "Password rotation accepted a weak replacement");
        check(store.changePassword(password, changed) == Result::ok, "Master password rotation failed");
        check(
            metadata(read(path)).salt != metadata(before).salt, "Password rotation reused the previous salt");
        check(store.load(L"Renamed", password, loaded) == Result::authentication && loaded.empty(),
            "Old master password still accepted");
        for (const auto* name : {L"Renamed", L"Second"})
            check(store.load(name, changed, loaded) == Result::ok && secure::equal(loaded, text),
                "Rotation did not update every snippet");
        loaded.clear();
        snippetdocument::Document loadedActions;
        secure::Text expectedActions, actualActions;
        check(snippetdocument::encode(actions, expectedActions) == snippetdocument::Result::ok &&
                  store.load(L"Actions", changed, loadedActions) == Result::ok &&
                  snippetdocument::encode(loadedActions, actualActions) == snippetdocument::Result::ok &&
                  secure::equal(expectedActions, actualActions),
            "Password rotation changed action data");
        check(store.load(L"Actions", wrong, loadedActions) == Result::authentication &&
                  loadedActions.actions.empty(),
            "Wrong password returned stale action data");
        loaded = secure::Text(message);
        check(store.load(L"Actions", changed, loaded) == Result::unsupportedActions && loaded.empty(),
            "Encrypted action sequence was flattened into editor text");
        snippets::Store actionPlain(root);
        check(store.convert(L"Actions", actions, changed, false, actionPlain) == Result::ok &&
                  actionPlain.load(L"Actions", loadedActions) == snippets::Result::ok &&
                  snippetdocument::encode(loadedActions, actualActions) == snippetdocument::Result::ok &&
                  secure::equal(expectedActions, actualActions),
            "Decrypting lost actions");
        check(store.convert(L"Actions", loadedActions, changed, true, actionPlain) == Result::ok &&
                  actionPlain.load(L"Actions", loaded) == snippets::Result::missing &&
                  store.rename(L"Actions", L"Renamed actions", changed) == Result::ok &&
                  store.load(L"Renamed actions", changed, loadedActions) == Result::ok &&
                  snippetdocument::encode(loadedActions, actualActions) == snippetdocument::Result::ok &&
                  secure::equal(expectedActions, actualActions),
            "Encrypting or renaming lost actions");
        const auto beforeInvalidDocument = read(path);
        for (const auto* payload :
            {L"{", L"{\"format\":\"mk-autotyper-snippet\",\"version\":2,\"actions\":[]}"}) {
            auto fixture = metadata(beforeInvalidDocument);
            encrypted::Key fixtureKey;
            check(fixtureKey.derive(changed, fixture.salt), "Authenticated document fixture key failed");
            for (auto& record : fixture.records)
                if (record.name == L"Renamed actions")
                    check(
                        encrypted::encrypt(fixture, fixtureKey, secure::Text(std::wstring(payload)), record),
                        "Authenticated document fixture encryption failed");
            write(path, encodeVault(fixture, fixtureKey));
            const auto result = store.load(L"Renamed actions", changed, loadedActions);
            check((result == Result::invalidDocument || result == Result::unsupportedFormat) &&
                      loadedActions.actions.empty(),
                "Authenticated invalid/future document was accepted or retained plaintext");
            check(store.changePassword(changed, password) == result,
                "Password rotation accepted an invalid/future document");
            write(path, beforeInvalidDocument);
        }
        check(store.remove(L"Renamed actions", changed) == Result::ok, "Action fixture cleanup failed");
        const std::wstring symbolName = L"<>:\"/\\|?* \xd83d\xde00 \u4e2d\u6587";
        check(store.save(symbolName, text, changed, false) == Result::ok &&
                  store.load(symbolName, changed, loaded) == Result::ok && secure::equal(text, loaded) &&
                  store.rename(symbolName, L"CON: / ? * .", changed) == Result::ok &&
                  store.load(L"CON: / ? * .", changed, loaded) == Result::ok && secure::equal(text, loaded) &&
                  store.remove(L"CON: / ? * .", changed) == Result::ok,
            "Encrypted display names retained filename restrictions or lost Unicode");
        const auto valid = read(path);
        for (size_t offset : {size_t(12), size_t(32), size_t(44), size_t(64), size_t(104)}) {
            auto modified = valid;
            modified[offset] ^= 1;
            write(path, modified);
            check(store.load(L"Renamed", changed, loaded) == Result::authentication && loaded.empty(),
                "Modified salt, nonce, tag, manifest or name was accepted");
        }
        auto corrupt = valid;
        corrupt.back() ^= 1;
        write(path, corrupt);
        check(store.load(L"Renamed", changed, loaded) == Result::authentication && loaded.empty(),
            "Modified ciphertext was accepted");
        write(path, encrypted::Bytes(valid.begin(), valid.begin() + 12));
        check(store.load(L"Renamed", changed, loaded) == Result::corrupt, "Truncated vault was accepted");
        write(path, valid);
        check(store.save(L"\u00c4pfel", text, changed, false) == Result::ok, "German snippet fixture failed");
        check(store.save(L"\u00e4PFEL", text, changed, false) == Result::exists,
            "German case collision created duplicate encrypted entries");
        check(store.load(L"\u00e4PFEL", changed, loaded) == Result::ok && secure::equal(text, loaded),
            "German case lookup failed");
        loaded.clear();
        check(store.rename(L"Renamed", L"\u00e4pfel", changed) == Result::exists,
            "German case rename overwrote another snippet");
        check(store.remove(L"\u00c4PFEL", changed) == Result::ok, "German case removal failed");
        auto duplicate = metadata(read(path));
        encrypted::Key duplicateKey;
        check(duplicateKey.derive(changed, duplicate.salt), "Duplicate fixture key failed");
        for (const auto* name : {L"\u00c4pfel", L"\u00e4pfel"}) {
            encrypted::Record record;
            record.name = name;
            check(encrypted::encrypt(duplicate, duplicateKey, text, record),
                "Duplicate fixture encryption failed");
            duplicate.records.push_back(std::move(record));
        }
        const auto beforeDuplicate = read(path);
        write(path, encodeVault(duplicate, duplicateKey));
        check(store.load(L"\u00c4pfel", changed, loaded) == Result::corrupt && loaded.empty(),
            "Case-insensitive duplicate records were accepted");
        write(path, beforeDuplicate);
        secure::Text empty;
        check(store.save(L"Empty", empty, changed, false) == Result::ok &&
                  store.load(L"Empty", changed, loaded) == Result::ok && loaded.empty(),
            "Empty encrypted snippet failed");
        check(store.remove(L"Second", wrong) == Result::authentication, "Deletion bypassed master password");
        check(store.remove(L"Second", changed) == Result::ok, "Encrypted delete failed");
        snippets::Store plain(root);
        const std::wstring plainPath = plain.path();
        check(plain.save(L"Convert", message, false) == snippets::Result::ok,
            "Plain conversion fixture failed");
        const auto beforeConversion = read(path);
        check(store.convert(L"Convert", text, wrong, true, plain) == Result::authentication &&
                  read(path) == beforeConversion &&
                  GetFileAttributesW(plainPath.c_str()) != INVALID_FILE_ATTRIBUTES,
            "Wrong conversion password changed storage");
        {
            snippets::File lock(CreateFileW(
                plainPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
            check(lock.handle != INVALID_HANDLE_VALUE, "Plain source lock failed");
            check(store.convert(L"Convert", text, changed, true, plain) == Result::ioError,
                "Blocked plain removal reported success");
        }
        check(store.list(names) == Result::ok &&
                  std::find(names.begin(), names.end(), L"Convert") == names.end() &&
                  GetFileAttributesW(plainPath.c_str()) != INVALID_FILE_ATTRIBUTES,
            "Failed encryption did not roll back destination");
        check(store.convert(L"Convert", text, changed, true, plain) == Result::ok &&
                  plain.load(L"Convert", loaded) == snippets::Result::missing,
            "Encryption left a plain duplicate");
        check(store.load(L"Convert", changed, loaded) == Result::ok && secure::equal(loaded, text),
            "Converted encrypted text changed");
        loaded.clear();
        {
            snippets::File lock(
                CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
            check(lock.handle != INVALID_HANDLE_VALUE, "Vault removal lock failed");
            check(store.convert(L"Convert", text, changed, false, plain) == Result::ioError,
                "Blocked vault removal reported success");
        }
        check(plain.load(L"Convert", loaded) == snippets::Result::missing &&
                  store.load(L"Convert", changed, loaded) == Result::ok,
            "Failed decryption did not preserve source and remove plaintext destination");
        loaded.clear();
        check(plain.save(L"Convert", L"existing destination", false) == snippets::Result::ok,
            "Collision fixture failed");
        check(store.convert(L"Convert", text, changed, false, plain) == Result::exists,
            "Conversion overwrote existing plaintext");
        std::wstring plainText;
        check(
            plain.load(L"Convert", plainText) == snippets::Result::ok && plainText == L"existing destination",
            "Plain collision damaged existing content");
        check(plain.remove(L"Convert") == snippets::Result::ok, "Collision cleanup failed");
        check(store.convert(L"Convert", text, wrong, false, plain) == Result::authentication &&
                  plain.load(L"Convert", loaded) == snippets::Result::missing,
            "Wrong password wrote plaintext");
        check(store.convert(L"Convert", text, changed, false, plain) == Result::ok &&
                  plain.load(L"Convert", plainText) == snippets::Result::ok && plainText == message,
            "Decrypted conversion changed text");
        check(store.list(names) == Result::ok && names.size() == 2 &&
                  std::find(names.begin(), names.end(), L"Convert") == names.end(),
            "Conversion damaged other encrypted snippets or left a duplicate");
        check(plain.remove(L"Convert") == snippets::Result::ok, "Conversion fixture cleanup failed");
        check(DeleteFileW(path.c_str()), "Vault reset failed");
        check(plain.save(L"NewVault", message, false) == snippets::Result::ok,
            "New-vault conversion fixture failed");
        check(store.convert(L"NewVault", text, weak, true, plain) == Result::weakPassword && !store.exists(),
            "Conversion created a vault with a weak password");
        check(store.convert(L"NewVault", text, password, true, plain) == Result::ok &&
                  store.load(L"NewVault", password, loaded) == Result::ok && secure::equal(text, loaded) &&
                  plain.load(L"NewVault", loaded) == snippets::Result::missing,
            "Conversion could not securely create a new vault and remove its plaintext source");
        loaded.clear();
        check(
            DeleteFileW(path.c_str()) && DeleteFileW(plain.path().c_str()) && RemoveDirectoryW(root.c_str()),
            "Vault fixture cleanup failed or temporary files leaked");
        std::puts(
            "PASS: AES-GCM storage, passwords/tampering, random encryption, password rotation, format "
            "conversion in both directions, collisions/failure rollback, rename/delete and empty text.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}

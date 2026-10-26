#pragma once
#include "SecureMemory.h"
#include "VaultSession.h"
#include "SnippetStore.h"
#include <bcrypt.h>
#include <array>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>
#pragma comment(lib, "Bcrypt.lib")

namespace encrypted {
constexpr ULONG Iterations = 600000;
constexpr size_t MaximumVaultBytes = 64 * 1024 * 1024, MaximumRecords = 4096;
enum class Result {
    ok,
    missing,
    authentication,
    corrupt,
    exists,
    invalidName,
    tooLarge,
    ioError,
    cryptoError,
    rollbackFailed,
    weakPassword,
    invalidText,
    invalidDocument,
    unsupportedFormat,
    unsupportedActions
};

inline Result documentResult(snippetdocument::Result result) {
    switch (result) {
    case snippetdocument::Result::ok:
        return Result::ok;
    case snippetdocument::Result::tooLarge:
        return Result::tooLarge;
    case snippetdocument::Result::unsupportedVersion:
    case snippetdocument::Result::unsupportedAction:
        return Result::unsupportedFormat;
    case snippetdocument::Result::notText:
        return Result::unsupportedActions;
    default:
        return Result::invalidDocument;
    }
}

inline bool validText(const secure::Text& text) {
    for (size_t i = 0; i < text.size(); ++i)
        if (!text[i])
            return false;
    return true;
}

inline bool validNewPassword(const secure::Text& password) {
    return password.size() >= 12 && password.size() <= 1024 && validText(password);
}

using Bytes = std::vector<BYTE>;

struct Algorithm {
    BCRYPT_ALG_HANDLE handle = nullptr;

    Algorithm(const wchar_t* name, ULONG flags = 0) {
        if (BCryptOpenAlgorithmProvider(&handle, name, nullptr, flags) < 0)
            handle = nullptr;
    }

    ~Algorithm() {
        if (handle)
            BCryptCloseAlgorithmProvider(handle, 0);
    }

    Algorithm(const Algorithm&) = delete;
    Algorithm& operator=(const Algorithm&) = delete;
};

// Passwords are borrowed only for the duration of a synchronous store operation.
// An empty password may use an explicitly supplied unlocked session.
struct Credentials {
    const secure::Text& password;
    VaultSession* session;

    Credentials(const secure::Text& value, VaultSession* unlockedSession = nullptr)
        : password(value), session(unlockedSession) {}
};

inline bool deriveMaterial(
    const secure::Text& password, const std::array<BYTE, 16>& salt, secure::Buffer& material) {
    material.clear();
    if (password.empty() || password.size() > 1024)
        return false;
    Algorithm hash(BCRYPT_SHA256_ALGORITHM, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (!hash.handle)
        return false;
    secure::Buffer derived(32);
    if (BCryptDeriveKeyPBKDF2(hash.handle, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(password.data())),
            static_cast<ULONG>(password.size() * sizeof(wchar_t)), const_cast<PUCHAR>(salt.data()),
            static_cast<ULONG>(salt.size()), Iterations, derived.data(), 32, 0) < 0)
        return false;
    material = std::move(derived);
    return true;
}

struct Key {
    Algorithm aes{BCRYPT_AES_ALGORITHM};
    secure::Buffer object;
    BCRYPT_KEY_HANDLE handle = nullptr;

    ~Key() {
        if (handle)
            BCryptDestroyKey(handle);
    }

    bool initialize(const secure::Buffer& material) {
        if (!aes.handle || handle || material.size() != 32)
            return false;
        if (BCryptSetProperty(aes.handle, BCRYPT_CHAINING_MODE,
                reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                sizeof(BCRYPT_CHAIN_MODE_GCM), 0) < 0)
            return false;
        ULONG size = 0, received = 0;
        if (BCryptGetProperty(aes.handle, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size), sizeof(size),
                &received, 0) < 0)
            return false;
        object = secure::Buffer(size);
        return BCryptGenerateSymmetricKey(aes.handle, &handle, object.data(), size, material.data(), 32, 0) >=
               0;
    }

    bool derive(const secure::Text& password, const std::array<BYTE, 16>& salt) {
        secure::Buffer material;
        return deriveMaterial(password, salt, material) && initialize(material);
    }
};

struct Record {
    std::wstring name;
    std::array<BYTE, 12> nonce{};
    std::array<BYTE, 16> tag{};
    Bytes cipher;
};

struct Vault {
    std::array<BYTE, 16> salt{};
    Record seal;
    std::vector<Record> records;
};

inline bool random(BYTE* output, size_t count) {
    return BCryptGenRandom(nullptr, output, static_cast<ULONG>(count), BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0;
}

inline void append(Bytes& bytes, const void* data, size_t size) {
    if (!size)
        return;
    const auto* first = static_cast<const BYTE*>(data);
    bytes.insert(bytes.end(), first, first + size);
}

inline void number(Bytes& bytes, ULONG value) {
    for (int i = 0; i < 4; ++i)
        bytes.push_back(static_cast<BYTE>(value >> (i * 8)));
}

inline Bytes aad(const Vault& vault, const std::wstring& name) {
    Bytes result;
    append(result, "MKATVAUL", 8);
    append(result, vault.salt.data(), vault.salt.size());
    number(result, Iterations);
    append(result, name.data(), name.size() * sizeof(wchar_t));
    return result;
}

inline bool crypt(
    const Key& key, Record& record, const Bytes& auth, BYTE* input, ULONG count, BYTE* output, bool encrypt) {
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = record.nonce.data();
    info.cbNonce = static_cast<ULONG>(record.nonce.size());
    info.pbTag = record.tag.data();
    info.cbTag = static_cast<ULONG>(record.tag.size());
    info.pbAuthData = const_cast<PUCHAR>(auth.data());
    info.cbAuthData = static_cast<ULONG>(auth.size());
    ULONG written = 0;
    BYTE empty = 0;
    const NTSTATUS status = encrypt ? BCryptEncrypt(key.handle, input, count, &info, nullptr, 0,
                                          output ? output : &empty, count, &written, 0)
                                    : BCryptDecrypt(key.handle, input, count, &info, nullptr, 0,
                                          output ? output : &empty, count, &written, 0);
    return status >= 0 && written == count;
}

inline bool encrypt(const Vault& vault, const Key& key, const secure::Text& text, Record& record) {
    if (!random(record.nonce.data(), record.nonce.size()))
        return false;
    record.cipher.resize(text.size() * sizeof(wchar_t));
    return crypt(key, record, aad(vault, record.name),
        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(text.data())), static_cast<ULONG>(record.cipher.size()),
        record.cipher.data(), true);
}

inline bool decrypt(const Vault& vault, const Key& key, const Record& record, secure::Text& result) {
    secure::Text text(record.cipher.size() / sizeof(wchar_t));
    Record parameters;
    parameters.nonce = record.nonce;
    parameters.tag = record.tag;
    if (!crypt(key, parameters, aad(vault, record.name), const_cast<PUCHAR>(record.cipher.data()),
            static_cast<ULONG>(record.cipher.size()), reinterpret_cast<PUCHAR>(text.writable()), false))
        return false;
    text.length(record.cipher.size() / sizeof(wchar_t));
    for (size_t i = 0; i < text.size(); ++i)
        if (!text[i])
            return false;
    result = std::move(text);
    return true;
}

inline void writeRecord(Bytes& bytes, const Record& record) {
    number(bytes, static_cast<ULONG>(record.name.size()));
    append(bytes, record.name.data(), record.name.size() * sizeof(wchar_t));
    append(bytes, record.nonce.data(), record.nonce.size());
    append(bytes, record.tag.data(), record.tag.size());
    number(bytes, static_cast<ULONG>(record.cipher.size()));
    append(bytes, record.cipher.data(), record.cipher.size());
}

inline Bytes recordsBytes(const Vault& vault) {
    Bytes bytes;
    number(bytes, static_cast<ULONG>(vault.records.size()));
    for (const auto& record : vault.records)
        writeRecord(bytes, record);
    return bytes;
}

inline bool digest(const Bytes& bytes, BYTE* hash) {
    Algorithm algorithm(BCRYPT_SHA256_ALGORITHM);
    return algorithm.handle && BCryptHash(algorithm.handle, nullptr, 0, const_cast<PUCHAR>(bytes.data()),
                                   static_cast<ULONG>(bytes.size()), hash, 32) >= 0;
}

inline bool authenticate(Vault& vault, const Key& key) {
    if (vault.seal.cipher.size() != 32)
        return false;
    secure::Buffer check(32), expected(32);
    if (!crypt(key, vault.seal, aad(vault, L""), vault.seal.cipher.data(), 32, check.data(), false) ||
        !digest(recordsBytes(vault), expected.data()))
        return false;
    unsigned difference = 0;
    for (size_t i = 0; i < 32; ++i)
        difference |= check.data()[i] ^ expected.data()[i];
    return difference == 0;
}

inline bool seal(Vault& vault, const Key& key) {
    secure::Buffer hash(32);
    if (!digest(recordsBytes(vault), hash.data()) ||
        !random(vault.seal.nonce.data(), vault.seal.nonce.size()))
        return false;
    vault.seal.cipher.resize(32);
    return crypt(key, vault.seal, aad(vault, L""), hash.data(), 32, vault.seal.cipher.data(), true);
}

class Reader {
    const Bytes& data_;
    size_t position_ = 0;

public:
    explicit Reader(const Bytes& data) : data_(data) {}

    bool take(void* target, size_t size) {
        if (size > data_.size() - position_)
            return false;
        if (size)
            memcpy(target, data_.data() + position_, size);
        position_ += size;
        return true;
    }

    bool number(ULONG& value) {
        BYTE bytes[4];
        if (!take(bytes, 4))
            return false;
        value = static_cast<ULONG>(bytes[0]) | (static_cast<ULONG>(bytes[1]) << 8) |
                (static_cast<ULONG>(bytes[2]) << 16) | (static_cast<ULONG>(bytes[3]) << 24);
        return true;
    }

    bool record(Record& value) {
        ULONG length = 0, size = 0;
        if (!number(length) || length > snippets::MaximumNameCharacters)
            return false;
        value.name.resize(length);
        if (!take(length ? &value.name[0] : nullptr, length * sizeof(wchar_t)) ||
            !take(value.nonce.data(), value.nonce.size()) || !take(value.tag.data(), value.tag.size()) ||
            !number(size) || size > snippetdocument::MaximumEncodedCharacters * sizeof(wchar_t) ||
            size % sizeof(wchar_t))
            return false;
        if (size > data_.size() - position_)
            return false;
        value.cipher.resize(size);
        return take(value.cipher.data(), size);
    }

    bool finished() const {
        return position_ == data_.size();
    }
};

class Store {
    std::wstring root_;

    std::wstring path() const {
        return root_ + L"\\Encrypted.dat";
    }

    Result read(Vault& vault) const {
        if (root_.empty())
            return Result::ioError;
        const DWORD attributes = GetFileAttributesW(path().c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES)
            return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND
                       ? Result::missing
                       : Result::ioError;
        if (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))
            return Result::ioError;
        snippets::File file(CreateFileW(path().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        LARGE_INTEGER size{};
        if (file.handle == INVALID_HANDLE_VALUE || !snippets::regularFile(file.handle) ||
            !GetFileSizeEx(file.handle, &size))
            return Result::ioError;
        if (size.QuadPart < 0 || size.QuadPart > MaximumVaultBytes)
            return Result::tooLarge;
        Bytes data(static_cast<size_t>(size.QuadPart));
        DWORD received = 0;
        if (!ReadFile(file.handle, data.data(), static_cast<DWORD>(data.size()), &received, nullptr) ||
            received != data.size())
            return Result::ioError;
        Reader reader(data);
        BYTE magic[8];
        ULONG rounds = 0, count = 0;
        if (!reader.take(magic, 8) || memcmp(magic, "MKATVAUL", 8) || !reader.number(rounds) ||
            rounds != Iterations || !reader.take(vault.salt.data(), vault.salt.size()) ||
            !reader.record(vault.seal) || !vault.seal.name.empty() || vault.seal.cipher.size() != 32 ||
            !reader.number(count) || count > MaximumRecords)
            return Result::corrupt;
        for (ULONG i = 0; i < count; ++i) {
            Record record;
            if (!reader.record(record) || !snippets::validName(record.name))
                return Result::corrupt;
            for (const auto& existing : vault.records)
                if (snippets::sameName(existing.name, record.name))
                    return Result::corrupt;
            vault.records.push_back(std::move(record));
        }
        return reader.finished() ? Result::ok : Result::corrupt;
    }
    enum class MissingVaultPolicy {
        reject,
        create
    };

    // Every operation re-reads and authenticates the complete catalog, including
    // when a session supplies the key. No decrypted vault or key handle is cached.
    Result openAuthenticatedVault(Vault& vault, Key& key, const Credentials& credentials,
        MissingVaultPolicy missing = MissingVaultPolicy::reject) const {
        const auto& password = credentials.password;
        const auto result = read(vault);
        if (result == Result::missing && missing == MissingVaultPolicy::create) {
            if (password.empty() && credentials.session) {
                credentials.session->lock();
                return Result::authentication;
            }
            if (!validNewPassword(password))
                return Result::weakPassword;
            if (!random(vault.salt.data(), vault.salt.size()))
                return Result::cryptoError;
        } else if (result != Result::ok) {
            if (credentials.session)
                credentials.session->lock();
            return result;
        }
        if (password.empty()) {
            secure::Buffer material;
            if (!credentials.session || !credentials.session->restore(vault.salt, material))
                return Result::authentication;
            if (!key.initialize(material)) {
                credentials.session->lock();
                return Result::cryptoError;
            }
        } else if (!key.derive(password, vault.salt))
            return Result::cryptoError;
        if (result == Result::ok && !authenticate(vault, key)) {
            if (credentials.session)
                credentials.session->lock();
            return Result::authentication;
        }
        return Result::ok;
    }

    Result commit(Vault& vault, const Key& key) const {
        if (root_.empty())
            return Result::ioError;
        if (!seal(vault, key))
            return Result::cryptoError;
        Bytes data;
        append(data, "MKATVAUL", 8);
        number(data, Iterations);
        append(data, vault.salt.data(), vault.salt.size());
        writeRecord(data, vault.seal);
        const auto records = recordsBytes(vault);
        append(data, records.data(), records.size());
        if (data.size() > MaximumVaultBytes)
            return Result::tooLarge;
        const int directory = SHCreateDirectoryExW(nullptr, root_.c_str(), nullptr);
        if (directory != ERROR_SUCCESS && directory != ERROR_ALREADY_EXISTS && directory != ERROR_FILE_EXISTS)
            return Result::ioError;
        GUID id;
        wchar_t unique[40];
        if (FAILED(CoCreateGuid(&id)) || !StringFromGUID2(id, unique, 40))
            return Result::ioError;
        const std::wstring temporary = root_ + L"\\" + unique + L".vault-tmp";
        bool written = false;
        {
            snippets::File file(CreateFileW(
                temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
            DWORD received = 0;
            written =
                file.handle != INVALID_HANDLE_VALUE &&
                WriteFile(file.handle, data.data(), static_cast<DWORD>(data.size()), &received, nullptr) &&
                received == data.size() && FlushFileBuffers(file.handle);
        }
        if (written && MoveFileExW(temporary.c_str(), path().c_str(),
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return Result::ok;
        DeleteFileW(temporary.c_str());
        return Result::ioError;
    }

    static auto find(Vault& vault, const std::wstring& name) -> std::vector<Record>::iterator {
        return std::find_if(vault.records.begin(), vault.records.end(),
            [&](const Record& record) { return snippets::sameName(record.name, name); });
    }

    static bool hasName(const Vault& vault, const std::wstring& name, const Record* except = nullptr) {
        return std::any_of(vault.records.begin(), vault.records.end(),
            [&](const Record& record) { return &record != except && snippets::sameName(record.name, name); });
    }

public:
    explicit Store(std::wstring root) : root_(std::move(root)) {}

    Result list(std::vector<std::wstring>& names) const {
        names.clear();
        Vault vault;
        const auto result = read(vault);
        if (result == Result::missing)
            return Result::ok;
        if (result != Result::ok)
            return result;
        for (const auto& record : vault.records)
            names.push_back(record.name);
        return Result::ok;
    }

    Result unlock(const secure::Text& password, VaultSession& session) const {
        session.lock();
        Vault vault;
        const auto result = read(vault);
        if (result != Result::ok)
            return result;
        secure::Buffer material;
        Key key;
        if (!deriveMaterial(password, vault.salt, material) || !key.initialize(material))
            return Result::cryptoError;
        if (!authenticate(vault, key))
            return Result::authentication;
        return session.remember(material, vault.salt) ? Result::ok : Result::cryptoError;
    }

    bool exists() const {
        return !root_.empty() && GetFileAttributesW(path().c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    Result load(
        const std::wstring& name, const Credentials& password, snippetdocument::Document& document) const {
        document.clear();
        Vault vault;
        Key key;
        const auto result = openAuthenticatedVault(vault, key, password);
        if (result != Result::ok)
            return result;
        const auto record = find(vault, name);
        if (record == vault.records.end())
            return Result::missing;
        secure::Text encoded;
        if (!decrypt(vault, key, *record, encoded))
            return Result::authentication;
        return documentResult(snippetdocument::decode(encoded, document));
    }

    Result save(const std::wstring& name, const snippetdocument::Document& document,
        const Credentials& password, bool overwrite) const {
        if (!snippets::validName(name))
            return Result::invalidName;
        secure::Text encoded;
        const auto validation = documentResult(snippetdocument::encode(document, encoded));
        if (validation != Result::ok)
            return validation;
        snippets::CollectionWriteLock lock(root_ + L"\\Encrypted.dat");
        if (root_.empty() || !lock.acquired)
            return Result::ioError;
        Vault vault;
        Key key;
        const auto result = openAuthenticatedVault(vault, key, password, MissingVaultPolicy::create);
        if (result != Result::ok)
            return result;
        auto record = find(vault, name);
        if (record != vault.records.end() && !overwrite)
            return Result::exists;
        if (record == vault.records.end()) {
            if (hasName(vault, name))
                return Result::exists;
            if (vault.records.size() >= MaximumRecords)
                return Result::tooLarge;
            vault.records.emplace_back();
            record = vault.records.end() - 1;
        }
        record->name = name;
        if (!encrypt(vault, key, encoded, *record))
            return Result::cryptoError;
        encoded.clear();
        return commit(vault, key);
    }

    Result convert(const std::wstring& name, const snippetdocument::Document& document,
        const Credentials& password, bool encryptText, const snippets::Store& plain) const {
        if (!snippets::validName(name))
            return Result::invalidName;
        secure::Text encoded;
        const auto validation = documentResult(snippetdocument::encode(document, encoded));
        if (validation != Result::ok)
            return validation;
        // Acquire the plain collection first; recursive acquisition by save/remove
        // is intentional. Every conversion uses this order to avoid deadlock.
        snippets::CollectionWriteLock plainLock(plain.directory());
        snippets::CollectionWriteLock locked(root_ + L"\\Encrypted.dat");
        if (root_.empty() || !plainLock.acquired || !locked.acquired)
            return Result::ioError;
        Vault vault;
        Key key;
        const auto policy = encryptText ? MissingVaultPolicy::create : MissingVaultPolicy::reject;
        const auto result = openAuthenticatedVault(vault, key, password, policy);
        if (result != Result::ok)
            return result;
        const auto record = find(vault, name);
        if (encryptText) {
            if (hasName(vault, name))
                return Result::exists;
            if (vault.records.size() >= MaximumRecords)
                return Result::tooLarge;
            Record output;
            output.name = name;
            if (!encrypt(vault, key, encoded, output))
                return Result::cryptoError;
            encoded.clear();
            vault.records.push_back(std::move(output));
            const auto saved = commit(vault, key);
            if (saved != Result::ok)
                return saved;
            if (plain.remove(name) == snippets::Result::ok)
                return Result::ok;
            // Keep the source until the destination is committed; undo the new
            // entry if removing the source fails. Other encrypted entries stay intact.
            vault.records.pop_back();
            return commit(vault, key) == Result::ok ? Result::ioError : Result::rollbackFailed;
        }
        if (record == vault.records.end())
            return Result::missing;
        encoded.clear();
        const auto saved = plain.save(name, document, false);
        if (saved != snippets::Result::ok)
            return saved == snippets::Result::exists     ? Result::exists
                   : saved == snippets::Result::tooLarge ? Result::tooLarge
                                                         : Result::ioError;
        vault.records.erase(record);
        const auto removed = commit(vault, key);
        if (removed == Result::ok)
            return Result::ok;
        return plain.remove(name) == snippets::Result::ok ? removed : Result::rollbackFailed;
    }

    Result load(const std::wstring& name, const Credentials& password, secure::Text& text) const {
        text.clear();
        snippetdocument::Document document;
        const auto result = load(name, password, document);
        return result == Result::ok ? documentResult(snippetdocument::takeEditorText(document, text))
                                    : result;
    }

    Result save(const std::wstring& name, const secure::Text& text, const Credentials& password,
        bool overwrite) const {
        if (!validText(text))
            return Result::invalidText;
        auto document = snippetdocument::Document::fromText(secure::Text(text.data(), text.size()));
        return save(name, document, password, overwrite);
    }

    Result convert(const std::wstring& name, const secure::Text& text, const Credentials& password,
        bool encryptText, const snippets::Store& plain) const {
        if (!validText(text))
            return Result::invalidText;
        auto document = snippetdocument::Document::fromText(secure::Text(text.data(), text.size()));
        return convert(name, document, password, encryptText, plain);
    }

    Result rename(const std::wstring& before, const std::wstring& after, const Credentials& password) const {
        if (!snippets::validName(after))
            return Result::invalidName;
        snippets::CollectionWriteLock lock(root_ + L"\\Encrypted.dat");
        if (root_.empty() || !lock.acquired)
            return Result::ioError;
        Vault vault;
        Key key;
        const auto result = openAuthenticatedVault(vault, key, password);
        if (result != Result::ok)
            return result;
        auto record = find(vault, before);
        if (record == vault.records.end())
            return Result::missing;
        if (hasName(vault, after, &*record))
            return Result::exists;
        secure::Text text;
        if (!decrypt(vault, key, *record, text))
            return Result::authentication;
        snippetdocument::Document document;
        const auto validation = documentResult(snippetdocument::decode(text, document));
        if (validation != Result::ok)
            return validation;
        document.clear();
        record->name = after;
        if (!encrypt(vault, key, text, *record))
            return Result::cryptoError;
        text.clear();
        return commit(vault, key);
    }

    Result remove(const std::wstring& name, const Credentials& password) const {
        snippets::CollectionWriteLock lock(root_ + L"\\Encrypted.dat");
        if (root_.empty() || !lock.acquired)
            return Result::ioError;
        Vault vault;
        Key key;
        const auto result = openAuthenticatedVault(vault, key, password);
        if (result != Result::ok)
            return result;
        const auto record = find(vault, name);
        if (record == vault.records.end())
            return Result::missing;
        vault.records.erase(record);
        return commit(vault, key);
    }

    Result changePassword(const secure::Text& before, const secure::Text& after) const {
        if (!validNewPassword(after))
            return Result::weakPassword;
        snippets::CollectionWriteLock lock(root_ + L"\\Encrypted.dat");
        if (root_.empty() || !lock.acquired)
            return Result::ioError;
        Vault old;
        Key oldKey;
        const auto result = openAuthenticatedVault(old, oldKey, before);
        if (result != Result::ok)
            return result;
        Vault updated;
        if (!random(updated.salt.data(), updated.salt.size()))
            return Result::cryptoError;
        Key newKey;
        if (!newKey.derive(after, updated.salt))
            return Result::cryptoError;
        for (const auto& record : old.records) {
            secure::Text text;
            if (!decrypt(old, oldKey, record, text))
                return Result::authentication;
            snippetdocument::Document document;
            const auto validation = documentResult(snippetdocument::decode(text, document));
            if (validation != Result::ok)
                return validation;
            document.clear();
            Record output;
            output.name = record.name;
            if (!encrypt(updated, newKey, text, output))
                return Result::cryptoError;
            text.clear();
            updated.records.push_back(std::move(output));
        }
        // One atomic replacement updates every encrypted snippet together.
        return commit(updated, newKey);
    }
};
} // namespace encrypted

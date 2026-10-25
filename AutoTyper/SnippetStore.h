#pragma once
#include <Windows.h>
#include <shlobj.h>
#include <objbase.h>
#include <algorithm>
#include <cwctype>
#include <cwchar>
#include <string>
#include <vector>
#include <utility>
#include "SnippetCodec.h"
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")

namespace snippets {
enum class Result {
    ok,
    invalidName,
    exists,
    invalidText,
    tooLarge,
    invalidDocument,
    unsupportedFormat,
    unsupportedActions,
    missing,
    ioError
};
constexpr DWORD MaximumBytes = 64 * 1024 * 1024;
constexpr size_t MaximumRecords = 4096, MaximumCollectionActions = 32768;
constexpr size_t MaximumCollectionText = 16 * 1024 * 1024;
constexpr size_t MaximumNameCharacters = 80;

enum class NameIssue {
    none,
    empty,
    tooLong,
    invalidCharacter,
};

inline int compareNames(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(
        a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), TRUE);
}

inline bool sameName(const std::wstring& a, const std::wstring& b) {
    return compareNames(a, b) == CSTR_EQUAL;
}

inline NameIssue nameIssue(const std::wstring& name) {
    if (name.empty())
        return NameIssue::empty;
    if (name.size() > MaximumNameCharacters)
        return NameIssue::tooLong;
    for (size_t i = 0; i < name.size(); ++i) {
        const wchar_t c = name[i];
        if (c < 32 || (c >= 0x7f && c <= 0x9f) || (c >= 0xdc00 && c <= 0xdfff))
            return NameIssue::invalidCharacter;
        if (c >= 0xd800 && c <= 0xdbff) {
            if (++i == name.size() || name[i] < 0xdc00 || name[i] > 0xdfff)
                return NameIssue::invalidCharacter;
        }
    }
    return NameIssue::none;
}

inline bool validName(const std::wstring& name) {
    return nameIssue(name) == NameIssue::none;
}

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

struct File {
    HANDLE handle = INVALID_HANDLE_VALUE;

    explicit File(HANDLE value) : handle(value) {}

    ~File() {
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }

    File(const File&) = delete;
    File& operator=(const File&) = delete;
};

inline bool regularFile(HANDLE file) {
    BY_HANDLE_FILE_INFORMATION info{};
    return GetFileInformationByHandle(file, &info) &&
           !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY));
}

struct Entry {
    std::wstring name;
    snippetdocument::Document document;
};

using Collection = std::vector<Entry>;

inline Result validateCollection(const Collection& entries) {
    if (entries.size() > MaximumRecords)
        return Result::tooLarge;
    size_t actions = 0, characters = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (!validName(entries[i].name))
            return Result::invalidName;
        for (size_t j = 0; j < i; ++j)
            if (sameName(entries[i].name, entries[j].name))
                return Result::exists;
        const auto result = documentResult(snippetdocument::validate(entries[i].document));
        if (result != Result::ok)
            return result;
        actions += entries[i].document.actions.size();
        for (const auto& action : entries[i].document.actions)
            characters += action.text.size();
        if (actions > MaximumCollectionActions || characters > MaximumCollectionText)
            return Result::tooLarge;
    }
    return Result::ok;
}

inline bool readEntries(snippetdocument::json::Reader& reader, Collection& entries) {
    if (!reader.consume(L'['))
        return false;
    if (reader.consume(L']'))
        return true;
    size_t actions = 0, characters = 0;
    do {
        if (entries.size() == MaximumRecords)
            return reader.fail(snippetdocument::Result::tooLarge);
        if (!reader.consume(L'{'))
            return false;
        Entry entry;
        unsigned fields = 0;
        do {
            secure::Text key;
            if (!reader.string(key) || !reader.consume(L':'))
                return false;
            if (snippetdocument::json::equal(key, L"name")) {
                if (fields & 1)
                    return false;
                fields |= 1;
                secure::Text name;
                if (!reader.string(name, MaximumNameCharacters))
                    return false;
                entry.name.assign(name.data(), name.size());
                if (!validName(entry.name))
                    return false;
            } else if (snippetdocument::json::equal(key, L"document")) {
                if (fields & 2)
                    return false;
                fields |= 2;
                if (!reader.document(entry.document, false))
                    return false;
                const auto valid = snippetdocument::validate(entry.document);
                if (valid != snippetdocument::Result::ok)
                    return reader.fail(valid);
            } else
                return reader.fail(snippetdocument::Result::unsupportedVersion);
        } while (reader.consume(L','));
        if (!reader.consume(L'}') || fields != 3)
            return false;
        for (const auto& existing : entries)
            if (sameName(existing.name, entry.name))
                return false;
        actions += entry.document.actions.size();
        for (const auto& action : entry.document.actions)
            characters += action.text.size();
        if (actions > MaximumCollectionActions || characters > MaximumCollectionText)
            return reader.fail(snippetdocument::Result::tooLarge);
        entries.push_back(std::move(entry));
    } while (reader.consume(L','));
    return reader.consume(L']');
}

inline Result decodeCollection(const secure::Text& text, Collection& output) {
    output.clear();
    if (text.size() > MaximumBytes)
        return Result::tooLarge;
    snippetdocument::json::Reader reader(text);
    Collection entries;
    if (!reader.consume(L'{'))
        return Result::invalidDocument;
    unsigned fields = 0;
    do {
        secure::Text key;
        if (!reader.string(key) || !reader.consume(L':'))
            return documentResult(reader.error());
        unsigned field = 0;
        if (snippetdocument::json::equal(key, L"format"))
            field = 1;
        else if (snippetdocument::json::equal(key, L"version"))
            field = 2;
        else if (snippetdocument::json::equal(key, L"snippets"))
            field = 4;
        else
            return Result::unsupportedFormat;
        if (fields & field)
            return Result::invalidDocument;
        fields |= field;
        if (field == 1) {
            secure::Text format;
            if (!reader.string(format))
                return documentResult(reader.error());
            if (!snippetdocument::json::equal(format, L"mk-autotyper-collection"))
                return Result::unsupportedFormat;
        } else if (field == 2) {
            unsigned version = 0;
            if (!reader.number(version))
                return documentResult(reader.error());
            if (version != 1)
                return Result::unsupportedFormat;
        } else if (!readEntries(reader, entries))
            return documentResult(reader.error());
    } while (reader.consume(L','));
    if (!reader.consume(L'}') || fields != 7 || !reader.finished())
        return Result::invalidDocument;
    output = std::move(entries);
    return Result::ok;
}

inline void writeCollection(snippetdocument::json::Writer& writer, const Collection& entries) {
    writer.literal(L"{\n  \"format\": \"mk-autotyper-collection\",\n  \"version\": 1,\n  \"snippets\": [");
    for (size_t i = 0; i < entries.size(); ++i) {
        writer.literal(i ? L",\n    {\"name\": " : L"\n    {\"name\": ");
        writer.string(secure::Text(entries[i].name));
        writer.literal(L", \"document\": ");
        writer.document(entries[i].document);
        writer.character(L'}');
    }
    writer.literal(L"\n  ]\n}\n");
}

// Serialize read-modify-write operations across app instances. Readers see an
// entire old or new file through atomic replacement, and do not hold this lock.
struct CollectionWriteLock {
    HANDLE handle = nullptr;
    bool acquired = false;

    explicit CollectionWriteLock(const std::wstring& root) {
        if (root.empty())
            return;
        std::wstring canonical(32768, L'\0');
        const DWORD size =
            GetFullPathNameW(root.c_str(), static_cast<DWORD>(canonical.size()), &canonical[0], nullptr);
        if (!size || size >= canonical.size())
            return;
        canonical.resize(size);
        std::replace(canonical.begin(), canonical.end(), L'/', L'\\');
        while (canonical.size() > 3 && canonical.back() == L'\\')
            canonical.pop_back();
        CharUpperBuffW(&canonical[0], static_cast<DWORD>(canonical.size()));
        unsigned long long hash = 14695981039346656037ull;
        for (wchar_t c : canonical) {
            hash ^= c;
            hash *= 1099511628211ull;
        }
        wchar_t name[80];
        swprintf_s(name, L"Local\\MK-AutoTyper.Snippets.%016llx", hash);
        handle = CreateMutexW(nullptr, FALSE, name);
        if (handle) {
            const DWORD wait = WaitForSingleObject(handle, 5000);
            acquired = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
        }
    }

    ~CollectionWriteLock() {
        if (acquired)
            ReleaseMutex(handle);
        if (handle)
            CloseHandle(handle);
    }

    CollectionWriteLock(const CollectionWriteLock&) = delete;
    CollectionWriteLock& operator=(const CollectionWriteLock&) = delete;
};

class Store {
    std::wstring root_;

    Result read(Collection& entries) const {
        entries.clear();
        if (root_.empty())
            return Result::ioError;
        const auto filePath = path();
        const DWORD attributes = GetFileAttributesW(filePath.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES)
            return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND
                       ? Result::ok
                       : Result::ioError;
        if (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))
            return Result::ioError;
        File file(CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        LARGE_INTEGER size{};
        if (file.handle == INVALID_HANDLE_VALUE || !regularFile(file.handle) ||
            !GetFileSizeEx(file.handle, &size))
            return Result::ioError;
        if (size.QuadPart < 0 || size.QuadPart > MaximumBytes)
            return Result::tooLarge;
        if (!size.QuadPart)
            return Result::invalidDocument;
        secure::Buffer data(static_cast<size_t>(size.QuadPart));
        DWORD received = 0;
        if (!ReadFile(file.handle, data.data(), static_cast<DWORD>(data.size()), &received, nullptr) ||
            received != data.size())
            return Result::ioError;
        const bool bom = data.size() >= 3 && !memcmp(data.data(), "\xef\xbb\xbf", 3);
        const int length = static_cast<int>(data.size()) - (bom ? 3 : 0);
        if (!length)
            return Result::invalidDocument;
        const auto* bytes = reinterpret_cast<const char*>(data.data()) + (bom ? 3 : 0);
        const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes, length, nullptr, 0);
        if (!count)
            return Result::invalidText;
        secure::Text text(count);
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes, length, text.writable(), count))
            return Result::invalidText;
        text.length(count);
        return decodeCollection(text, entries);
    }

    Result commit(const Collection& entries) const {
        const auto validation = validateCollection(entries);
        if (validation != Result::ok)
            return validation;
        snippetdocument::json::Writer counter(nullptr, MaximumBytes);
        writeCollection(counter, entries);
        if (counter.full())
            return Result::tooLarge;
        secure::Text encoded(counter.size());
        snippetdocument::json::Writer writer(encoded.writable(), MaximumBytes);
        writeCollection(writer, entries);
        encoded.length(writer.size());
        const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, encoded.data(),
            static_cast<int>(encoded.size()), nullptr, 0, nullptr, nullptr);
        if (!count)
            return Result::invalidText;
        if (count > MaximumBytes)
            return Result::tooLarge;
        secure::Buffer data(count);
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, encoded.data(),
                static_cast<int>(encoded.size()), reinterpret_cast<char*>(data.data()), count, nullptr,
                nullptr))
            return Result::invalidText;
        encoded.clear();
        const int created = SHCreateDirectoryExW(nullptr, root_.c_str(), nullptr);
        if (created != ERROR_SUCCESS && created != ERROR_ALREADY_EXISTS && created != ERROR_FILE_EXISTS)
            return Result::ioError;
        GUID id;
        wchar_t unique[40];
        if (FAILED(CoCreateGuid(&id)) || !StringFromGUID2(id, unique, 40))
            return Result::ioError;
        const auto temporary = root_ + L"\\" + unique + L".tmp";
        bool written = false;
        {
            File file(CreateFileW(
                temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
            if (file.handle == INVALID_HANDLE_VALUE)
                return Result::ioError;
            DWORD bytes = 0;
            written = WriteFile(file.handle, data.data(), static_cast<DWORD>(data.size()), &bytes, nullptr) &&
                      bytes == data.size() && FlushFileBuffers(file.handle);
        }
        if (written && MoveFileExW(temporary.c_str(), path().c_str(),
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return Result::ok;
        DeleteFileW(temporary.c_str());
        return Result::ioError;
    }

    static auto find(Collection& entries, const std::wstring& name) -> Collection::iterator {
        return std::find_if(
            entries.begin(), entries.end(), [&](const Entry& entry) { return sameName(entry.name, name); });
    }

public:
    explicit Store(std::wstring root = L"") : root_(std::move(root)) {
        if (!root_.empty())
            return;
        PWSTR folder = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder))) {
            root_ = std::wstring(folder) + L"\\MK-AutoTyper";
            CoTaskMemFree(folder);
        }
    }

    const std::wstring& directory() const {
        return root_;
    }

    std::wstring path() const {
        return root_ + L"\\Snippets.json";
    }

    bool list(std::vector<std::wstring>& names) const {
        names.clear();
        Collection entries;
        if (read(entries) != Result::ok)
            return false;
        for (const auto& entry : entries)
            names.push_back(entry.name);
        std::sort(names.begin(), names.end(), [](const std::wstring& a, const std::wstring& b) {
            return compareNames(a, b) == CSTR_LESS_THAN;
        });
        return true;
    }

    Result load(const std::wstring& name, snippetdocument::Document& document) const {
        document.clear();
        if (!validName(name))
            return Result::invalidName;
        Collection entries;
        const auto result = read(entries);
        if (result != Result::ok)
            return result;
        const auto entry = find(entries, name);
        if (entry == entries.end())
            return Result::missing;
        document = std::move(entry->document);
        return Result::ok;
    }

    Result save(const std::wstring& name, const snippetdocument::Document& document, bool overwrite) const {
        if (!validName(name))
            return Result::invalidName;
        const auto validation = documentResult(snippetdocument::validate(document));
        if (validation != Result::ok)
            return validation;
        CollectionWriteLock lock(root_);
        if (!lock.acquired)
            return Result::ioError;
        Collection entries;
        const auto result = read(entries);
        if (result != Result::ok)
            return result;
        auto entry = find(entries, name);
        if (entry != entries.end() && !overwrite)
            return Result::exists;
        if (entry == entries.end()) {
            if (entries.size() == MaximumRecords)
                return Result::tooLarge;
            entries.emplace_back();
            entry = entries.end() - 1;
            entry->name = name;
        }
        // Clone through the codec so action-owned secret buffers remain move-only.
        secure::Text encoded;
        auto cloned = snippetdocument::encode(document, encoded);
        if (cloned != snippetdocument::Result::ok)
            return documentResult(cloned);
        cloned = snippetdocument::decode(encoded, entry->document);
        if (cloned != snippetdocument::Result::ok)
            return documentResult(cloned);
        return commit(entries);
    }

    Result load(const std::wstring& name, secure::Text& text) const {
        text.clear();
        snippetdocument::Document document;
        const auto result = load(name, document);
        return result == Result::ok ? documentResult(snippetdocument::takeEditorText(document, text))
                                    : result;
    }

    Result load(const std::wstring& name, std::wstring& text) const {
        text.clear();
        secure::Text value;
        const auto result = load(name, value);
        if (result == Result::ok)
            text.assign(value.data(), value.size());
        return result;
    }

    Result save(const std::wstring& name, const std::wstring& text, bool overwrite) const {
        auto document = snippetdocument::Document::fromText(secure::Text(text));
        return save(name, document, overwrite);
    }

    Result rename(const std::wstring& before, const std::wstring& after) const {
        if (!validName(before) || !validName(after))
            return Result::invalidName;
        CollectionWriteLock lock(root_);
        if (!lock.acquired)
            return Result::ioError;
        Collection entries;
        const auto result = read(entries);
        if (result != Result::ok)
            return result;
        const auto entry = find(entries, before), collision = find(entries, after);
        if (entry == entries.end())
            return Result::missing;
        if (collision != entries.end() && collision != entry)
            return Result::exists;
        entry->name = after;
        return commit(entries);
    }

    Result remove(const std::wstring& name) const {
        if (!validName(name))
            return Result::invalidName;
        CollectionWriteLock lock(root_);
        if (!lock.acquired)
            return Result::ioError;
        Collection entries;
        const auto result = read(entries);
        if (result != Result::ok)
            return result;
        const auto entry = find(entries, name);
        if (entry == entries.end())
            return Result::missing;
        entries.erase(entry);
        return commit(entries);
    }
};
} // namespace snippets

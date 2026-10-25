#define NOMINMAX
#include "../AutoTyper/SnippetStore.h"
#include <cstdio>
#include <stdexcept>

using namespace snippetdocument;

void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

bool sameDocument(const Document& a, const Document& b) {
    if (a.actions.size() != b.actions.size())
        return false;
    for (size_t i = 0; i < a.actions.size(); ++i) {
        const auto& x = a.actions[i];
        const auto& y = b.actions[i];
        if (x.type != y.type || !secure::equal(x.text, y.text) || x.key.encoding != y.key.encoding ||
            x.key.code != y.key.code || x.key.modifiers != y.key.modifiers ||
            x.key.extended != y.key.extended || x.milliseconds != y.milliseconds)
            return false;
    }
    return true;
}

Document sequence() {
    Document document;
    document.actions.push_back(Action::typeText(
        secure::Text(std::wstring(L"\u00c4pfel \u00df \u20ac \xd83d\xde00\r\n\t\b\f\x01 \"\\ {Win+R}"))));
    document.actions.push_back(Action::pressKey({KeyEncoding::virtualKey, 'R', windows, false}));
    document.actions.push_back(Action::delay(500));
    document.actions.push_back(
        Action::pressKey({KeyEncoding::scanCode, 0x1c, control | alt | shift | windows, true}));
    document.actions.push_back(Action::pressKey({KeyEncoding::virtualKey, VK_LWIN, 0, false}));
    document.actions.push_back(Action::typeText(secure::Text(std::wstring(L""))));
    return document;
}

void codec() {
    const auto original = sequence();
    secure::Text encoded;
    check(encode(original, encoded) == Result::ok, "Sequence encoding failed");
    Document decoded;
    check(decode(encoded, decoded) == Result::ok && sameDocument(original, decoded),
        "Action order or data changed");
    secure::Text canonical;
    check(encode(decoded, canonical) == Result::ok && secure::equal(encoded, canonical),
        "Non-deterministic JSON writer");
    Document allKeys;
    for (unsigned code = 1; code <= 254; ++code)
        allKeys.actions.push_back(Action::pressKey({KeyEncoding::virtualKey, code, code % 16, false}));
    for (unsigned code = 1; code <= 255; ++code)
        allKeys.actions.push_back(Action::pressKey({KeyEncoding::scanCode, code, code % 16, code % 2 != 0}));
    check(encode(allKeys, canonical) == Result::ok && decode(canonical, decoded) == Result::ok &&
              sameDocument(allKeys, decoded),
        "Key code or modifier range did not roundtrip");
    check(decode(encoded, decoded) == Result::ok, "Sequence fixture restore failed");
    secure::Text editor(std::wstring(L"stale"));
    check(takeEditorText(decoded, editor) == Result::notText && editor.empty() &&
              sameDocument(original, decoded),
        "Text-only editor flattened or consumed actions");

    const std::wstring reordered =
        LR"({"actions":[{"text":"\u00c4\uD83D\uDE00\t\n\/\\\"","type":"text"}],"version":1,"format":"mk-autotyper-snippet"})";
    check(decode(secure::Text(reordered), decoded) == Result::ok &&
              takeEditorText(decoded, editor) == Result::ok &&
              secure::equal(editor, secure::Text(std::wstring(L"\u00c4\xd83d\xde00\t\n/\\\""))),
        "JSON field order, escapes or surrogate pairs failed");

    for (size_t size = 0; size < encoded.size() - 1; ++size) {
        auto stale = sequence();
        check(decode(secure::Text(encoded.data(), size), stale) != Result::ok && stale.actions.empty(),
            "Truncated JSON was accepted or returned partial actions");
    }
    const std::vector<std::wstring> invalid = {L"", L"plain old snippet", L"[]", L"{}",
        LR"({"format":"mk-autotyper-snippet","version":1,"version":1,"actions":[]})",
        LR"({"format":"mk-autotyper-snippet","version":01,"actions":[]})",
        LR"({"format":"mk-autotyper-snippet","version":1.0,"actions":[]})",
        LR"({"format":"mk-autotyper-snippet","version":-1,"actions":[]})",
        LR"({"format":"mk-autotyper-snippet","version":4294967296,"actions":[]})",
        LR"({"format":"mk-autotyper-snippet","version":1,"actions":[],})",
        LR"({"format":"mk-autotyper-snippet","version":1,"actions":[]}false)",
        LR"({"format":"mk-autotyper-snippet","version":1,"actions":[{"type":"text","text":"a","text":"b"}]})",
        LR"({"format":"mk-autotyper-snippet","version":1,"actions":[{"type":"text"}]})",
        LR"({"format":"mk-autotyper-snippet","version":1,"actions":[{"type":"text","text":"\q"}]})",
        LR"({"format":"mk-autotyper-snippet","version":1,"actions":[{"type":"text","text":"\u0000"}]})",
        LR"({"format":"mk-autotyper-snippet","version":1,"actions":[{"type":"text","text":"\ud800"}]})",
        LR"({"format":"mk-autotyper-snippet","version":1,"actions":[{"type":"text","text":"\udc00"}]})",
        LR"({"format":"mk-autotyper-snippet","version":1,"actions":[{"type":"delay","milliseconds":1e3}]})",
        LR"({"format":"mk-autotyper-snippet","version":1,"actions":[{"type":"delay","milliseconds":0,"text":"ignored"}]})",
        LR"({"format":"mk-autotyper-snippet","version":1,"actions":[[[[]]]]})"};
    for (const auto& text : invalid) {
        auto stale = sequence();
        check(decode(secure::Text(text), stale) != Result::ok && stale.actions.empty(),
            "Invalid JSON was accepted or retained stale output");
    }
    for (const auto& text : {LR"({"format":"mk-autotyper-snippet","version":2,"actions":[]})",
             LR"({"format":"mk-autotyper-snippet","version":1,"actions":[],"future":true})"}) {
        check(decode(secure::Text(std::wstring(text)), decoded) == Result::unsupportedVersion,
            "Future document data was silently ignored");
    }
    for (const auto& action : {LR"({"type":"future"})", LR"({"type":"text","text":"x","future":true})",
             LR"({"type":"key","encoding":"future","code":1,"modifiers":0,"extended":false})"}) {
        const auto text =
            std::wstring(LR"({"format":"mk-autotyper-snippet","version":1,"actions":[)") + action + L"]}";
        check(decode(secure::Text(text), decoded) == Result::unsupportedAction,
            "Future action data was silently ignored");
    }

    Document bounds;
    bounds.actions.push_back(Action::delay(MaximumDelayMilliseconds));
    check(encode(bounds, encoded) == Result::ok, "Maximum delay was rejected");
    ++bounds.actions[0].milliseconds;
    check(encode(bounds, encoded) == Result::tooLarge && encoded.empty(), "Unbounded delay accepted");
    for (const auto key :
        {KeyStroke{KeyEncoding::virtualKey, 0, 0, false}, KeyStroke{KeyEncoding::virtualKey, 255, 0, false},
            KeyStroke{KeyEncoding::scanCode, 256, 0, false},
            KeyStroke{KeyEncoding::virtualKey, 'A', 16, false},
            KeyStroke{KeyEncoding::virtualKey, 'A', 0, true}}) {
        bounds.actions.clear();
        bounds.actions.push_back(Action::pressKey(key));
        check(encode(bounds, encoded) == Result::invalid && encoded.empty(), "Invalid key action accepted");
    }
    bounds.clear();
    bounds.actions.push_back(Action::pressKey({KeyEncoding::scanCode, 255, 15, true}));
    check(encode(bounds, encoded) == Result::ok, "Full scan code range was rejected");
    bounds = Document::fromText(secure::Text(std::wstring(MaximumTextCharacters, L'\x01')));
    check(encode(bounds, encoded) == Result::ok && decode(encoded, decoded) == Result::ok &&
              sameDocument(bounds, decoded),
        "Maximum escaped text roundtrip failed");
    bounds.actions.push_back(Action::typeText(secure::Text(std::wstring(L"x"))));
    check(
        encode(bounds, encoded) == Result::tooLarge && encoded.empty(), "Total text budget was not enforced");
    bounds.clear();
    bounds.actions.resize(MaximumActions);
    check(encode(bounds, encoded) == Result::ok && decode(encoded, decoded) == Result::ok &&
              decoded.actions.size() == MaximumActions,
        "Maximum action count failed");
    bounds.actions.emplace_back();
    check(encode(bounds, encoded) == Result::tooLarge, "Unbounded action count accepted");
    check(
        decode(secure::Text(std::wstring(MaximumEncodedCharacters + 1, L' ')), decoded) == Result::tooLarge &&
            decoded.actions.empty(),
        "Encoded document bound was not enforced");
}

void collectionCodec() {
    auto document = sequence();
    secure::Text payload;
    check(encode(document, payload) == Result::ok, "Collection document fixture failed");
    const std::wstring body(payload.data(), payload.size());
    const auto wrap = [&](const std::wstring& entries) {
        return std::wstring(L"{\"format\":\"mk-autotyper-collection\",\"version\":1,\"snippets\":[") +
               entries + L"]}";
    };
    const auto item = [&](const std::wstring& name) {
        return L"{\"name\":\"" + name + L"\",\"document\":" + body + L"}";
    };
    const auto valid = wrap(item(L"<>:\\\"/\\\\|?* \\ud83d\\ude00") + L"," + item(L"Second"));
    snippets::Collection entries;
    check(snippets::decodeCollection(secure::Text(valid), entries) == snippets::Result::ok &&
              entries.size() == 2 && entries[0].name == L"<>:\"/\\|?* \xd83d\xde00" &&
              sameDocument(document, entries[0].document) && sameDocument(document, entries[1].document),
        "Collection name escapes or nested actions failed");
    snippetdocument::json::Writer counter(nullptr, snippets::MaximumBytes);
    snippets::writeCollection(counter, entries);
    secure::Text encoded(counter.size());
    snippetdocument::json::Writer writer(encoded.writable(), snippets::MaximumBytes);
    snippets::writeCollection(writer, entries);
    encoded.length(writer.size());
    check(snippets::decodeCollection(encoded, entries) == snippets::Result::ok && entries.size() == 2 &&
              sameDocument(document, entries[0].document),
        "Collection writer changed actions");
    for (const auto& invalid : {L"", L"{}", L"[]",
             L"{\"format\":\"mk-autotyper-collection\",\"version\":1,\"snippets\":[],\"extra\":true}",
             L"{\"format\":\"mk-autotyper-collection\",\"version\":2,\"snippets\":[]}"}) {
        check(snippets::decodeCollection(secure::Text(std::wstring(invalid)), entries) !=
                      snippets::Result::ok &&
                  entries.empty(),
            "Unknown/malformed collection returned partial or stale entries");
    }
    for (const auto& invalid :
        {wrap(item(L"\u00c4pfel") + L"," + item(L"\u00e4pfel")), wrap(item(L"\\u0000")),
            wrap(item(std::wstring(81, L'x'))), wrap(L"{\"name\":\"Missing document\"}"),
            wrap(L"{\"name\":\"A\",\"name\":\"B\",\"document\":" + body + L"}"), valid + L"false"}) {
        check(snippets::decodeCollection(secure::Text(invalid), entries) != snippets::Result::ok &&
                  entries.empty(),
            "Invalid/duplicate collection entry was accepted");
    }
    for (size_t length = 0; length < valid.size(); ++length)
        check(
            snippets::decodeCollection(secure::Text(valid.data(), length), entries) != snippets::Result::ok &&
                entries.empty(),
            "Truncated collection returned partial actions");
    entries.resize(snippets::MaximumRecords + 1);
    check(snippets::validateCollection(entries) == snippets::Result::tooLarge,
        "Collection record count is unbounded");
    entries.clear();
    for (size_t i = 0; i <= snippets::MaximumCollectionActions / MaximumActions; ++i) {
        snippets::Entry entry;
        entry.name = std::to_wstring(i);
        entry.document.actions.resize(MaximumActions);
        entries.push_back(std::move(entry));
    }
    check(snippets::validateCollection(entries) == snippets::Result::tooLarge,
        "Collection action budget is unbounded");
}

void storage() {
    wchar_t executable[32768];
    GetModuleFileNameW(nullptr, executable, 32768);
    const std::wstring root =
        std::wstring(executable) + L"." + std::to_wstring(GetCurrentProcessId()) + L".data";
    snippets::Store store(root);
    const auto document = sequence();
    check(store.save(L"\u00c4ctions", document, false) == snippets::Result::ok, "Document save failed");
    Document loaded;
    check(store.load(L"\u00e4CTIONS", loaded) == snippets::Result::ok && sameDocument(document, loaded),
        "Plain JSON action roundtrip failed");
    secure::Text editor(std::wstring(L"stale"));
    check(store.load(L"\u00c4ctions", editor) == snippets::Result::unsupportedActions && editor.empty(),
        "Store flattened action document for the text editor");
    check(store.rename(L"\u00c4ctions", L"Renamed") == snippets::Result::ok &&
              store.load(L"Renamed", loaded) == snippets::Result::ok && sameDocument(document, loaded),
        "Rename altered actions");
    {
        snippets::File file(CreateFileW(
            store.path().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
        check(file.handle != INVALID_HANDLE_VALUE, "Document fixture open failed");
        const auto updated = Document::fromText(secure::Text(std::wstring(L"replacement")));
        check(store.save(L"Renamed", updated, true) == snippets::Result::ioError,
            "Blocked atomic replacement reported success");
    }
    check(store.load(L"Renamed", loaded) == snippets::Result::ok && sameDocument(document, loaded),
        "Failed update damaged original action sequence");
    Document invalid;
    invalid.actions.push_back(Action::pressKey({KeyEncoding::virtualKey, 0, 0, false}));
    check(store.save(L"Renamed", invalid, true) == snippets::Result::invalidDocument &&
              store.load(L"Renamed", loaded) == snippets::Result::ok && sameDocument(document, loaded),
        "Invalid document replaced an existing snippet");
    check(store.remove(L"Renamed") == snippets::Result::ok && DeleteFileW(store.path().c_str()) &&
              RemoveDirectoryW(root.c_str()),
        "Document temporary files leaked");
}

int main() {
    try {
        codec();
        collectionCodec();
        storage();
        std::puts(
            "PASS: versioned JSON actions, key encodings/modifiers, bounds, strict malformed/future data rejection, text-editor protection and atomic document storage.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}

#pragma once
#include "SnippetCodec.h"

namespace actioneditor {
inline secure::Text keyName(const snippetdocument::KeyStroke& key) {
    const HKL layout = GetKeyboardLayout(0);
    const UINT vk =
        key.encoding == snippetdocument::KeyEncoding::virtualKey
            ? key.code
            : MapVirtualKeyExW(key.code | (key.extended ? 0xe000 : 0), MAPVK_VSC_TO_VK_EX, layout);
    secure::Text label(256);
    snippetdocument::json::Writer writer(label.writable(), 256);
    if (key.modifiers & snippetdocument::windows)
        writer.literal(L"Win+");
    if (key.modifiers & snippetdocument::control)
        writer.literal(L"Ctrl+");
    if (key.modifiers & snippetdocument::alt)
        writer.literal(L"Alt+");
    if (key.modifiers & snippetdocument::shift)
        writer.literal(L"Shift+");
    const UINT scan = key.encoding == snippetdocument::KeyEncoding::scanCode
                          ? key.code | (key.extended ? 0xe000 : 0)
                          : MapVirtualKeyExW(vk, MAPVK_VK_TO_VSC_EX, layout);
    wchar_t name[96]{};
    const LONG flags = static_cast<LONG>((scan & 255) << 16) | ((scan & 0xff00) == 0xe000 ? (1 << 24) : 0);
    if (vk == VK_PAUSE)
        writer.literal(L"Pause");
    else if (GetKeyNameTextW(flags, name, 96))
        writer.literal(name);
    else {
        writer.literal(key.encoding == snippetdocument::KeyEncoding::scanCode ? L"Scan code " : L"Key ");
        writer.number(key.encoding == snippetdocument::KeyEncoding::scanCode ? key.code : vk);
    }
    SecureZeroMemory(name, sizeof(name));
    label.length(writer.size());
    return label;
}

inline const wchar_t* kind(const snippetdocument::Action& action) {
    return action.type == snippetdocument::ActionType::text  ? L"Text"
           : action.type == snippetdocument::ActionType::key ? L"Key / shortcut"
                                                             : L"Pause";
}

inline bool number(HWND control, DWORD minimum, DWORD maximum, DWORD& result) {
    secure::Text value = secure::Text::read(control);
    if (value.empty())
        return false;
    DWORD parsed = 0;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] < L'0' || value[i] > L'9' || parsed > (maximum - (value[i] - L'0')) / 10)
            return false;
        parsed = parsed * 10 + (value[i] - L'0');
    }
    if (parsed < minimum || parsed > maximum)
        return false;
    result = parsed;
    return true;
}

} // namespace actioneditor

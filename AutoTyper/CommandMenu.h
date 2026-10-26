#pragma once
#include "AppDialogs.h"

namespace dialogs {
struct MenuCommand {
    UINT id;
    const wchar_t* title;
    const wchar_t* description;
    bool enabled = true;
};

template <size_t Count>
UINT commandMenu(HWND owner, HWND anchor, const Style& style, const MenuCommand (&commands)[Count],
    decltype(&TrackPopupMenuEx) track = &TrackPopupMenuEx) {
    HMENU menu = CreatePopupMenu();
    if (!menu)
        return 0;
    HBRUSH brush = CreateSolidBrush(style.background);
    MENUINFO info{sizeof(info)};
    info.fMask = MIM_BACKGROUND;
    info.hbrBack = brush;
    SetMenuInfo(menu, &info);
    for (size_t i = 0; i < Count; ++i) {
        MENUITEMINFOW item{sizeof(item)};
        item.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STRING | MIIM_STATE;
        item.fType = MFT_OWNERDRAW;
        item.wID = commands[i].id;
        item.fState = commands[i].enabled ? MFS_ENABLED : MFS_DISABLED;
        item.dwTypeData = const_cast<wchar_t*>(commands[i].title);
        InsertMenuItemW(menu, static_cast<UINT>(i), TRUE, &item);
    }
    RECT rect{};
    GetWindowRect(anchor, &rect);
    const UINT selected =
        track(menu, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN, rect.right, rect.bottom, owner, nullptr);
    DestroyMenu(menu);
    DeleteObject(brush);
    return selected;
}

template <size_t Count> const MenuCommand* findCommand(UINT id, const MenuCommand (&commands)[Count]) {
    for (const auto& command : commands)
        if (id == command.id)
            return &command;
    return nullptr;
}

template <size_t Count>
bool measureCommand(const Style& style, MEASUREITEMSTRUCT& item, const MenuCommand (&commands)[Count]) {
    if (item.CtlType != ODT_MENU || !findCommand(item.itemID, commands))
        return false;
    item.itemWidth = MulDiv(250, style.dpi, 96);
    item.itemHeight = MulDiv(52, style.dpi, 96);
    return true;
}

template <size_t Count>
bool drawCommand(const Style& style, const DRAWITEMSTRUCT& item, const MenuCommand (&commands)[Count]) {
    if (item.CtlType != ODT_MENU)
        return false;
    const auto* command = findCommand(item.itemID, commands);
    if (!command)
        return false;
    const bool selected = (item.itemState & ODS_SELECTED) != 0;
    HBRUSH brush = CreateSolidBrush(selected ? style.field : style.background);
    FillRect(item.hDC, &item.rcItem, brush);
    DeleteObject(brush);
    RECT rect = item.rcItem;
    rect.left += MulDiv(14, style.dpi, 96);
    rect.top += MulDiv(6, style.dpi, 96);
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, item.itemState & ODS_DISABLED ? style.muted
                           : selected                    ? style.accent
                                                         : style.text);
    const auto old = SelectObject(item.hDC, style.font);
    DrawTextW(item.hDC, command->title, -1, &rect, DT_SINGLELINE | DT_NOPREFIX);
    rect.top += MulDiv(22, style.dpi, 96);
    SetTextColor(item.hDC, style.muted);
    DrawTextW(item.hDC, command->description, -1, &rect, DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(item.hDC, old);
    return true;
}
} // namespace dialogs

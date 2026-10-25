#pragma once
#include <Windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <string>
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Uuid.lib")

namespace startup {
using Microsoft::WRL::ComPtr;

struct Shortcut {
    std::wstring target, arguments;
    int show = SW_SHOWNORMAL;
};

inline std::wstring executablePath() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, &path[0], static_cast<DWORD>(path.size()));
    if (!length || length >= path.size())
        return {};
    path.resize(length);
    return path;
}

inline std::wstring shortcutPath() {
    PWSTR folder = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Startup, KF_FLAG_DEFAULT, nullptr, &folder)))
        return {};
    const std::wstring path = std::wstring(folder) + L"\\MK AutoTyper.lnk";
    CoTaskMemFree(folder);
    return path;
}

inline bool readShortcut(const std::wstring& path, Shortcut& value) {
    ComPtr<IShellLinkW> link;
    ComPtr<IPersistFile> file;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) ||
        FAILED(link.As(&file)) || FAILED(file->Load(path.c_str(), STGM_READ)))
        return false;
    constexpr int BufferCharacters = 32768;
    std::wstring target(BufferCharacters, L'\0'), arguments(BufferCharacters, L'\0');
    if (FAILED(link->GetPath(&target[0], BufferCharacters, nullptr, SLGP_RAWPATH)) ||
        FAILED(link->GetArguments(&arguments[0], BufferCharacters)) || FAILED(link->GetShowCmd(&value.show)))
        return false;
    value.target = target.c_str();
    value.arguments = arguments.c_str();
    return true;
}

inline bool writeShortcut(const std::wstring& path, const std::wstring& executable) {
    if (path.empty() || executable.empty())
        return false;
    ComPtr<IShellLinkW> link;
    ComPtr<IPersistFile> file;
    const std::wstring directory = executable.substr(0, executable.find_last_of(L"\\/"));
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) ||
        FAILED(link->SetPath(executable.c_str())) || FAILED(link->SetArguments(L"/tray")) ||
        FAILED(link->SetWorkingDirectory(directory.c_str())) ||
        FAILED(link->SetDescription(L"Start MK AutoTyper in the system tray")) ||
        FAILED(link->SetShowCmd(SW_SHOWMINNOACTIVE)) || FAILED(link.As(&file)))
        return false;
    const std::wstring temporary = path + L".tmp";
    if (FAILED(file->Save(temporary.c_str(), TRUE))) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return true;
    DeleteFileW(temporary.c_str());
    return false;
}

inline bool removeShortcut(const std::wstring& path) {
    if (path.empty())
        return false;
    return DeleteFileW(path.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND;
}

inline bool enabled() {
    const std::wstring path = shortcutPath(), executable = executablePath();
    if (path.empty() || executable.empty())
        return false;
    Shortcut value;
    return readShortcut(path, value) && _wcsicmp(value.target.c_str(), executable.c_str()) == 0 &&
           value.arguments == L"/tray";
}

inline bool setEnabled(bool enable) {
    const std::wstring path = shortcutPath();
    return enable ? writeShortcut(path, executablePath()) : removeShortcut(path);
}
} // namespace startup

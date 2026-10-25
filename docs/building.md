# Building

## Prerequisites

- Windows and Visual Studio with **Desktop development with C++**.
- MSVC v143 and a Windows 10/11 SDK for the project defaults.
- PowerShell 5.1 or later for packaging and test scripts.

The app is C++14/Win32, using Windows SDK, GDI, Rich Edit, Shell, DWM and CNG APIs. There is no browser runtime or third-party application dependency.

## Compile

Open `AutoTyper.sln`, select Release and x64/x86, then Build. The solution's x86 configuration maps to the project's Win32 configuration.

From a developer command prompt:

```bat
msbuild AutoTyper.sln /m /p:Configuration=Release /p:Platform=x64
msbuild AutoTyper.sln /m /p:Configuration=Release /p:Platform=x86
```

Use `/p:PlatformToolset=...` for another compatible installed toolset. Release configurations link the C++ runtime statically and omit development debug symbols. Debug configurations remain available for source debugging.

## Package a release

```powershell
.\scripts\Build-Release.ps1
```

With an installed v145 toolchain:

```powershell
.\scripts\Build-Release.ps1 -PlatformToolset v145
```

The script runs tests, builds both architectures in isolated staging, checks version metadata and produces direct EXEs, ZIP bundles, a source archive, the license, release notes and checksums in `artifacts\v2.0`. It does not commit, tag, upload or publish. See [releasing](releasing.md).

## Tests and formatting

```powershell
powershell -ExecutionPolicy Bypass -File tests\Run-KeyboardTests.ps1
powershell -ExecutionPolicy Bypass -File tests\Run-UiTests.ps1
```

Tests compile with `/W4 /WX`, use disposable storage/settings and avoid real clipboard access or desktop typing. See [testing](testing.md).

Use `.clang-format` for changed C++ files:

```bat
clang-format -i AutoTyper\ChangedFile.h
clang-format --dry-run --Werror AutoTyper\ChangedFile.h
```

AI-assisted development is used in this project. Contributions remain subject to the same review, testing and licensing requirements.

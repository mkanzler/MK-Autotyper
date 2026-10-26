$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) { throw 'Install Visual Studio C++ desktop build tools and a Windows SDK.' }
$build = Join-Path $project 'build\ui-tests'
New-Item -ItemType Directory -Force $build | Out-Null
$developer = Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat'
$command = Join-Path $build 'run.cmd'
@"
@echo off
call "$developer" >nul
if errorlevel 1 exit /b 1
cl /nologo /EHsc /W4 /WX /std:c++14 /DUNICODE /D_UNICODE "$PSScriptRoot\SnippetDocumentTests.cpp" /Fe:"$build\SnippetDocumentTests.exe" /Fo:"$build\SnippetDocumentTests.obj" user32.lib
if errorlevel 1 exit /b 1
"$build\SnippetDocumentTests.exe"
if errorlevel 1 exit /b 1
cl /nologo /EHsc /W4 /WX /std:c++14 /DUNICODE /D_UNICODE "$PSScriptRoot\SettingsTests.cpp" /Fe:"$build\SettingsTests.exe" /Fo:"$build\SettingsTests.obj" advapi32.lib
if errorlevel 1 exit /b 1
"$build\SettingsTests.exe"
if errorlevel 1 exit /b 1
cl /nologo /EHsc /W4 /WX /std:c++14 /DUNICODE /D_UNICODE "$PSScriptRoot\StartupTests.cpp" /Fe:"$build\StartupTests.exe" /Fo:"$build\StartupTests.obj"
if errorlevel 1 exit /b 1
"$build\StartupTests.exe"
if errorlevel 1 exit /b 1
cl /nologo /EHsc /W4 /WX /std:c++14 /DUNICODE /D_UNICODE "$PSScriptRoot\FeatureTests.cpp" /Fe:"$build\FeatureTests.exe" /Fo:"$build\FeatureTests.obj" user32.lib gdi32.lib advapi32.lib
if errorlevel 1 exit /b 1
"$build\FeatureTests.exe"
if errorlevel 1 exit /b 1
cl /nologo /EHsc /W4 /WX /std:c++14 /DUNICODE /D_UNICODE "$PSScriptRoot\EncryptedTests.cpp" /Fe:"$build\EncryptedTests.exe" /Fo:"$build\EncryptedTests.obj" user32.lib
if errorlevel 1 exit /b 1
"$build\EncryptedTests.exe"
if errorlevel 1 exit /b 1
cl /nologo /EHsc /W4 /WX /std:c++14 /DUNICODE /D_UNICODE "$PSScriptRoot\VaultSessionTests.cpp" /Fe:"$build\VaultSessionTests.exe" /Fo:"$build\VaultSessionTests.obj" user32.lib
if errorlevel 1 exit /b 1
"$build\VaultSessionTests.exe"
if errorlevel 1 exit /b 1
cl /nologo /EHsc /W4 /WX /std:c++14 /DUNICODE /D_UNICODE "$PSScriptRoot\UiTests.cpp" /Fe:"$build\UiTests.exe" /Fo:"$build\UiTests.obj" user32.lib gdi32.lib shell32.lib /link /SUBSYSTEM:CONSOLE
if errorlevel 1 exit /b 1
"$build\UiTests.exe"
if errorlevel 1 exit /b 1
cl /nologo /EHsc /W4 /WX /std:c++14 /DUNICODE /D_UNICODE "$PSScriptRoot\ActionUiTests.cpp" /Fe:"$build\ActionUiTests.exe" /Fo:"$build\ActionUiTests.obj" user32.lib gdi32.lib shell32.lib /link /SUBSYSTEM:CONSOLE
if errorlevel 1 exit /b 1
"$build\ActionUiTests.exe"
"@ | Set-Content -LiteralPath $command -Encoding ascii
& $env:ComSpec /d /c $command
if ($LASTEXITCODE) { throw "UI/settings tests failed ($LASTEXITCODE)." }

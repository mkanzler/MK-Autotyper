$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) { throw 'Install Visual Studio C++ desktop build tools and a Windows SDK.' }
$build = Join-Path $project 'build\keyboard-tests'
New-Item -ItemType Directory -Force $build | Out-Null
$developer = Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat'
$source = Join-Path $project 'tests\KeyboardInputTests.cpp'
$testExe = Join-Path $build 'KeyboardInputTests.exe'
$testObject = Join-Path $build 'KeyboardInputTests.obj'
# Use a batch file so the configured Visual Studio compiler and DLLs share an environment.
$command = Join-Path $build 'run.cmd'
@"
@echo off
call "$developer" >nul
if errorlevel 1 exit /b 1
cl /nologo /EHsc /W4 /WX /std:c++14 /DUNICODE /D_UNICODE "$source" /Fe:"$testExe" /Fo:"$testObject" user32.lib
if errorlevel 1 exit /b 1
"$testExe"
if errorlevel 1 exit /b 1
cl /nologo /EHsc /W4 /WX /std:c++14 /DUNICODE /D_UNICODE "$PSScriptRoot\ActionTests.cpp" /Fe:"$build\ActionTests.exe" /Fo:"$build\ActionTests.obj" user32.lib
if errorlevel 1 exit /b 1
"$build\ActionTests.exe"
if errorlevel 1 exit /b 1
cl /nologo /EHsc /W4 /WX /std:c++14 /DUNICODE /D_UNICODE "$PSScriptRoot\TypingRuntimeTests.cpp" /Fe:"$build\TypingRuntimeTests.exe" /Fo:"$build\TypingRuntimeTests.obj" user32.lib
if errorlevel 1 exit /b 1
"$build\TypingRuntimeTests.exe"
"@ | Set-Content -LiteralPath $command -Encoding ascii
& $env:ComSpec /d /c $command
if ($LASTEXITCODE) { throw "Keyboard tests failed ($LASTEXITCODE)." }

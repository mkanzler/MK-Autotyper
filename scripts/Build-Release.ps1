[CmdletBinding()]
param(
    [string]$OutputDirectory,
    [ValidatePattern('^v[0-9]+$')][string]$PlatformToolset,
    [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$version = '2.0'
$fileVersion = '2.0.0.0'
$license = Join-Path $project 'LICENSE'
if (!(Test-Path -LiteralPath $license)) { throw 'The GPLv3 LICENSE file is required for distribution.' }
if (!$OutputDirectory) { $OutputDirectory = Join-Path $project "artifacts\v$version" }
if (![IO.Path]::IsPathRooted($OutputDirectory)) { $OutputDirectory = Join-Path $project $OutputDirectory }
$destination = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $destination) { throw "Output already exists: $destination. Choose a fresh -OutputDirectory." }

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio C++ desktop build tools and a Windows SDK.' }
$installation = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or !$installation) { throw 'Visual Studio C++ desktop build tools were not found.' }
$msbuild = Join-Path $installation 'MSBuild\Current\Bin\MSBuild.exe'
$dumpbin = Get-ChildItem -LiteralPath (Join-Path $installation 'VC\Tools\MSVC') -Directory |
    Sort-Object Name -Descending | ForEach-Object {
        $candidate = Join-Path $_.FullName 'bin\Hostx64\x64\dumpbin.exe'
        if (Test-Path -LiteralPath $candidate) { $candidate }
    } | Select-Object -First 1
if (!(Test-Path -LiteralPath $msbuild) -or !$dumpbin) { throw 'MSBuild or dumpbin was not found.' }

if (!$SkipTests) {
    foreach ($suite in @('Run-KeyboardTests.ps1', 'Run-UiTests.ps1')) {
        & (Join-Path $project "tests\$suite")
        if (!$?) { throw "Test suite failed: $suite" }
    }
}

New-Item -ItemType Directory -Path $destination | Out-Null
$staging = Join-Path $destination '_staging'
New-Item -ItemType Directory -Path $staging | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
$utf8 = New-Object Text.UTF8Encoding($false)
Copy-Item -LiteralPath $license -Destination $destination
$verification = New-Object 'System.Collections.Generic.List[string]'
$verification.Add("MK AutoTyper v$version")
$verification.Add("File/product version: $fileVersion")
$verification.Add('Tests: ' + $(if ($SkipTests) { 'skipped by explicit option' } else { 'keyboard/runtime and UI/storage/encryption suites passed' }))

foreach ($architecture in @('x64', 'x86')) {
    $binaryDirectory = Join-Path $staging "bin\$architecture"
    $objectDirectory = Join-Path $staging "obj\$architecture"
    New-Item -ItemType Directory -Force -Path $binaryDirectory, $objectDirectory | Out-Null
    $arguments = @((Join-Path $project 'AutoTyper.sln'), '/m', '/t:Rebuild', '/nologo', '/v:minimal',
        '/p:Configuration=Release', "/p:Platform=$architecture",
        ('/p:OutDir=' + $binaryDirectory.Replace('\', '/') + '/'),
        ('/p:IntDir=' + $objectDirectory.Replace('\', '/') + '/'))
    if ($PlatformToolset) { $arguments += "/p:PlatformToolset=$PlatformToolset" }
    & $msbuild @arguments
    if ($LASTEXITCODE -ne 0) { throw "Release build failed: $architecture" }
    $executable = Join-Path $binaryDirectory 'MK-AutoTyper.exe'
    $info = (Get-Item -LiteralPath $executable).VersionInfo
    if ($info.FileVersion -ne $fileVersion -or $info.ProductVersion -ne $fileVersion) { throw "Incorrect version metadata: $architecture" }
    $bytes = [IO.File]::ReadAllBytes($executable)
    $pe = [BitConverter]::ToInt32($bytes, 0x3c)
    $machine = [BitConverter]::ToUInt16($bytes, $pe + 4)
    $expected = if ($architecture -eq 'x64') { 0x8664 } else { 0x14c }
    if ($machine -ne $expected) { throw "Incorrect executable architecture: $architecture" }
    $imports = & $dumpbin /nologo /dependents $executable
    if ($LASTEXITCODE -ne 0) { throw "Import inspection failed: $architecture" }
    if (($imports -join "`n") -match '(?i)(VCRUNTIME[0-9_]*|MSVCP[0-9_]*|CONCRT[0-9_]*)\.dll') {
        throw "Unexpected external C++ runtime dependency: $architecture"
    }
    $headers = & $dumpbin /nologo /headers $executable
    if ($LASTEXITCODE -ne 0) { throw "PE header inspection failed: $architecture" }
    # Linker optimization records are harmless; CodeView records can expose PDB paths.
    if (($headers -join "`n") -match '(?i)\b(cv|CodeView|RSDS|NB10)\b|\.pdb\b') {
        throw "Development symbols or PDB reference found: $architecture"
    }
    $licenseBytes = [IO.File]::ReadAllBytes($license)
    $binaryText = [Text.Encoding]::UTF8.GetString($bytes)
    if (!$binaryText.Contains([Text.Encoding]::UTF8.GetString($licenseBytes))) {
        throw "Embedded GPLv3 license is missing: $architecture"
    }
    Copy-Item -LiteralPath $executable -Destination (Join-Path $destination "MK-AutoTyper-v$version-windows-$architecture.exe")

    $package = Join-Path $staging "package-$architecture"
    New-Item -ItemType Directory -Path $package | Out-Null
    Copy-Item -LiteralPath $executable -Destination (Join-Path $package 'MK-AutoTyper.exe')
    $quickStart = @"
MK AutoTyper v$version - Windows $architecture

Extract this folder and run MK-AutoTyper.exe. No installation is required.
Windows 10 version 1607 or later, or Windows 11, is required.

Enter text, choose delays, click Start typing, then focus the destination.
Ctrl+Alt+V types clipboard text or stops an active session.
Ctrl+Alt+P searches saved snippets. Both shortcuts are configurable.
Closing the window keeps the app in the tray; right-click its icon to Exit.

User guide: https://github.com/mkanzler/MK-Autotyper/blob/release-$version/docs/user-guide.md
Release notes: https://github.com/mkanzler/MK-Autotyper/releases/tag/release-$version

Copyright (C) 2024-2026 Marcel Kanzler.
Licensed under GNU GPL version 3 only. See LICENSE for the full terms.
This is free software, provided without any warranty.
Corresponding source: https://github.com/mkanzler/MK-Autotyper/releases/download/release-$version/MK-AutoTyper-v$version-source.zip
"@
    [IO.File]::WriteAllText((Join-Path $package 'QUICKSTART.txt'), $quickStart + "`r`n", $utf8)
    Copy-Item -LiteralPath $license -Destination $package
    $innerHashes = Get-ChildItem -LiteralPath $package -File | Sort-Object Name | ForEach-Object {
        (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $_.Name
    }
    [IO.File]::WriteAllLines((Join-Path $package 'SHA256SUMS.txt'), [string[]]$innerHashes, $utf8)
    $archive = Join-Path $destination "MK-AutoTyper-v$version-windows-$architecture.zip"
    [IO.Compression.ZipFile]::CreateFromDirectory($package, $archive, [IO.Compression.CompressionLevel]::Optimal, $false)
    $verification.Add("$architecture executable SHA-256: " + (Get-FileHash -LiteralPath $executable).Hash.ToLowerInvariant())
    $verification.Add("$architecture version, PE architecture, static runtime imports, embedded GPLv3 and absence of CodeView/PDB references verified.")
}

$source = Join-Path $staging 'source'
New-Item -ItemType Directory -Path $source | Out-Null
$sourceFiles = @('AutoTyper.sln', 'README.md', 'CHANGELOG.md', 'LICENSE', '.clang-format', '.gitignore') |
    ForEach-Object { Join-Path $project $_ } | Where-Object { Test-Path -LiteralPath $_ }
$extensions = @('.cpp', '.h', '.rc', '.ico', '.svg', '.vcxproj', '.filters', '.ps1', '.md', '.png', '.jpg')
foreach ($directory in @('AutoTyper', 'tests', 'docs', 'scripts')) {
    $sourceFiles += Get-ChildItem -LiteralPath (Join-Path $project $directory) -File -Recurse | Where-Object {
        $_.Extension -in $extensions -and $_.FullName.Substring($project.Length + 1) -notmatch '(?i)(^|[\\/])(build|artifacts|x64|Debug|Release|\.vs)[\\/]'
    } | Select-Object -ExpandProperty FullName
}
foreach ($file in $sourceFiles) {
    $relative = $file.Substring($project.Length + 1)
    $target = Join-Path $source $relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
    Copy-Item -LiteralPath $file -Destination $target
}
[IO.Compression.ZipFile]::CreateFromDirectory($source, (Join-Path $destination "MK-AutoTyper-v$version-source.zip"), [IO.Compression.CompressionLevel]::Optimal, $false)
Copy-Item -LiteralPath (Join-Path $project "docs\releases\v$version.md") -Destination (Join-Path $destination 'RELEASE-NOTES.md')
$hashes = Get-ChildItem -LiteralPath $destination -File | Where-Object { $_.Extension -in @('.zip', '.exe') -or $_.Name -eq 'LICENSE' } | Sort-Object Name | ForEach-Object {
    (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $_.Name
}
[IO.File]::WriteAllLines((Join-Path $destination 'SHA256SUMS.txt'), [string[]]$hashes, $utf8)
$verification.Add('Packaging is local only; no Git commit, tag, upload or publication was performed.')
[IO.File]::WriteAllLines((Join-Path $destination 'BUILD-INFO.txt'), $verification.ToArray(), $utf8)
Write-Output "Release prepared in $destination"

param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [string]$BuildDirectory = "",
    [string]$Destination = "",
    [string]$QtRoot = "E:\Qt\Qt5.12.8\5.12.8\msvc2017_64"
)

$ErrorActionPreference = "Stop"
$clientRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repositoryRoot = (Resolve-Path (Join-Path $clientRoot "..\..")).Path

if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $repositoryRoot "build\qt-client"
}
if ([string]::IsNullOrWhiteSpace($Destination)) {
    $Destination = Join-Path $repositoryRoot "dist\NexusIMClient-$Configuration"
}

$executable = Join-Path $BuildDirectory "bin\$Configuration\NexusIMClient.exe"
$deployTool = Join-Path $QtRoot "bin\windeployqt.exe"
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "NexusIMClient.exe not found: $executable. Build the requested configuration first."
}
if (-not (Test-Path -LiteralPath $deployTool -PathType Leaf)) {
    throw "windeployqt.exe not found: $deployTool"
}
if (Test-Path -LiteralPath $Destination) {
    throw "Destination already exists: $Destination. Choose a new path or remove it explicitly."
}

if ([string]::IsNullOrWhiteSpace($env:VCINSTALLDIR)) {
    $vsDeveloperShell = "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\Tools\VsDevCmd.bat"
    if (-not (Test-Path -LiteralPath $vsDeveloperShell -PathType Leaf)) {
        throw "VS2019 developer environment was not found: $vsDeveloperShell"
    }
    $environmentLines = & $env:ComSpec /d /c `
        "call `"$vsDeveloperShell`" -arch=x64 -host_arch=x64 >nul && set"
    if ($LASTEXITCODE -ne 0) {
        throw "Could not load the VS2019 x64 developer environment."
    }
    foreach ($line in $environmentLines) {
        $separator = $line.IndexOf('=')
        if ($separator -gt 0) {
            $name = $line.Substring(0, $separator)
            $value = $line.Substring($separator + 1)
            Set-Item -Path "Env:$name" -Value $value
        }
    }
}

New-Item -ItemType Directory -Path $Destination | Out-Null
Copy-Item -LiteralPath $executable -Destination $Destination

$deployArguments = @(
    "--$($Configuration.ToLowerInvariant())",
    "--no-translations",
    "--compiler-runtime",
    (Join-Path $Destination "NexusIMClient.exe")
)
& $deployTool @deployArguments
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}

$crtDirectory = Join-Path $env:VCToolsRedistDir "x64\Microsoft.VC142.CRT"
if (-not (Test-Path -LiteralPath $crtDirectory -PathType Container)) {
    throw "MSVC x64 redistributable directory was not found: $crtDirectory"
}
Get-ChildItem -LiteralPath $crtDirectory -Filter "*.dll" -File |
    Copy-Item -Destination $Destination

$platformPlugin = Join-Path $Destination "platforms\qwindows.dll"
$coreLibrary = Join-Path $Destination "Qt5Core.dll"
$compilerRuntime = Join-Path $Destination "vcruntime140.dll"
if (-not (Test-Path -LiteralPath $platformPlugin -PathType Leaf) -or
    -not (Test-Path -LiteralPath $coreLibrary -PathType Leaf) -or
    -not (Test-Path -LiteralPath $compilerRuntime -PathType Leaf)) {
    throw "Package validation failed: Qt runtime, MSVC runtime, or qwindows plugin is missing."
}

Write-Host "Package ready: $Destination"

param(
    [string]$Version = "1.0.0-stage7",
    [string]$QtRoot = "E:\Qt\Qt5.12.8\5.12.8\msvc2017_64",
    [string]$BuildDirectory = "",
    [string]$OutputRoot = "",
    [string]$CMakeExecutable = ""
)

$ErrorActionPreference = "Stop"
$clientRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repositoryRoot = (Resolve-Path (Join-Path $clientRoot "..\..")).Path

if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $repositoryRoot "build\qt-client-stage7"
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $repositoryRoot "dist"
}
if ([string]::IsNullOrWhiteSpace($CMakeExecutable)) {
    $cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($null -ne $cmakeCommand) {
        $CMakeExecutable = $cmakeCommand.Source
    } else {
        $vsCMake = "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        if (Test-Path -LiteralPath $vsCMake -PathType Leaf) {
            $CMakeExecutable = $vsCMake
        }
    }
    if ([string]::IsNullOrWhiteSpace($CMakeExecutable)) {
        throw "cmake.exe was not found in PATH; pass -CMakeExecutable explicitly."
    }
}
$CTestExecutable = Join-Path (Split-Path -Parent $CMakeExecutable) "ctest.exe"
if (-not (Test-Path -LiteralPath $CTestExecutable -PathType Leaf)) {
    throw "ctest.exe was not found next to CMake: $CTestExecutable"
}

$destination = Join-Path $OutputRoot "NexusIMClient-$Version-win64"
$archive = "$destination.zip"
$archiveHash = "$archive.sha256"
foreach ($path in @($destination, $archive, $archiveHash)) {
    if (Test-Path -LiteralPath $path) {
        throw "Release output already exists: $path. Remove it explicitly or choose another version."
    }
}

& $CMakeExecutable -S $clientRoot -B $BuildDirectory `
    -G "Visual Studio 16 2019" -A x64 `
    "-DCMAKE_PREFIX_PATH=$QtRoot" `
    -DNEXUSIM_QT_BUILD_TESTS=ON
if ($LASTEXITCODE -ne 0) {
    throw "Qt client CMake configure failed with exit code $LASTEXITCODE"
}

& $CMakeExecutable --build $BuildDirectory --config Release --parallel
if ($LASTEXITCODE -ne 0) {
    throw "Qt client Release build failed with exit code $LASTEXITCODE"
}

$qtBin = Join-Path $QtRoot "bin"
if (-not (Test-Path -LiteralPath (Join-Path $qtBin "Qt5Core.dll") -PathType Leaf)) {
    throw "Qt runtime directory is incomplete: $qtBin"
}
$previousPath = $env:PATH
try {
    $env:PATH = "$qtBin;$previousPath"
    & $CTestExecutable --test-dir $BuildDirectory -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        throw "Qt client tests failed with exit code $LASTEXITCODE"
    }
} finally {
    $env:PATH = $previousPath
}

& python.exe (Join-Path $clientRoot "tests\source_guards.py")
if ($LASTEXITCODE -ne 0) {
    throw "Qt source guards failed with exit code $LASTEXITCODE"
}

& (Join-Path $clientRoot "package.ps1") `
    -Configuration Release `
    -BuildDirectory $BuildDirectory `
    -Destination $destination `
    -QtRoot $QtRoot

$notice = Join-Path $repositoryRoot "THIRD_PARTY_NOTICES.md"
if (Test-Path -LiteralPath $notice -PathType Leaf) {
    Copy-Item -LiteralPath $notice -Destination $destination
}

$releaseNotes = @"
NexusIM Windows Client $Version

Runtime: Windows x64, Qt 5.12.8, MSVC/VS2019.
Default endpoint: 127.0.0.1:8000.
Test-PC endpoint: 192.168.73.128:8000.

The endpoint can be changed in the login Settings dialog or with:
  NexusIMClient.exe --host <server-ip> --port 8000

This V1 build has no TLS. Use it only on the isolated test network and do not
expose port 8000 directly to the public Internet.
"@
Set-Content -LiteralPath (Join-Path $destination "README.txt") `
    -Value $releaseNotes -Encoding UTF8

$manifestPath = Join-Path $destination "SHA256SUMS.txt"
$manifestLines = Get-ChildItem -LiteralPath $destination -Recurse -File |
    Where-Object { $_.FullName -ne $manifestPath } |
    Sort-Object FullName |
    ForEach-Object {
        $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash.ToLowerInvariant()
        $relative = $_.FullName.Substring($destination.Length + 1).Replace('\', '/')
        "$hash  $relative"
    }
Set-Content -LiteralPath $manifestPath -Value $manifestLines -Encoding ASCII

Compress-Archive -Path (Join-Path $destination "*") -DestinationPath $archive `
    -CompressionLevel Optimal
$zipHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $archive).Hash.ToLowerInvariant()
Set-Content -LiteralPath $archiveHash `
    -Value "$zipHash  $(Split-Path -Leaf $archive)" -Encoding ASCII

Write-Host "Release directory: $destination"
Write-Host "Release archive:   $archive"
Write-Host "Archive checksum:  $archiveHash"

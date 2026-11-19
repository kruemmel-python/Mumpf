$ErrorActionPreference = "Stop"

$Root = "H:\mumpf_for_Windows"
$TmpDir = Join-Path $Root ".tmp"
$BuildDir = Join-Path $Root "build"

New-Item -ItemType Directory -Force -Path $TmpDir | Out-Null
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

# Strikt kein Temp/Cache auf C:
$env:TEMP = $TmpDir
$env:TMP = $TmpDir
$env:TMPDIR = $TmpDir

$VsCMake = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if (-not (Test-Path $VsCMake)) {
    $VsCMake = "cmake"
}

Write-Host "Konfiguriere CMake in $BuildDir ..."
& $VsCMake -S $Root -B $BuildDir -G "Visual Studio 17 2022" -A x64

Write-Host "Kompiliere mumpf.exe (Release) ..."
& $VsCMake --build $BuildDir --config Release

$ExePath = Join-Path $BuildDir "Release\mumpf.exe"
if (Test-Path $ExePath) {
    Write-Host "Build erfolgreich: $ExePath" -ForegroundColor Green
} else {
    throw "mumpf.exe wurde nicht erzeugt."
}

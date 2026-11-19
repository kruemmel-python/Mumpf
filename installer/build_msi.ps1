$ErrorActionPreference = "Stop"

$Root = "H:\mumpf_for_Windows"
$Tmp = Join-Path $Root ".tmp"
$Dist = Join-Path $Root "dist"
$Wix = Join-Path $Root ".tools\wix.exe"

New-Item -ItemType Directory -Force -Path $Tmp, $Dist | Out-Null

# Strikt kein Temp/Cache auf C:
$env:TEMP = $Tmp
$env:TMP = $Tmp
$env:TMPDIR = $Tmp
$env:NUGET_PACKAGES = Join-Path $Tmp "nuget"
$env:DOTNET_CLI_HOME = Join-Path $Tmp "dotnet"

if (-not (Test-Path $Wix)) {
    throw "wix.exe nicht gefunden unter $Wix"
}

$OutMsi = Join-Path $Dist "Mumpf-Setup-1.0.0-x64.msi"
Write-Host "Baue MSI-Installer: $OutMsi ..."

& $Wix build (Join-Path $Root "installer\mumpf.wxs") `
    -arch x64 `
    -intermediatefolder (Join-Path $Tmp "wix-obj") `
    -o $OutMsi

if (Test-Path $OutMsi) {
    $sizeMb = [math]::Round((Get-Item $OutMsi).Length / 1MB, 2)
    Write-Host "MSI erfolgreich erstellt: $OutMsi ($sizeMb MB)" -ForegroundColor Green
} else {
    throw "MSI wurde nicht erzeugt."
}

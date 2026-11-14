# build.ps1 - configure (first run) and build both firmwares
#
#   .\build.ps1              build picoboy_gbc + pico_nes
#   .\build.ps1 -Target pico_nes
#   .\build.ps1 -Clean       wipe build/ and reconfigure
#   .\build.ps1 -Dist        also copy the two .uf2 into dist/
#
# SDK lookup order: $env:PICO_SDK_PATH -> Raspberry Pi install folders ->
# %USERPROFILE%\pico-sdk / .pico-sdk. If the SDK folder carries its own
# cmake/ninja/arm-gcc they are added to PATH; otherwise the script relies on
# whatever cmake / ninja / arm-none-eabi-gcc are already on PATH.
#
# Note: cmake writes progress to stderr and PowerShell wraps it as
# NativeCommandError. So this script never uses $ErrorActionPreference = "Stop";
# success is decided by $LASTEXITCODE only, and the log tail is printed on failure.
# Keep this file ASCII-only: PowerShell 5.1 mis-decodes UTF-8 script files.

param(
    [ValidateSet("all", "picoboy_gbc", "pico_nes")]
    [string]$Target = "all",
    [switch]$Clean,
    [switch]$Dist
)

function Test-SdkRoot {
    param([string]$Path)
    if (-not $Path) { return $false }
    return (Test-Path -LiteralPath (Join-Path $Path "pico-sdk"))
}

# ---- locate the Pico SDK ----
$candidates = New-Object System.Collections.Generic.List[string]

# 1) an already exported PICO_SDK_PATH (accepts .../pico-sdk or its parent)
if ($env:PICO_SDK_PATH) {
    $psp = $env:PICO_SDK_PATH.TrimEnd('\', '/')
    $leaf = Split-Path -Leaf $psp
    if (Test-SdkRoot $psp) {
        $candidates.Add($psp)
    } elseif ($leaf -eq "pico-sdk") {
        $parent = Split-Path -Parent $psp
        if (Test-SdkRoot $parent) { $candidates.Add($parent) }
    }
}

# 2) Raspberry Pi installer drop, plus any Pico SDK* folder under it
foreach ($base in @("C:\Program Files\Raspberry Pi", "E:\Program Files\Raspberry Pi")) {
    if (Test-Path -LiteralPath $base) {
        Get-ChildItem -LiteralPath $base -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -like "Pico SDK*" } |
            ForEach-Object { $candidates.Add($_.FullName) }
    }
}

# 3) common manual clones
$candidates.Add("$env:USERPROFILE\pico-sdk")
$candidates.Add("$env:USERPROFILE\.pico-sdk")
$candidates.Add("C:\pico\pico-sdk")
$candidates.Add("E:\pico\pico-sdk")

$sdkRoot = $null
foreach ($c in $candidates) {
    if (Test-SdkRoot $c) { $sdkRoot = $c; break }
}

if (-not $sdkRoot) {
    Write-Host "Pico SDK 1.5.x not found." -ForegroundColor Red
    Write-Host "Install the Pico SDK, or clone pico-sdk and set PICO_SDK_PATH to it, then rerun."
    exit 1
}

$env:PICO_SDK_PATH = Join-Path $sdkRoot "pico-sdk"
Write-Host "Pico SDK: $env:PICO_SDK_PATH"

# ---- prepend SDK-bundled toolchain if present, else trust PATH ----
$toolDirs = @(
    (Join-Path $sdkRoot "cmake\bin"),
    (Join-Path $sdkRoot "ninja"),
    (Join-Path $sdkRoot "gcc-arm-none-eabi\bin")
)
$prepend = @()
foreach ($d in $toolDirs) { if (Test-Path -LiteralPath $d) { $prepend += $d } }
if ($prepend.Count -gt 0) {
    $env:PATH = ($prepend -join ";") + ";" + $env:PATH
}

foreach ($exe in @("cmake", "ninja", "arm-none-eabi-gcc")) {
    $cmd = Get-Command $exe -ErrorAction SilentlyContinue
    if ($cmd) { Write-Host ("  {0}: {1}" -f $exe, $cmd.Source) }
}
if (-not (Get-Command "cmake" -ErrorAction SilentlyContinue)) {
    Write-Host "cmake not found on PATH." -ForegroundColor Red; exit 1
}

Set-Location -LiteralPath $PSScriptRoot

$buildDir = Join-Path $PSScriptRoot "build"
if ($Clean -and (Test-Path -LiteralPath $buildDir)) {
    Remove-Item -LiteralPath $buildDir -Recurse -Force -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $buildDir) {
        Write-Host "warning: build/ not fully removed (a file is locked); continuing." -ForegroundColor Yellow
    }
}
if (-not (Test-Path -LiteralPath $buildDir)) { New-Item -ItemType Directory -Path $buildDir | Out-Null }

function Invoke-Logged {
    param([string]$Label, [string]$LogPath, [scriptblock]$Command)
    Write-Host "== $Label =="
    & $Command 2>&1 | Out-File -FilePath $LogPath -Encoding utf8
    $code = $LASTEXITCODE
    if ($code -ne 0) {
        Write-Host "FAILED (exit $code), last lines of $LogPath" -ForegroundColor Red
        Get-Content -LiteralPath $LogPath | Select-Object -Last 40 | ForEach-Object { Write-Host $_ }
        exit $code
    }
}

if (-not (Test-Path -LiteralPath (Join-Path $buildDir "build.ninja"))) {
    Invoke-Logged "configure" (Join-Path $buildDir "log_configure.txt") { cmake -S . -B build -G Ninja }
}

if ($Target -eq "all") {
    Invoke-Logged "build" (Join-Path $buildDir "log_build.txt") { cmake --build build }
} else {
    Invoke-Logged "build $Target" (Join-Path $buildDir "log_build.txt") { cmake --build build --target $Target }
}

# ROM packing list (produced during configure) - show it, then the uf2 sizes
$cfgLog = Join-Path $buildDir "log_configure.txt"
if (Test-Path -LiteralPath $cfgLog) {
    Get-Content -LiteralPath $cfgLog | Select-String -Pattern "^  \+ |^=> " |
        ForEach-Object { Write-Host $_.Line }
}

Write-Host ""
$uf2s = Get-ChildItem -LiteralPath $buildDir -Filter *.uf2
$uf2s | ForEach-Object { "{0,-18} {1,10:N0} B" -f $_.Name, $_.Length }

if ($Dist) {
    $distDir = Join-Path $PSScriptRoot "dist"
    if (-not (Test-Path -LiteralPath $distDir)) { New-Item -ItemType Directory -Path $distDir | Out-Null }
    $uf2s | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $distDir -Force }
    Write-Host "copied to $distDir"
}

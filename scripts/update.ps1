# Nocturne of Time: pull the latest changes, re-run CMake setup, and build.
#
# Run from PowerShell in the repository folder:
#   .\scripts\update.ps1
# Add -Clean to wipe CMake's saved settings first (use this if setup reports errors).
param(
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
Set-Location (Split-Path -Parent $PSScriptRoot)

Write-Host "== Pulling latest changes" -ForegroundColor Cyan
git pull
if ($LASTEXITCODE -ne 0) { throw "git pull failed" }
git submodule update --init --recursive
if ($LASTEXITCODE -ne 0) { throw "git submodule update failed" }

if ($Clean) {
    Write-Host "== Clearing CMake settings" -ForegroundColor Cyan
    Remove-Item -Force -ErrorAction SilentlyContinue .\build\x64\CMakeCache.txt
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue .\build\x64\CMakeFiles
}

Write-Host "== Running CMake setup" -ForegroundColor Cyan
cmake -S . -B "build/x64" -G "Visual Studio 17 2022" -T v143 -A x64
if ($LASTEXITCODE -ne 0) { throw "CMake setup failed - try: .\scripts\update.ps1 -Clean" }

Write-Host "== Building" -ForegroundColor Cyan
cmake --build .\build\x64
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

Write-Host "== Done. Launch the game with .\x64\Debug\soh.exe" -ForegroundColor Green

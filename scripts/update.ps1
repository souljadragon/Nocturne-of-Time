# Nocturne of Time: pull the latest changes, re-run CMake setup, and build.
#
# Run from PowerShell in the repository folder:
#   powershell -ExecutionPolicy Bypass -File .\scripts\update.ps1
#
# CMake setup always starts from scratch: on Windows, re-running setup over saved settings (CMakeCache.txt)
# failed to find the vcpkg packages (SDL2) every time, while a fresh setup always worked. Compiled files are
# kept, so the build itself stays incremental. (-Clean is still accepted; it is now the default.)
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

Write-Host "== Clearing CMake settings" -ForegroundColor Cyan
Remove-Item -Force -ErrorAction SilentlyContinue .\build\x64\CMakeCache.txt
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue .\build\x64\CMakeFiles
# Downloaded dependencies (spdlog, ImGui...) keep their own small CMake caches; clear those too
Get-ChildItem -Directory -ErrorAction SilentlyContinue .\build\x64\_deps -Filter "*-subbuild" |
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue

Write-Host "== Running CMake setup" -ForegroundColor Cyan
cmake -S . -B "build/x64" -G "Visual Studio 17 2022" -T v143 -A x64
if ($LASTEXITCODE -ne 0) { throw "CMake setup failed - send the red text above to Claude" }

Write-Host "== Building" -ForegroundColor Cyan
cmake --build .\build\x64
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

Write-Host "== Done. Launch the game with .\x64\Debug\soh.exe" -ForegroundColor Green

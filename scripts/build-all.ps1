# One-click build: front-end + back-end + shell → build/run/
# Usage: .\scripts\build-all.ps1 [-Clean]
#   -Clean : clean rebuild (cmake --build --clean-first)

param([switch]$Clean)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $projectRoot "build"
$runDir = Join-Path $buildDir "run"

Write-Host "============================================" -ForegroundColor Magenta
Write-Host " CodePilot One-Click Build" -ForegroundColor Magenta
Write-Host "============================================" -ForegroundColor Magenta

$buildArgs = @("--build", $buildDir)
if ($Clean) {
    $buildArgs += "--clean-first"
}

# ====================================================================
# Step 1 — Build front-end (Vite)
# ====================================================================
Write-Host "`n[1/3] Building front-end..." -ForegroundColor Cyan
$webSrc = Join-Path $projectRoot "frontend\new-web"
Push-Location $webSrc
try {
    npm run build
}
finally {
    Pop-Location
}

# Deploy front-end static files
$webDest = Join-Path $runDir "web"
if (Test-Path $webDest) {
    Remove-Item -Recurse -Force $webDest
}
Copy-Item -Recurse -Force (Join-Path $webSrc "dist") $webDest
Write-Host "  -> Front-end deployed to $webDest" -ForegroundColor Green

# ====================================================================
# Step 2 — Build C++ back-end (codepilot-agent-server)
# ====================================================================
Write-Host "`n[2/3] Building C++ back-end..." -ForegroundColor Cyan
& cmake @buildArgs | ForEach-Object { Write-Host "  $_" }

# ====================================================================
# Step 3 — Install all artifacts to build/run/
# ====================================================================
Write-Host "`n[3/3] Installing to build/run/..." -ForegroundColor Cyan

# Install directly to build/run/ (overrides CMAKE_INSTALL_PREFIX for this run)
$installArgs = @(
    "--install", $buildDir,
    "--prefix", $runDir
)
if ($Clean) {
    # Only remove target binaries on clean build; keep config/storage intact
    Get-ChildItem $runDir -Filter "*.exe" -ErrorAction SilentlyContinue | Remove-Item -Force
    Get-ChildItem $runDir -Filter "*.dll" -ErrorAction SilentlyContinue | Remove-Item -Force
    Get-ChildItem $runDir -Filter "*.lib" -ErrorAction SilentlyContinue | Remove-Item -Force
}

& cmake @installArgs *>&1 | ForEach-Object { Write-Host "  $_" }

if ($LASTEXITCODE -ne 0) {
    Write-Host "`n  !! cmake --install failed with code $LASTEXITCODE" -ForegroundColor Red
    Write-Host "  Falling back to manual copy..." -ForegroundColor Yellow

    # Manual fallback — copy binaries from build output directories
    $srcDirs = @(
        (Join-Path $buildDir "apps\agent-server"),
        (Join-Path $buildDir "apps\webview2-shell"),
        (Join-Path $buildDir "x64\Debug"),
        (Join-Path $buildDir "x64\Release")
    )
    foreach ($srcDir in $srcDirs) {
        if (Test-Path $srcDir) {
            Get-ChildItem $srcDir -Filter "*.exe" -ErrorAction SilentlyContinue |
                Copy-Item -Destination $runDir -Force
            Get-ChildItem $srcDir -Filter "*.dll" -ErrorAction SilentlyContinue |
                Copy-Item -Destination $runDir -Force
        }
    }
    Write-Host "  -> Manual copy complete" -ForegroundColor Green
}

# ====================================================================
# Summary
# ====================================================================
Write-Host "`n============================================" -ForegroundColor Green
Write-Host " Build complete!" -ForegroundColor Green
Write-Host " Output: $runDir" -ForegroundColor Green
Write-Host "============================================" -ForegroundColor Green

Write-Host "`nFiles in build/run/:" -ForegroundColor Cyan
Get-ChildItem $runDir -Filter "*.exe" |
    ForEach-Object { Write-Host "  $($_.Name)" -ForegroundColor White }

Write-Host "`nTo launch: $runDir\codepilot-shell.exe" -ForegroundColor Yellow
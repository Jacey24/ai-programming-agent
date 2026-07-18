# Build frontend and deploy to build/run/web/
# Usage: .\scripts\build-frontend.ps1

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$webSrc = Join-Path $projectRoot "frontend\new-web"
$runDir = Join-Path $projectRoot "build\run\web"

Write-Host ">>> Building frontend..." -ForegroundColor Cyan
Push-Location $webSrc
npm run build
Pop-Location

Write-Host ">>> Deploying to build/run/web..." -ForegroundColor Cyan
if (Test-Path $runDir) {
    Remove-Item -Recurse -Force $runDir
}
Copy-Item -Recurse -Force (Join-Path $webSrc "dist") $runDir

Write-Host ">>> Done! Static files ready at $runDir" -ForegroundColor Green
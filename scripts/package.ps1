# One-click installer packaging: front-end + C++ → staging → ISCC → dist/*.exe
# Usage: .\scripts\package.ps1 [-Version "0.2.0"] [-OutputDir "dist"] [-SkipBuild]
#   -Version   : override version (default: from git describe or CMakeLists.txt)
#   -OutputDir : installer output directory (default: dist)
#   -SkipBuild : skip build steps, only assemble staging + ISCC

param(
    [string]$Version = "",
    [string]$OutputDir = "dist",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $projectRoot "build"
$stagingDir = Join-Path $buildDir "staging"
$resourcesDir = Join-Path $stagingDir "resources"
$distDir = Join-Path $projectRoot $OutputDir

# ─── Version detection ───────────────────────────────────────
if (-not $Version) {
    # Try git describe first (suppress stderr for repos with no tags)
    $gitTag = $null
    try {
        $gitOutput = & git -C $projectRoot describe --tags --abbrev=0 2>&1
        if ($LASTEXITCODE -eq 0 -and $gitOutput) {
            $gitTag = ($gitOutput -join '').Trim()
        }
    }
    catch { }
    
    if ($gitTag) {
        $Version = $gitTag -replace '^v', ''
    }
    else {
        # Fallback: read from CMakeLists.txt
        $cmakeContent = Get-Content (Join-Path $projectRoot "CMakeLists.txt") -Raw
        if ($cmakeContent -match 'VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') {
            $Version = $Matches[1]
        }
        else {
            $Version = "0.1.0"
        }
    }
    # Append timestamp for local prototype builds
    $timestamp = Get-Date -Format "yyyyMMdd-HHmm"
    $Version = "$Version-dev.$timestamp"
}

Write-Host "============================================" -ForegroundColor Magenta
Write-Host " CodePilot Installer Package" -ForegroundColor Magenta
Write-Host " Version: $Version" -ForegroundColor Magenta
Write-Host "============================================" -ForegroundColor Magenta

# ─────────────────────────────────────────────────────────────
# Step 1 — Build front-end
# ─────────────────────────────────────────────────────────────
if (-not $SkipBuild) {
    Write-Host "`n[1/5] Building front-end..." -ForegroundColor Cyan
    $webSrc = Join-Path $projectRoot "frontend\new-web"
    if (-not (Test-Path (Join-Path $webSrc "package.json"))) {
        Write-Host "  ERROR: frontend/new-web/package.json not found!" -ForegroundColor Red
        exit 1
    }
    Push-Location $webSrc
    try {
        npm run build 2>&1 | ForEach-Object { Write-Host "  $_" }
        if ($LASTEXITCODE -ne 0) {
            Write-Host "  ERROR: npm run build failed (exit code $LASTEXITCODE)" -ForegroundColor Red
            exit 1
        }
    }
    finally {
        Pop-Location
    }
    Write-Host "  -> Front-end built" -ForegroundColor Green

    # ─────────────────────────────────────────────────────────
    # Step 2 — CMake configure (ensure correct generator)
    # ─────────────────────────────────────────────────────────
    Write-Host "`n[2/5] Configuring CMake (Visual Studio 17 2022)..." -ForegroundColor Cyan
    $cmakeConfigArgs = @(
        "-S", $projectRoot,
        "-B", $buildDir,
        "-G", "Visual Studio 17 2022",
        "-A", "x64"
    )
    & cmake @cmakeConfigArgs 2>&1 | ForEach-Object { Write-Host "  $_" }
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  ERROR: CMake configure failed (exit code $LASTEXITCODE)" -ForegroundColor Red
        exit 1
    }
    Write-Host "  -> CMake configured" -ForegroundColor Green

    # ─────────────────────────────────────────────────────────
    # Step 3 — CMake build (Release)
    # ─────────────────────────────────────────────────────────
    Write-Host "`n[3/5] Building C++ projects (Release)..." -ForegroundColor Cyan
    & cmake --build $buildDir --config Release 2>&1 | ForEach-Object { Write-Host "  $_" }
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  ERROR: CMake build failed (exit code $LASTEXITCODE)" -ForegroundColor Red
        exit 1
    }
    Write-Host "  -> C++ build complete" -ForegroundColor Green
}
else {
    Write-Host "`n[1-3/5] Skipping build (--SkipBuild)" -ForegroundColor Yellow
}

# ─────────────────────────────────────────────────────────────
# Step 4 — Assemble staging directory
# ─────────────────────────────────────────────────────────────
Write-Host "`n[4/5] Assembling staging directory..." -ForegroundColor Cyan

# Clean staging
if (Test-Path $stagingDir) {
    Remove-Item -Recurse -Force $stagingDir
}

# Create directories
$dirs = @(
    $resourcesDir,
    (Join-Path $stagingDir "config"),
    (Join-Path $stagingDir "web"),
    (Join-Path $stagingDir "storage"),
    (Join-Path $stagingDir "workspace"),
    (Join-Path $stagingDir "logs")
)
foreach ($d in $dirs) {
    New-Item -ItemType Directory -Force -Path $d | Out-Null
}

# ─── Collect binaries from Release output directories ────────
# Visual Studio multi-config puts outputs in build/<subdir>/Release/
$releaseOutputs = @(
    (Join-Path $buildDir "apps\agent-server\Release"),
    (Join-Path $buildDir "apps\webview2-shell\Release"),
    (Join-Path $buildDir "apps\cli-client\Release")
)

$binariesCopied = $false
foreach ($srcDir in $releaseOutputs) {
    if (Test-Path $srcDir) {
        # Copy .exe files
        Get-ChildItem $srcDir -Filter "*.exe" -ErrorAction SilentlyContinue |
            ForEach-Object {
                Copy-Item $_.FullName -Destination $stagingDir -Force
                Write-Host "  -> EXE: $($_.Name)" -ForegroundColor White
                $binariesCopied = $true
            }
        # Copy .dll files
        Get-ChildItem $srcDir -Filter "*.dll" -ErrorAction SilentlyContinue |
            ForEach-Object {
                Copy-Item $_.FullName -Destination $stagingDir -Force
                Write-Host "  -> DLL: $($_.Name)" -ForegroundColor DarkGray
            }
    }
}

if (-not $binariesCopied) {
    Write-Host "  ERROR: No .exe files found in Release output dirs!" -ForegroundColor Red
    Write-Host "  Checked paths:" -ForegroundColor Yellow
    foreach ($srcDir in $releaseOutputs) {
        Write-Host "    $srcDir" -ForegroundColor Yellow
    }
    Write-Host "  Try running the full build first: .\scripts\package.ps1 (without --SkipBuild)" -ForegroundColor Yellow
    exit 1
}

# ─── Copy config files ───────────────────────────────────────
$configSrc = Join-Path $projectRoot "config"
if (Test-Path $configSrc) {
    Copy-Item (Join-Path $configSrc "*") -Destination (Join-Path $stagingDir "config") -Force -Exclude "llm.local.json"
    Write-Host "  -> Config files copied" -ForegroundColor White
}

# ─── Copy front-end static files ─────────────────────────────
$webDist = Join-Path $projectRoot "frontend\new-web\dist"
if (Test-Path $webDist) {
    Copy-Item (Join-Path $webDist "*") -Destination (Join-Path $stagingDir "web") -Recurse -Force
    Write-Host "  -> Front-end web assets copied" -ForegroundColor White
}
else {
    Write-Host "  WARNING: frontend/new-web/dist not found. Run build first!" -ForegroundColor Yellow
}

# ─── Create placeholder files ─────────────────────────────────
"" | Out-File -FilePath (Join-Path $stagingDir "storage\.gitkeep") -Encoding ascii
"" | Out-File -FilePath (Join-Path $stagingDir "workspace\.gitkeep") -Encoding ascii
"" | Out-File -FilePath (Join-Path $stagingDir "logs\.gitkeep") -Encoding ascii

# ─── Generate app.ico placeholder ─────────────────────────────
$icoPath = Join-Path $resourcesDir "app.ico"
if (-not (Test-Path $icoPath)) {
    # Try to extract icon from codepilot-shell.exe first
    $shellExe = Join-Path $stagingDir "codepilot-shell.exe"
    if (Test-Path $shellExe) {
        try {
            Add-Type -AssemblyName System.Drawing
            $icon = [System.Drawing.Icon]::ExtractAssociatedIcon($shellExe)
            if ($icon) {
                $fs = [System.IO.File]::Create($icoPath)
                $icon.Save($fs)
                $fs.Close()
                $icon.Dispose()
                Write-Host "  -> Icon extracted from codepilot-shell.exe" -ForegroundColor White
            }
        }
        catch {
            Write-Host "  -> Icon extraction failed, using generated placeholder" -ForegroundColor Yellow
        }
    }
    
    # If extraction failed or no icon yet, generate a simple one
    if (-not (Test-Path $icoPath)) {
        try {
            Add-Type -AssemblyName System.Drawing
            $bitmap = New-Object System.Drawing.Bitmap(32, 32)
            $g = [System.Drawing.Graphics]::FromImage($bitmap)
            $g.Clear([System.Drawing.Color]::FromArgb(30, 30, 46))
            $brush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(99, 102, 241))
            $g.FillEllipse($brush, 4, 4, 24, 24)
            $font = New-Object System.Drawing.Font("Consolas", 12, [System.Drawing.FontStyle]::Bold)
            $textBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::White)
            $g.DrawString("C", $font, $textBrush, 9, 5)
            $g.Dispose()
            $bitmap.Save($icoPath, [System.Drawing.Imaging.ImageFormat]::Icon)
            $bitmap.Dispose()
            Write-Host "  -> Placeholder icon generated" -ForegroundColor White
        }
        catch {
            Write-Host "  WARNING: Could not generate icon: $_" -ForegroundColor Yellow
            Write-Host "  Installer will build without custom icon." -ForegroundColor Yellow
        }
    }
}

Write-Host "  -> Staging assembled at: $stagingDir" -ForegroundColor Green

# ─────────────────────────────────────────────────────────────
# Step 5 — Run Inno Setup
# ─────────────────────────────────────────────────────────────
Write-Host "`n[5/5] Running Inno Setup compiler..." -ForegroundColor Cyan

# Auto-detect ISCC.exe
$isccPaths = @(
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    "C:\Program Files\Inno Setup 6\ISCC.exe",
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "${env:ProgramFiles}\Inno Setup 6\ISCC.exe"
)

$iscc = $null
foreach ($p in $isccPaths) {
    if (Test-Path $p) {
        $iscc = $p
        break
    }
}

if (-not $iscc) {
    Write-Host "  ERROR: Inno Setup 6 ISCC.exe not found!" -ForegroundColor Red
    Write-Host "  Searched:" -ForegroundColor Yellow
    foreach ($p in $isccPaths) { Write-Host "    $p" -ForegroundColor Yellow }
    Write-Host "  Please install Inno Setup 6 from: https://jrsoftware.org/isinfo.php" -ForegroundColor Yellow
    exit 1
}

Write-Host "  Using ISCC: $iscc" -ForegroundColor White

# Ensure dist directory exists
if (-not (Test-Path $distDir)) {
    New-Item -ItemType Directory -Force -Path $distDir | Out-Null
}

$setupScript = Join-Path $projectRoot "deploy\installer\setup.iss"

# StagingDir must be an absolute path — ISCC resolves relative paths from the .iss file's
# directory (deploy/installer/), which would break the staging reference.
$stagingAbsolute = (Resolve-Path $stagingDir).Path
$outputAbsolute = (Resolve-Path $distDir).Path

$isccArgs = @(
    "/DMyAppVersion=$Version",
    "/DStagingDir=$stagingAbsolute",
    "/O$outputAbsolute",
    $setupScript
)

Write-Host "  ISCC arguments: $($isccArgs -join ' ')" -ForegroundColor DarkGray

$oldErrorAction = $ErrorActionPreference
$ErrorActionPreference = "Continue"
& $iscc @isccArgs 2>&1 | ForEach-Object { Write-Host "  $_" }
$isccExitCode = $LASTEXITCODE
$ErrorActionPreference = $oldErrorAction

if ($isccExitCode -ne 0) {
    Write-Host "  ERROR: ISCC failed with exit code $isccExitCode" -ForegroundColor Red
    exit 1
}

# ─────────────────────────────────────────────────────────────
# Summary
# ─────────────────────────────────────────────────────────────
Write-Host "`n============================================" -ForegroundColor Green
Write-Host " Package complete!" -ForegroundColor Green
Write-Host "============================================" -ForegroundColor Green

$installerFile = Join-Path $distDir "CodePilot-Setup-$Version.exe"
if (Test-Path $installerFile) {
    $fileInfo = Get-Item $installerFile
    Write-Host "`n  Output : $installerFile" -ForegroundColor Cyan
    Write-Host "  Size   : $([math]::Round($fileInfo.Length / 1MB, 2)) MB" -ForegroundColor Cyan
}
else {
    # ISCC may have used a different filename pattern, find it
    $found = Get-ChildItem $distDir -Filter "CodePilot-Setup-*.exe" |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if ($found) {
        Write-Host "`n  Output : $($found.FullName)" -ForegroundColor Cyan
        Write-Host "  Size   : $([math]::Round($found.Length / 1MB, 2)) MB" -ForegroundColor Cyan
    }
    else {
        Write-Host "`n  Installer not found in $distDir. Check ISCC output above." -ForegroundColor Yellow
    }
}

Write-Host ""
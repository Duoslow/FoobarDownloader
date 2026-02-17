# setup_deps.ps1 - Download SDK dependencies for foo_downloader
#
# Usage: powershell -ExecutionPolicy Bypass -File setup_deps.ps1
#
# Downloads and extracts:
#   - foobar2000 SDK (includes foobar2000/, libPPUI/, pfc/)
#   - WTL headers (vendor/wtl/)
#   - SQLite amalgamation (vendor/sqlite3.c, vendor/sqlite3.h)
#   - aria2c.exe (vendor/aria2c.exe)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ── Version config (update these when upgrading) ──────────────────────────────
$FB2K_SDK_URL   = 'https://www.foobar2000.org/files/SDK-2025-03-07.7z'
$WTL_URL        = 'https://sourceforge.net/projects/wtl/files/WTL%2010/WTL%2010.0.10320%20Release/WTL10_10320_Release.zip/download'
$SQLITE_URL     = 'https://sqlite.org/2026/sqlite-amalgamation-3510200.zip'
$ARIA2_URL      = 'https://github.com/aria2/aria2/releases/download/release-1.37.0/aria2-1.37.0-win-64bit-build1.zip'

$RootDir = $PSScriptRoot
$TempDir = Join-Path $RootDir '_setup_temp'

function Ensure-Dir($path) {
    if (-not (Test-Path $path)) { New-Item -ItemType Directory -Path $path | Out-Null }
}

# ── Check for 7-Zip (needed for .7z SDK archive) ─────────────────────────────
$7z = $null
foreach ($candidate in @(
    "${env:ProgramFiles}\7-Zip\7z.exe",
    "${env:ProgramFiles(x86)}\7-Zip\7z.exe",
    (Get-Command 7z -ErrorAction SilentlyContinue).Source
)) {
    if ($candidate -and (Test-Path $candidate)) { $7z = $candidate; break }
}
if (-not $7z) {
    Write-Error "7-Zip is required to extract the foobar2000 SDK. Install from https://7-zip.org"
}

Ensure-Dir $TempDir
Ensure-Dir (Join-Path $RootDir 'vendor')

# ── 1. foobar2000 SDK ────────────────────────────────────────────────────────
if (Test-Path (Join-Path $RootDir 'foobar2000\SDK')) {
    Write-Host "[skip] foobar2000 SDK already present" -ForegroundColor DarkGray
} else {
    Write-Host "[1/4] Downloading foobar2000 SDK..." -ForegroundColor Yellow
    $sdkArchive = Join-Path $TempDir 'fb2k_sdk.7z'
    Invoke-WebRequest -Uri $FB2K_SDK_URL -OutFile $sdkArchive -UseBasicParsing

    Write-Host "       Extracting..." -ForegroundColor Yellow
    $sdkExtract = Join-Path $TempDir 'fb2k_sdk'
    & $7z x $sdkArchive "-o$sdkExtract" -y | Out-Null

    # The SDK archive extracts with a top-level folder; find and copy contents
    $sdkRoot = Get-ChildItem $sdkExtract -Directory | Select-Object -First 1
    if (-not $sdkRoot) { $sdkRoot = Get-Item $sdkExtract }

    foreach ($dir in @('foobar2000', 'libPPUI', 'pfc')) {
        $src = Join-Path $sdkRoot.FullName $dir
        if (Test-Path $src) {
            Copy-Item $src -Destination (Join-Path $RootDir $dir) -Recurse -Force
            Write-Host "       Copied $dir/" -ForegroundColor Green
        } else {
            Write-Warning "$dir not found in SDK archive"
        }
    }
}

# ── 2. WTL headers ───────────────────────────────────────────────────────────
$wtlDir = Join-Path $RootDir 'vendor\wtl'
if (Test-Path (Join-Path $wtlDir 'atlapp.h')) {
    Write-Host "[skip] WTL headers already present" -ForegroundColor DarkGray
} else {
    Write-Host "[2/4] Downloading WTL..." -ForegroundColor Yellow
    $wtlZip = Join-Path $TempDir 'wtl.zip'
    Invoke-WebRequest -Uri $WTL_URL -OutFile $wtlZip -UseBasicParsing

    $wtlExtract = Join-Path $TempDir 'wtl'
    Expand-Archive -Path $wtlZip -DestinationPath $wtlExtract -Force

    Ensure-Dir $wtlDir
    # WTL headers are in Include/ subfolder
    $includeDir = Get-ChildItem $wtlExtract -Recurse -Directory -Filter 'Include' | Select-Object -First 1
    if ($includeDir) {
        Copy-Item (Join-Path $includeDir.FullName '*') -Destination $wtlDir -Force
    } else {
        # Headers might be at root level
        Copy-Item (Join-Path $wtlExtract '*.h') -Destination $wtlDir -Force
    }
    Write-Host "       WTL headers installed" -ForegroundColor Green
}

# ── 3. SQLite amalgamation ────────────────────────────────────────────────────
$sqliteC = Join-Path $RootDir 'vendor\sqlite3.c'
if (Test-Path $sqliteC) {
    Write-Host "[skip] SQLite amalgamation already present" -ForegroundColor DarkGray
} else {
    Write-Host "[3/4] Downloading SQLite amalgamation..." -ForegroundColor Yellow
    $sqliteZip = Join-Path $TempDir 'sqlite.zip'
    Invoke-WebRequest -Uri $SQLITE_URL -OutFile $sqliteZip -UseBasicParsing

    $sqliteExtract = Join-Path $TempDir 'sqlite'
    Expand-Archive -Path $sqliteZip -DestinationPath $sqliteExtract -Force

    $sqliteSrc = Get-ChildItem $sqliteExtract -Recurse -File -Filter 'sqlite3.c' | Select-Object -First 1
    Copy-Item $sqliteSrc.FullName -Destination (Join-Path $RootDir 'vendor\sqlite3.c') -Force
    $sqliteHdr = Get-ChildItem $sqliteExtract -Recurse -File -Filter 'sqlite3.h' | Select-Object -First 1
    Copy-Item $sqliteHdr.FullName -Destination (Join-Path $RootDir 'vendor\sqlite3.h') -Force
    Write-Host "       SQLite amalgamation installed" -ForegroundColor Green
}

# ── 4. aria2c.exe ────────────────────────────────────────────────────────────
$aria2Exe = Join-Path $RootDir 'vendor\aria2c.exe'
if (Test-Path $aria2Exe) {
    Write-Host "[skip] aria2c.exe already present" -ForegroundColor DarkGray
} else {
    Write-Host "[4/4] Downloading aria2c..." -ForegroundColor Yellow
    $aria2Zip = Join-Path $TempDir 'aria2.zip'
    Invoke-WebRequest -Uri $ARIA2_URL -OutFile $aria2Zip -UseBasicParsing

    $aria2Extract = Join-Path $TempDir 'aria2'
    Expand-Archive -Path $aria2Zip -DestinationPath $aria2Extract -Force

    $aria2Bin = Get-ChildItem $aria2Extract -Recurse -File -Filter 'aria2c.exe' | Select-Object -First 1
    Copy-Item $aria2Bin.FullName -Destination $aria2Exe -Force
    Write-Host "       aria2c.exe installed" -ForegroundColor Green
}

# ── Cleanup ───────────────────────────────────────────────────────────────────
if (Test-Path $TempDir) { Remove-Item $TempDir -Recurse -Force }

Write-Host "`nAll dependencies ready. Open foo_downloader.sln in Visual Studio to build." -ForegroundColor Green

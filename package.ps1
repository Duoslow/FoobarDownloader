# package.ps1 - Build Release and package foo_downloader.fb2k-component
#
# Usage: powershell -ExecutionPolicy Bypass -File package.ps1

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

try {

$SolutionDir = $PSScriptRoot
$OutputName  = 'foo_downloader.fb2k-component'
$StagingDir  = Join-Path $SolutionDir '_package_staging'

# ── 1. Locate MSBuild ──────────────────────────────────────────────────────────
$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vsWhere)) {
    throw "vswhere.exe not found. Is Visual Studio installed?"
}

$msbuildPath = & $vsWhere -latest -requires Microsoft.Component.MSBuild `
    -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1

if (-not $msbuildPath -or -not (Test-Path $msbuildPath)) {
    throw "MSBuild.exe not found. Install the C++ build tools."
}

Write-Host "MSBuild: $msbuildPath" -ForegroundColor Cyan

# ── 2. Build Release ───────────────────────────────────────────────────────────
$solution = Join-Path $SolutionDir 'foo_downloader.sln'
Write-Host "Building Release|x64 ..." -ForegroundColor Yellow

& $msbuildPath $solution `
    /p:Configuration=Release `
    /p:Platform=x64 `
    /m `
    /v:minimal

if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE"
}

Write-Host "Build succeeded." -ForegroundColor Green

# ── 3. Stage files ─────────────────────────────────────────────────────────────
$dllPath   = Join-Path $SolutionDir 'build\Release\foo_downloader.dll'
$aria2Path = Join-Path $SolutionDir 'vendor\aria2c.exe'

if (-not (Test-Path $dllPath)) {
    throw "DLL not found at $dllPath"
}
if (-not (Test-Path $aria2Path)) {
    throw "aria2c.exe not found at $aria2Path"
}

if (Test-Path $StagingDir) { Remove-Item $StagingDir -Recurse -Force }
New-Item -ItemType Directory -Path $StagingDir | Out-Null

Copy-Item $dllPath   -Destination $StagingDir
Copy-Item $aria2Path -Destination $StagingDir

Write-Host "Staged files:" -ForegroundColor Cyan
Get-ChildItem $StagingDir | ForEach-Object {
    Write-Host ("  {0}  ({1:N0} KB)" -f $_.Name, ($_.Length / 1KB))
}

# ── 4. Create .fb2k-component (ZIP) ────────────────────────────────────────────
$outputPath = Join-Path $SolutionDir $OutputName
if (Test-Path $outputPath) { Remove-Item $outputPath -Force }

$zipPath = Join-Path $SolutionDir 'foo_downloader.zip'
if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
Compress-Archive -Path (Join-Path $StagingDir '*') -DestinationPath $zipPath
Rename-Item $zipPath $OutputName

Write-Host "`nPackage created: $outputPath" -ForegroundColor Green
Write-Host ("Size: {0:N0} KB" -f ((Get-Item $outputPath).Length / 1KB))

# ── 5. Cleanup ─────────────────────────────────────────────────────────────────
Remove-Item $StagingDir -Recurse -Force

Write-Host "`nDone." -ForegroundColor Green

} catch {
    Write-Host "`nERROR: $_" -ForegroundColor Red
    Write-Host $_.ScriptStackTrace -ForegroundColor DarkRed
}

Write-Host "`nPress any key to close..."
$null = $Host.UI.RawUI.ReadKey('NoEcho,IncludeKeyDown')

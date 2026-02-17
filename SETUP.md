# foo_downloader - Setup Guide

## Prerequisites

1. **Visual Studio 2022** with "Desktop development with C++" workload (MSVC v143, Windows SDK, C++ ATL)
2. **foobar2000 v2.x** portable install at `C:\Dev\foobar2000_dev\`
3. **7-Zip** (needed by the setup script to extract the SDK)

## First-time setup

Run the dependency setup script to download the foobar2000 SDK, WTL, SQLite, and aria2c:

```
powershell -ExecutionPolicy Bypass -File setup_deps.ps1
```

This downloads and installs:
- **foobar2000 SDK** (2025-03-07) → `foobar2000/`, `libPPUI/`, `pfc/`
- **WTL 10** headers → `vendor/wtl/`
- **SQLite** amalgamation → `vendor/sqlite3.c`, `vendor/sqlite3.h`
- **aria2c.exe** → `vendor/aria2c.exe`

## Build

1. Open `foo_downloader.sln` in Visual Studio 2022
2. Set configuration to **Debug | x64**
3. Build the solution (Ctrl+Shift+B)
4. Press F5 to launch foobar2000 with debugger

The post-build step copies the DLL to the foobar2000 components folder.

## Package for release

```
powershell -ExecutionPolicy Bypass -File package.ps1
```

Produces `foo_downloader.fb2k-component` — a ZIP containing the Release DLL and aria2c.exe.
Double-click to install in foobar2000.

## Verify

- Preferences > Components should show "Downloader v0.1.0"
- foobar2000 console should show `[foo_downloader]` messages
- Layout > Add panel: "Downloader" should be available
- Right-click in playlist: "Downloader > Download from URL..." should appear

## Adding a New Source Provider

1. Create `sources/source_myapi.h` implementing `ISourceProvider`
2. Register in `source_manager.cpp`: `Register(std::make_unique<MyApiSource>())`
3. Add the file to the vcxproj
4. Rebuild — appears in UI dropdown automatically

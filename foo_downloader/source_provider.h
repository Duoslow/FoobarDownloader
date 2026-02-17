#pragma once

#include <string>
#include <vector>

// ============================================================================
// Download item resolved by a source provider
// ============================================================================
struct DownloadItem {
    std::string url;            // The actual downloadable URL
    std::string filename;       // Suggested filename (can be empty — aria2 will figure it out)
    std::string title;          // Display title (for UI)
    std::string artist;         // Optional metadata
    std::string album;          // Optional metadata
    bool useYtDlp = false;      // Use yt-dlp instead of aria2
    std::string audioFormat;    // For yt-dlp: "flac", "mp3", "opus", etc.
    std::string audioQuality;   // For yt-dlp: "0" (best), "5" (worst)
    std::vector<std::string> headers;  // Custom HTTP headers (e.g., "Referer: https://...")
};

// ============================================================================
// Abstract source provider interface
// ============================================================================
// To add a new source:
//   1. Create a class implementing ISourceProvider
//   2. Register it in SourceManager's constructor
//   3. Rebuild — it will automatically appear in the UI dropdown
// ============================================================================
class ISourceProvider {
public:
    virtual ~ISourceProvider() = default;

    // Unique identifier string (e.g., "direct_url")
    virtual const char* GetId() const = 0;

    // Human-readable display name (e.g., "Direct URL")
    virtual const char* GetName() const = 0;

    // Given user input (URL, search term, etc.), resolve to downloadable URLs.
    // Returns true on success, false on error.
    // On success, fills `items` with one or more DownloadItem entries.
    virtual bool Resolve(const char* input, std::vector<DownloadItem>& items, std::string& errorMsg) = 0;

    // Whether this source requires additional settings (API keys, auth, etc.)
    virtual bool HasSettings() const { return false; }

    // Optional: description shown in UI
    virtual const char* GetDescription() const { return ""; }

    // Label for the action button (e.g., "Download", "Search")
    virtual const char* GetActionLabel() const { return "Download"; }
};

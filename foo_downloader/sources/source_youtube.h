#pragma once

#include "../source_provider.h"
#include <string>
#include <vector>

struct YouTubeSearchResult {
    std::string id;         // Video ID
    std::string title;
    std::string artist;     // Channel/uploader
    std::string album;
    int duration = 0;       // Seconds
    int64_t viewCount = 0;
    std::string uploadDate; // YYYYMMDD from yt-dlp
};

struct YouTubeQuality {
    const char* label;      // Display name
    const char* format;     // yt-dlp --audio-format value
    const char* quality;    // yt-dlp --audio-quality value
};

class YouTubeSource : public ISourceProvider {
public:
    const char* GetId() const override { return "youtube"; }
    const char* GetName() const override { return "YouTube"; }
    const char* GetDescription() const override { return "Search and download audio from YouTube"; }
    const char* GetActionLabel() const override { return "Search"; }

    bool Resolve(const char* input, std::vector<DownloadItem>& items, std::string& errorMsg) override;

    static std::string GetYtDlpPath();
    static bool EnsureYtDlp(std::string& errorMsg);
    static std::string FormatViewCount(int64_t count);
    static std::string FormatUploadDate(const std::string& yyyymmdd);

private:
    std::vector<YouTubeSearchResult> Search(const std::string& query, std::string& errorMsg);
    bool ShowSelectionDialog(const std::vector<YouTubeSearchResult>& results,
                             std::vector<int>& selectedIndices,
                             int& qualityIdx);

    static std::string RunProcess(const std::string& cmdLine, int timeoutMs = 30000);
    static bool DownloadYtDlp();
    static std::string ExtractJsonString(const std::string& json, const std::string& key);
    static int ExtractJsonInt(const std::string& json, const std::string& key);
    static int64_t ExtractJsonInt64(const std::string& json, const std::string& key);
};

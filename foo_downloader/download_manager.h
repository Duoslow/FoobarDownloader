#pragma once

#include "aria2_rpc.h"
#include "source_provider.h"
#include "../vendor/sqlite3.h"
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>
#include <windows.h>

struct DownloadEntry {
    std::string gid;
    std::string sourceId;
    std::string url;
    std::string title;
    std::string outputPath;
    std::string status;       // "queued", "active", "paused", "complete", "error"
    std::string errorMessage;
    std::string engine;       // "aria2" or "ytdlp"
    double progress = 0.0;
    uint64_t speed = 0;
    uint64_t totalSize = 0;
};

struct YtDlpProcess {
    HANDLE hProcess = NULL;
    HANDLE hStdoutRead = NULL;
    std::string capturedOutput;
};

using DownloadUpdateCallback = std::function<void(const DownloadEntry& entry)>;

class DownloadManager {
public:
    static DownloadManager& instance();

    bool StartDownload(const std::string& sourceId, const std::string& input);
    std::vector<DownloadEntry> GetDownloads() const;
    void ClearCompleted();
    void RemoveByIndex(int idx);
    void PauseByIndex(int idx);
    void ResumeByIndex(int idx);
    void CancelByIndex(int idx);
    void SetUpdateCallback(DownloadUpdateCallback cb);
    void Shutdown();

    // Persistence
    void SaveHistory();
    void LoadHistory();

private:
    DownloadManager();
    ~DownloadManager();
    DownloadManager(const DownloadManager&) = delete;
    DownloadManager& operator=(const DownloadManager&) = delete;

    void PollThread();
    void OnDownloadComplete(DownloadEntry& entry);

    // yt-dlp support
    std::string StartYtDlpDownload(const DownloadItem& item);
    void PollYtDlpDownload(DownloadEntry& entry);
    void CleanupYtDlpProcess(const std::string& gid);

    static std::string GetDllDirectory();
    static std::string GetDatabasePath();
    void OpenDb();
    void CloseDb();

    sqlite3* m_db = nullptr;
    std::vector<DownloadEntry> m_downloads;
    mutable std::mutex m_mutex;
    std::atomic<bool> m_shutdown{ false };
    std::thread m_pollThread;
    DownloadUpdateCallback m_callback;

    // yt-dlp process tracking
    std::map<std::string, YtDlpProcess> m_ytdlpProcs;
    int m_ytdlpCounter = 0;
};

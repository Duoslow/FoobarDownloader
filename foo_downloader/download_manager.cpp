#include "stdafx.h"
#include "guids.h"
#include "download_manager.h"
#include "source_manager.h"
#include "playlist_utils.h"
#include "sources/source_youtube.h"

extern const char* GetConfigOutputFolder();
extern bool GetConfigEmbedMetadata();
extern const char* GetConfigYtDlpPath();
extern const char* GetConfigYtDlpExtraFlags();
extern int GetConfigRetryCount();

DownloadManager& DownloadManager::instance() {
    static DownloadManager inst;
    return inst;
}

DownloadManager::DownloadManager() {
    LoadHistory();
}

DownloadManager::~DownloadManager() {
    Shutdown();
}

// ============================================================================
// History persistence (SQLite)
// ============================================================================

std::string DownloadManager::GetDllDirectory() {
    char dllPath[MAX_PATH] = {};
    HMODULE hMod = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&GetDllDirectory, &hMod);
    if (hMod) {
        GetModuleFileNameA(hMod, dllPath, MAX_PATH);
        std::string dir(dllPath);
        auto pos = dir.find_last_of("\\/");
        if (pos != std::string::npos) dir = dir.substr(0, pos + 1);
        return dir;
    }
    return "";
}

std::string DownloadManager::GetDatabasePath() {
    return GetDllDirectory() + "downloads.db";
}

void DownloadManager::OpenDb() {
    if (m_db) return;

    std::string path = GetDatabasePath();
    int rc = sqlite3_open(path.c_str(), &m_db);
    if (rc != SQLITE_OK) {
        FB2K_console_formatter() << "[foo_downloader] Failed to open database: " << sqlite3_errmsg(m_db);
        sqlite3_close(m_db);
        m_db = nullptr;
        return;
    }

    // Enable WAL mode for better concurrent access
    sqlite3_exec(m_db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    // Create table
    const char* createSql =
        "CREATE TABLE IF NOT EXISTS downloads ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  title TEXT NOT NULL DEFAULT '',"
        "  status TEXT NOT NULL DEFAULT '',"
        "  output_path TEXT NOT NULL DEFAULT '',"
        "  source_id TEXT NOT NULL DEFAULT '',"
        "  url TEXT NOT NULL DEFAULT '',"
        "  engine TEXT NOT NULL DEFAULT '',"
        "  error_message TEXT NOT NULL DEFAULT '',"
        "  created_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))"
        ");";

    char* errMsg = nullptr;
    rc = sqlite3_exec(m_db, createSql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        FB2K_console_formatter() << "[foo_downloader] Failed to create table: " << (errMsg ? errMsg : "unknown error");
        sqlite3_free(errMsg);
    }
}

void DownloadManager::CloseDb() {
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

void DownloadManager::SaveHistory() {
    // NOTE: caller must hold m_mutex
    if (!m_db) OpenDb();
    if (!m_db) return;

    sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    // Delete all rows and re-insert
    sqlite3_exec(m_db, "DELETE FROM downloads;", nullptr, nullptr, nullptr);

    const char* insertSql =
        "INSERT INTO downloads (title, status, output_path, source_id, url, engine, error_message) "
        "VALUES (?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, insertSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        FB2K_console_formatter() << "[foo_downloader] Failed to prepare insert: " << sqlite3_errmsg(m_db);
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return;
    }

    for (const auto& e : m_downloads) {
        if (e.status != "complete" && e.status != "error") continue;

        sqlite3_bind_text(stmt, 1, e.title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, e.status.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, e.outputPath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, e.sourceId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, e.url.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, e.engine.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, e.errorMessage.c_str(), -1, SQLITE_TRANSIENT);

        sqlite3_step(stmt);
        sqlite3_reset(stmt);
    }

    sqlite3_finalize(stmt);
    sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
}

void DownloadManager::LoadHistory() {
    OpenDb();
    if (!m_db) return;

    // Migrate from old txt file if it exists
    std::string dir = GetDllDirectory();
    std::string txtPath = dir + "download_history.txt";
    DWORD attrs = GetFileAttributesA(txtPath.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        // Parse old TSV file
        FILE* f = fopen(txtPath.c_str(), "r");
        if (f) {
            std::vector<DownloadEntry> migrated;
            char lineBuf[8192];
            while (fgets(lineBuf, sizeof(lineBuf), f)) {
                std::string line(lineBuf);
                while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
                    line.pop_back();
                if (line.empty()) continue;

                // Unescape helper (inline for migration only)
                auto unescape = [](const std::string& s) -> std::string {
                    std::string out;
                    out.reserve(s.size());
                    for (size_t i = 0; i < s.size(); i++) {
                        if (s[i] == '\\' && i + 1 < s.size()) {
                            char next = s[i + 1];
                            if (next == 't') { out += '\t'; i++; }
                            else if (next == 'n') { out += '\n'; i++; }
                            else if (next == 'r') { out += '\r'; i++; }
                            else if (next == '\\') { out += '\\'; i++; }
                            else out += s[i];
                        } else {
                            out += s[i];
                        }
                    }
                    return out;
                };

                std::vector<std::string> fields;
                size_t start = 0;
                while (start < line.size()) {
                    auto tab = line.find('\t', start);
                    if (tab == std::string::npos) {
                        fields.push_back(unescape(line.substr(start)));
                        break;
                    }
                    fields.push_back(unescape(line.substr(start, tab - start)));
                    start = tab + 1;
                }

                if (fields.size() >= 6) {
                    DownloadEntry entry;
                    entry.title = fields[0];
                    entry.status = fields[1];
                    entry.outputPath = fields[2];
                    entry.sourceId = fields[3];
                    entry.url = fields[4];
                    entry.engine = fields[5];
                    if (fields.size() >= 7) entry.errorMessage = fields[6];
                    if (entry.status == "complete") entry.progress = 100.0;
                    migrated.push_back(std::move(entry));
                }
            }
            fclose(f);

            // Insert migrated entries into SQLite
            if (!migrated.empty()) {
                const char* insertSql =
                    "INSERT INTO downloads (title, status, output_path, source_id, url, engine, error_message) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?);";

                sqlite3_stmt* stmt = nullptr;
                sqlite3_prepare_v2(m_db, insertSql, -1, &stmt, nullptr);
                sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

                for (const auto& e : migrated) {
                    sqlite3_bind_text(stmt, 1, e.title.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(stmt, 2, e.status.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(stmt, 3, e.outputPath.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(stmt, 4, e.sourceId.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(stmt, 5, e.url.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(stmt, 6, e.engine.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(stmt, 7, e.errorMessage.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_step(stmt);
                    sqlite3_reset(stmt);
                }

                sqlite3_finalize(stmt);
                sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);

                FB2K_console_formatter() << "[foo_downloader] Migrated " << (uint32_t)migrated.size() << " entries from download_history.txt to SQLite.";
            }

            // Delete old txt file
            DeleteFileA(txtPath.c_str());
        }
    }

    // Load entries from SQLite
    const char* selectSql =
        "SELECT title, status, output_path, source_id, url, engine, error_message FROM downloads ORDER BY id;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, selectSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        FB2K_console_formatter() << "[foo_downloader] Failed to query downloads: " << sqlite3_errmsg(m_db);
        return;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DownloadEntry entry;
        entry.title        = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        entry.status       = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        entry.outputPath   = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        entry.sourceId     = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        entry.url          = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        entry.engine       = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        entry.errorMessage = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));

        if (entry.status == "complete") entry.progress = 100.0;

        m_downloads.push_back(std::move(entry));
    }

    sqlite3_finalize(stmt);

    if (!m_downloads.empty()) {
        FB2K_console_formatter() << "[foo_downloader] Loaded " << (uint32_t)m_downloads.size() << " entries from history.";
    }
}

// ============================================================================
// Start download
// ============================================================================

bool DownloadManager::StartDownload(const std::string& sourceId, const std::string& input) {
    ISourceProvider* source = SourceManager::instance().GetById(sourceId);
    if (!source) {
        FB2K_console_formatter() << "[foo_downloader] Unknown source: " << sourceId.c_str();
        popup_message::g_show("Unknown download source selected.", "foo_downloader", popup_message::icon_error);
        return false;
    }

    std::vector<DownloadItem> items;
    std::string errorMsg;

    if (!source->Resolve(input.c_str(), items, errorMsg)) {
        if (!errorMsg.empty()) {
            FB2K_console_formatter() << "[foo_downloader] Resolve failed: " << errorMsg.c_str();
            popup_message::g_show(errorMsg.c_str(), "foo_downloader", popup_message::icon_error);
        }
        return false;
    }

    if (items.empty()) {
        popup_message::g_show("No downloadable items found.", "foo_downloader", popup_message::icon_error);
        return false;
    }

    for (const auto& item : items) {
        if (item.useYtDlp) {
            // Use yt-dlp for this download
            std::string gid = StartYtDlpDownload(item);
            if (gid.empty()) {
                FB2K_console_formatter() << "[foo_downloader] Failed to start yt-dlp for: " << item.url.c_str();
                continue;
            }

            DownloadEntry entry;
            entry.gid = gid;
            entry.sourceId = sourceId;
            entry.url = item.url;
            entry.title = item.title.empty() ? item.url : item.title;
            entry.status = "active";
            entry.engine = "ytdlp";

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_downloads.push_back(std::move(entry));
            }

            FB2K_console_formatter() << "[foo_downloader] yt-dlp started: " << item.title.c_str();
        } else {
            // Use aria2 for this download
            auto& aria2 = Aria2RpcClient::instance();
            if (!aria2.IsRunning()) {
                popup_message::g_show("aria2 daemon is not running. Check Preferences > Tools > Downloader.", "foo_downloader", popup_message::icon_error);
                return false;
            }

            std::map<std::string, std::string> options;
            std::vector<std::string> headers;

            int retries = GetConfigRetryCount();
            if (retries > 0) {
                options["max-tries"] = std::to_string(retries);
                options["retry-wait"] = "2";
            }

            if (!item.filename.empty() && item.filename.find('.') != std::string::npos) {
                options["out"] = item.filename;
            }

            // Apply any custom headers provided by the source
            for (const auto& h : item.headers) {
                headers.push_back(h);
            }

            std::string gid;
            int rpcRetries = (retries > 0) ? retries : 1;
            for (int attempt = 0; attempt < rpcRetries; attempt++) {
                gid = aria2.AddUri(item.url, options, headers);
                if (!gid.empty()) break;
                if (attempt + 1 < rpcRetries) {
                    FB2K_console_formatter() << "[foo_downloader] AddUri failed, retrying (" << (uint32_t)(attempt + 1) << "/" << (uint32_t)rpcRetries << ")...";
                    Sleep(1000);
                }
            }
            if (gid.empty()) {
                FB2K_console_formatter() << "[foo_downloader] Failed to add after " << (uint32_t)rpcRetries << " attempts: " << item.url.c_str();
                continue;
            }

            DownloadEntry entry;
            entry.gid = gid;
            entry.sourceId = sourceId;
            entry.url = item.url;
            entry.title = item.title.empty() ? item.url : item.title;
            entry.status = "queued";
            entry.engine = "aria2";

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_downloads.push_back(std::move(entry));
            }

            FB2K_console_formatter() << "[foo_downloader] Queued: " << item.title.c_str() << " (GID: " << gid.c_str() << ")";
        }
    }

    // Start poll thread on first download
    if (!m_pollThread.joinable()) {
        m_pollThread = std::thread(&DownloadManager::PollThread, this);
    }

    return true;
}

// ============================================================================
// yt-dlp download management
// ============================================================================

std::string DownloadManager::StartYtDlpDownload(const DownloadItem& item) {
    // Use configured path first, fall back to auto-detect
    const char* cfgPath = GetConfigYtDlpPath();
    std::string ytdlpPath = (cfgPath && *cfgPath) ? cfgPath : YouTubeSource::GetYtDlpPath();

    // Build output directory
    std::string outputDir = GetConfigOutputFolder();
    if (outputDir.empty()) {
        // Default: user's Music folder
        char musicPath[MAX_PATH];
        if (SHGetFolderPathA(NULL, CSIDL_MYMUSIC, NULL, 0, musicPath) == S_OK) {
            outputDir = std::string(musicPath) + "\\foo_downloader";
        } else {
            outputDir = "C:\\Music\\foo_downloader";
        }
    }

    // Ensure output dir exists
    CreateDirectoryA(outputDir.c_str(), NULL);

    // Build yt-dlp command
    std::string audioFmt = item.audioFormat.empty() ? "flac" : item.audioFormat;
    std::string audioQual = item.audioQuality.empty() ? "0" : item.audioQuality;

    std::string embedFlags;
    if (GetConfigEmbedMetadata()) {
        // Thumbnail embedding only works with: mp3, mkv/mka, ogg/opus/flac, m4a/mp4/m4v/mov
        if (audioFmt == "wav") {
            embedFlags = "--embed-metadata ";
        } else {
            embedFlags = "--embed-metadata --embed-thumbnail ";
        }
    }

    // User-specified extra flags
    std::string extraFlags;
    const char* cfgExtra = GetConfigYtDlpExtraFlags();
    if (cfgExtra && *cfgExtra) {
        extraFlags = std::string(cfgExtra) + " ";
    }

    std::string retryFlags;
    int retries = GetConfigRetryCount();
    if (retries > 0) {
        retryFlags = "--retries " + std::to_string(retries) + " "
                     "--file-access-retries " + std::to_string(retries) + " "
                     "--fragment-retries " + std::to_string(retries) + " ";
    }

    std::string cmd = "\"" + ytdlpPath + "\" "
        "-x "
        "--audio-format " + audioFmt + " "
        "--audio-quality " + audioQual + " "
        + embedFlags
        + extraFlags
        + retryFlags +
        "--newline "
        "--no-warnings "
        "--no-playlist "
        "-o \"" + outputDir + "\\%(title)s.%(ext)s\" "
        "\"" + item.url + "\"";

    FB2K_console_formatter() << "[foo_downloader] yt-dlp cmd: " << cmd.c_str();

    // Create pipe for stdout
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hReadPipe = NULL, hWritePipe = NULL;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        return "";
    }
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    std::vector<char> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(0);

    if (!CreateProcessA(NULL, cmdBuf.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        FB2K_console_formatter() << "[foo_downloader] Failed to start yt-dlp process.";
        return "";
    }

    CloseHandle(hWritePipe); // Close write end in parent
    CloseHandle(pi.hThread);

    // Generate unique GID
    std::string gid = "ytdlp_" + std::to_string(++m_ytdlpCounter);

    YtDlpProcess proc;
    proc.hProcess = pi.hProcess;
    proc.hStdoutRead = hReadPipe;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_ytdlpProcs[gid] = proc;
    }

    return gid;
}

void DownloadManager::PollYtDlpDownload(DownloadEntry& entry) {
    auto it = m_ytdlpProcs.find(entry.gid);
    if (it == m_ytdlpProcs.end()) {
        entry.status = "error";
        entry.errorMessage = "yt-dlp process not found";
        return;
    }

    auto& proc = it->second;

    // Non-blocking read from stdout pipe
    DWORD avail = 0;
    if (PeekNamedPipe(proc.hStdoutRead, NULL, 0, NULL, &avail, NULL) && avail > 0) {
        char buf[4096];
        DWORD toRead = (avail < sizeof(buf) - 1) ? avail : sizeof(buf) - 1;
        DWORD bytesRead = 0;
        if (ReadFile(proc.hStdoutRead, buf, toRead, &bytesRead, NULL) && bytesRead > 0) {
            buf[bytesRead] = 0;
            proc.capturedOutput += buf;
        }
    }

    // Parse progress from captured output
    {
        auto& out = proc.capturedOutput;

        auto lastDl = out.rfind("[download]");
        if (lastDl != std::string::npos) {
            auto lineEnd = out.find('\n', lastDl);
            std::string line = (lineEnd != std::string::npos)
                ? out.substr(lastDl, lineEnd - lastDl)
                : out.substr(lastDl);

            auto pctPos = line.find('%');
            if (pctPos != std::string::npos) {
                size_t numStart = pctPos;
                while (numStart > 0 && (isdigit(line[numStart - 1]) || line[numStart - 1] == '.')) {
                    numStart--;
                }
                std::string pctStr = line.substr(numStart, pctPos - numStart);
                try { entry.progress = std::stod(pctStr); } catch (...) {}
            }

            auto atPos = line.find(" at ");
            if (atPos != std::string::npos) {
                std::string speedPart = line.substr(atPos + 4);
                size_t start = speedPart.find_first_not_of(' ');
                if (start != std::string::npos) {
                    speedPart = speedPart.substr(start);
                    double speedVal = 0;
                    try { speedVal = std::stod(speedPart); } catch (...) {}

                    if (speedPart.find("GiB/s") != std::string::npos) {
                        entry.speed = (uint64_t)(speedVal * 1024 * 1024 * 1024);
                    } else if (speedPart.find("MiB/s") != std::string::npos) {
                        entry.speed = (uint64_t)(speedVal * 1024 * 1024);
                    } else if (speedPart.find("KiB/s") != std::string::npos) {
                        entry.speed = (uint64_t)(speedVal * 1024);
                    } else if (speedPart.find("B/s") != std::string::npos) {
                        entry.speed = (uint64_t)speedVal;
                    }
                }
            }
        }

        // Check for ExtractAudio destination (final output path)
        auto extractPos = out.rfind("[ExtractAudio] Destination: ");
        if (extractPos != std::string::npos) {
            size_t pathStart = extractPos + strlen("[ExtractAudio] Destination: ");
            auto pathEnd = out.find('\n', pathStart);
            std::string path = (pathEnd != std::string::npos)
                ? out.substr(pathStart, pathEnd - pathStart)
                : out.substr(pathStart);
            while (!path.empty() && (path.back() == '\r' || path.back() == '\n' || path.back() == ' ')) {
                path.pop_back();
            }
            if (!path.empty()) entry.outputPath = path;
        }

        if (entry.outputPath.empty()) {
            auto destPos = out.rfind("[download] Destination: ");
            if (destPos != std::string::npos) {
                size_t pathStart = destPos + strlen("[download] Destination: ");
                auto pathEnd = out.find('\n', pathStart);
                std::string path = (pathEnd != std::string::npos)
                    ? out.substr(pathStart, pathEnd - pathStart)
                    : out.substr(pathStart);
                while (!path.empty() && (path.back() == '\r' || path.back() == '\n' || path.back() == ' ')) {
                    path.pop_back();
                }
                if (!path.empty()) entry.outputPath = path;
            }
        }
    }

    // Check if process has exited
    DWORD exitCode = STILL_ACTIVE;
    if (GetExitCodeProcess(proc.hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
        // Read any remaining output
        DWORD avail2 = 0;
        while (PeekNamedPipe(proc.hStdoutRead, NULL, 0, NULL, &avail2, NULL) && avail2 > 0) {
            char buf[4096];
            DWORD toRead = (avail2 < sizeof(buf) - 1) ? avail2 : sizeof(buf) - 1;
            DWORD bytesRead = 0;
            if (ReadFile(proc.hStdoutRead, buf, toRead, &bytesRead, NULL) && bytesRead > 0) {
                buf[bytesRead] = 0;
                proc.capturedOutput += buf;
            } else {
                break;
            }
        }

        // Re-parse output path from final output
        auto& out = proc.capturedOutput;
        auto extractPos = out.rfind("[ExtractAudio] Destination: ");
        if (extractPos != std::string::npos) {
            size_t pathStart = extractPos + strlen("[ExtractAudio] Destination: ");
            auto pathEnd = out.find('\n', pathStart);
            std::string path = (pathEnd != std::string::npos)
                ? out.substr(pathStart, pathEnd - pathStart)
                : out.substr(pathStart);
            while (!path.empty() && (path.back() == '\r' || path.back() == '\n' || path.back() == ' ')) {
                path.pop_back();
            }
            if (!path.empty()) entry.outputPath = path;
        }

        if (exitCode == 0) {
            entry.status = "complete";
            entry.progress = 100.0;
            entry.speed = 0;

            if (!entry.outputPath.empty()) {
                auto lastSlash = entry.outputPath.find_last_of("\\/");
                if (lastSlash != std::string::npos && lastSlash + 1 < entry.outputPath.size()) {
                    entry.title = entry.outputPath.substr(lastSlash + 1);
                }
            }

            OnDownloadComplete(entry);
            FB2K_console_formatter() << "[foo_downloader] yt-dlp complete: " << entry.title.c_str();
        } else {
            entry.status = "error";
            entry.speed = 0;

            auto errPos = out.rfind("ERROR:");
            if (errPos != std::string::npos) {
                auto errEnd = out.find('\n', errPos);
                entry.errorMessage = (errEnd != std::string::npos)
                    ? out.substr(errPos, errEnd - errPos)
                    : out.substr(errPos);
            } else {
                entry.errorMessage = "yt-dlp exited with code " + std::to_string(exitCode);
            }
            FB2K_console_formatter() << "[foo_downloader] yt-dlp error: " << entry.errorMessage.c_str();
        }

        // Save history after status change
        SaveHistory();

        CloseHandle(proc.hStdoutRead);
        CloseHandle(proc.hProcess);
        m_ytdlpProcs.erase(it);
    }
}

void DownloadManager::CleanupYtDlpProcess(const std::string& gid) {
    auto it = m_ytdlpProcs.find(gid);
    if (it != m_ytdlpProcs.end()) {
        TerminateProcess(it->second.hProcess, 1);
        CloseHandle(it->second.hStdoutRead);
        CloseHandle(it->second.hProcess);
        m_ytdlpProcs.erase(it);
    }
}

// ============================================================================
// Standard operations
// ============================================================================

std::vector<DownloadEntry> DownloadManager::GetDownloads() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_downloads;
}

void DownloadManager::ClearCompleted() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_downloads.erase(
        std::remove_if(m_downloads.begin(), m_downloads.end(),
            [](const DownloadEntry& e) {
                return e.status == "complete" || e.status == "error";
            }),
        m_downloads.end());
    SaveHistory();
}

void DownloadManager::RemoveByIndex(int idx) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (idx >= 0 && idx < (int)m_downloads.size()) {
        auto& entry = m_downloads[idx];
        if (entry.status == "queued" || entry.status == "active" || entry.status == "paused") {
            if (entry.engine == "ytdlp") {
                CleanupYtDlpProcess(entry.gid);
            } else {
                Aria2RpcClient::instance().Remove(entry.gid);
            }
        }
        m_downloads.erase(m_downloads.begin() + idx);
        SaveHistory();
    }
}

void DownloadManager::PauseByIndex(int idx) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (idx >= 0 && idx < (int)m_downloads.size()) {
        auto& entry = m_downloads[idx];
        if (entry.engine == "aria2" && (entry.status == "active" || entry.status == "queued")) {
            if (Aria2RpcClient::instance().Pause(entry.gid)) {
                entry.status = "paused";
                entry.speed = 0;
            }
        }
        // yt-dlp doesn't support pause - would need to kill and restart
    }
}

void DownloadManager::ResumeByIndex(int idx) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (idx >= 0 && idx < (int)m_downloads.size()) {
        auto& entry = m_downloads[idx];
        if (entry.engine == "aria2" && entry.status == "paused") {
            if (Aria2RpcClient::instance().Unpause(entry.gid)) {
                entry.status = "active";
            }
        }
    }
}

void DownloadManager::CancelByIndex(int idx) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (idx >= 0 && idx < (int)m_downloads.size()) {
        auto& entry = m_downloads[idx];
        if (entry.status == "queued" || entry.status == "active" || entry.status == "paused") {
            if (entry.engine == "ytdlp") {
                CleanupYtDlpProcess(entry.gid);
            } else {
                Aria2RpcClient::instance().Remove(entry.gid);
            }
            entry.status = "error";
            entry.errorMessage = "Cancelled";
            entry.speed = 0;
            SaveHistory();
        }
    }
}

void DownloadManager::SetUpdateCallback(DownloadUpdateCallback cb) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_callback = std::move(cb);
}

void DownloadManager::Shutdown() {
    m_shutdown = true;
    if (m_pollThread.joinable()) {
        m_pollThread.join();
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    // Kill any active yt-dlp processes
    for (auto& [gid, proc] : m_ytdlpProcs) {
        TerminateProcess(proc.hProcess, 1);
        CloseHandle(proc.hStdoutRead);
        CloseHandle(proc.hProcess);
    }
    m_ytdlpProcs.clear();

    // Remove active aria2 downloads
    auto& aria2 = Aria2RpcClient::instance();
    for (auto& entry : m_downloads) {
        if ((entry.status == "queued" || entry.status == "active" || entry.status == "paused") && entry.engine != "ytdlp") {
            aria2.Remove(entry.gid);
        }
    }

    // Mark any active downloads as cancelled before saving
    for (auto& entry : m_downloads) {
        if (entry.status == "queued" || entry.status == "active" || entry.status == "paused") {
            entry.status = "error";
            entry.errorMessage = "Interrupted (foobar2000 closed)";
        }
    }

    SaveHistory();
    CloseDb();
}

void DownloadManager::PollThread() {
    FB2K_console_formatter() << "[foo_downloader] Poll thread started.";

    while (!m_shutdown) {
        for (int i = 0; i < 50 && !m_shutdown; i++) {
            Sleep(10);
        }
        if (m_shutdown) break;

        std::lock_guard<std::mutex> lock(m_mutex);

        for (auto& entry : m_downloads) {
            if (entry.status == "complete" || entry.status == "error") continue;
            if (entry.status == "paused") continue;

            if (entry.engine == "ytdlp") {
                PollYtDlpDownload(entry);
            } else {
                auto& aria2 = Aria2RpcClient::instance();
                if (!aria2.IsRunning()) continue;

                Aria2Status status = aria2.GetStatus(entry.gid);

                entry.progress = status.GetProgress();
                entry.speed = status.downloadSpeed;
                entry.totalSize = status.totalLength;

                if (!status.files.empty() && !status.files[0].empty()) {
                    entry.outputPath = status.files[0];
                    for (char& c : entry.outputPath) {
                        if (c == '/') c = '\\';
                    }
                    std::string filePath = status.files[0];
                    auto lastSlash = filePath.find_last_of("\\/");
                    if (lastSlash != std::string::npos && lastSlash + 1 < filePath.size()) {
                        entry.title = filePath.substr(lastSlash + 1);
                    }
                }

                if (status.IsComplete()) {
                    entry.status = "complete";
                    OnDownloadComplete(entry);
                    SaveHistory();
                } else if (status.IsError()) {
                    entry.status = "error";
                    entry.errorMessage = status.errorMessage;
                    FB2K_console_formatter() << "[foo_downloader] Error: " << entry.title.c_str() << " - " << entry.errorMessage.c_str();
                    SaveHistory();
                } else if (status.status == "paused") {
                    entry.status = "paused";
                    entry.speed = 0;
                } else if (status.IsActive()) {
                    entry.status = "active";
                }
            }

            if (m_callback) {
                DownloadEntry snapshot = entry;
                fb2k::inMainThread([this, snapshot]() {
                    if (m_callback) {
                        m_callback(snapshot);
                    }
                });
            }
        }
    }
}

void DownloadManager::OnDownloadComplete(DownloadEntry& entry) {
    FB2K_console_formatter() << "[foo_downloader] Complete: " << entry.title.c_str() << " -> " << entry.outputPath.c_str();

    if (!entry.outputPath.empty()) {
        std::string path = entry.outputPath;

        fb2k::inMainThread([path]() {
            PlaylistUtils::AddToDownloadedPlaylist(path);
        });
    }
}

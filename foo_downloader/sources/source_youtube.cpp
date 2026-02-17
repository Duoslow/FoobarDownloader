#include "../stdafx.h"
#include "source_youtube.h"
#include "../resource.h"

#include <helpers/atl-misc.h>
#include <helpers/DarkMode.h>
#include <commctrl.h>
#include <shellapi.h>
#include <sstream>

// ============================================================================
// Quality options
// ============================================================================

static const YouTubeQuality g_qualities[] = {
    { "FLAC (lossless)",       "flac", "0" },
    { "WAV (lossless)",        "wav",  "0" },
    { "MP3 320kbps (best)",    "mp3",  "0" },
    { "MP3 256kbps",           "mp3",  "2" },
    { "MP3 192kbps",           "mp3",  "3" },
    { "MP3 128kbps",           "mp3",  "5" },
    { "MP3 96kbps (low)",      "mp3",  "7" },
    { "AAC 256kbps",           "m4a",  "0" },
    { "AAC 128kbps",           "m4a",  "5" },
    { "Opus (best)",           "opus", "0" },
    { "Vorbis (best)",         "vorbis","0" },
};
static const int g_numQualities = sizeof(g_qualities) / sizeof(g_qualities[0]);

// ============================================================================
// Search results selection dialog with quality picker
// ============================================================================

class CYouTubeResultsDialog : public CDialogImpl<CYouTubeResultsDialog> {
public:
    enum { IDD = IDD_YT_SEARCH_RESULTS };

    const std::vector<YouTubeSearchResult>& m_results;
    std::vector<int> m_selected;
    int m_qualityIdx = 0;

    CYouTubeResultsDialog(const std::vector<YouTubeSearchResult>& results)
        : m_results(results) {}

    BEGIN_MSG_MAP_EX(CYouTubeResultsDialog)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_ID_HANDLER_EX(IDOK, OnOk)
        COMMAND_ID_HANDLER_EX(IDCANCEL, OnCancel)
        NOTIFY_HANDLER_EX(IDC_YT_RESULTS_LIST, NM_DBLCLK, OnListDblClick)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM) {
        m_dark.AddDialogWithControls(*this);
        CenterWindow(GetParent());

        // Info text
        pfc::string_formatter info;
        info << "Found " << (unsigned)m_results.size() << " result(s). Select tracks to download:";
        uSetDlgItemText(*this, IDC_YT_RESULTS_INFO, info);

        // Setup ListView
        CListViewCtrl list(GetDlgItem(IDC_YT_RESULTS_LIST));
        list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_CHECKBOXES);

        list.InsertColumn(0, L"Title", LVCFMT_LEFT, 170);
        list.InsertColumn(1, L"Artist", LVCFMT_LEFT, 90);
        list.InsertColumn(2, L"Duration", LVCFMT_LEFT, 48);
        list.InsertColumn(3, L"Views", LVCFMT_RIGHT, 58);
        list.InsertColumn(4, L"URL", LVCFMT_LEFT, 150);

        for (int i = 0; i < (int)m_results.size(); i++) {
            const auto& r = m_results[i];

            pfc::stringcvt::string_wide_from_utf8 wTitle(r.title.c_str());
            list.InsertItem(i, wTitle);

            pfc::stringcvt::string_wide_from_utf8 wArtist(r.artist.c_str());
            list.SetItemText(i, 1, wArtist);

            // Duration
            char durBuf[16];
            if (r.duration >= 3600) {
                snprintf(durBuf, sizeof(durBuf), "%d:%02d:%02d", r.duration / 3600, (r.duration % 3600) / 60, r.duration % 60);
            } else {
                snprintf(durBuf, sizeof(durBuf), "%d:%02d", r.duration / 60, r.duration % 60);
            }
            pfc::stringcvt::string_wide_from_utf8 wDur(durBuf);
            list.SetItemText(i, 2, wDur);

            // View count
            std::string views = YouTubeSource::FormatViewCount(r.viewCount);
            pfc::stringcvt::string_wide_from_utf8 wViews(views.c_str());
            list.SetItemText(i, 3, wViews);

            // URL
            std::string url = "youtube.com/watch?v=" + r.id;
            pfc::stringcvt::string_wide_from_utf8 wUrl(url.c_str());
            list.SetItemText(i, 4, wUrl);
        }

        // Setup quality dropdown
        CComboBox combo(GetDlgItem(IDC_YT_QUALITY_COMBO));
        for (int i = 0; i < g_numQualities; i++) {
            pfc::stringcvt::string_wide_from_utf8 wLabel(g_qualities[i].label);
            combo.AddString(wLabel);
        }
        combo.SetCurSel(0);

        return FALSE;
    }

    LRESULT OnListDblClick(LPNMHDR pnmh) {
        LPNMITEMACTIVATE pItem = (LPNMITEMACTIVATE)pnmh;
        int idx = pItem->iItem;
        if (idx >= 0 && idx < (int)m_results.size()) {
            std::string url = "https://www.youtube.com/watch?v=" + m_results[idx].id;
            pfc::stringcvt::string_wide_from_utf8 wUrl(url.c_str());
            ShellExecuteW(NULL, L"open", wUrl, NULL, NULL, SW_SHOWNORMAL);
        }
        return 0;
    }

    void OnOk(UINT, int, CWindow) {
        CListViewCtrl list(GetDlgItem(IDC_YT_RESULTS_LIST));
        m_selected.clear();

        for (int i = 0; i < (int)m_results.size(); i++) {
            if (list.GetCheckState(i)) {
                m_selected.push_back(i);
            }
        }

        CComboBox combo(GetDlgItem(IDC_YT_QUALITY_COMBO));
        m_qualityIdx = combo.GetCurSel();
        if (m_qualityIdx < 0) m_qualityIdx = 0;

        EndDialog(IDOK);
    }

    void OnCancel(UINT, int, CWindow) {
        EndDialog(IDCANCEL);
    }

private:
    fb2k::CDarkModeHooks m_dark;
};

// ============================================================================
// YouTubeSource implementation
// ============================================================================

bool YouTubeSource::Resolve(const char* input, std::vector<DownloadItem>& items, std::string& errorMsg) {
    if (!input || !*input) {
        errorMsg = "Please enter a search query or YouTube URL.";
        return false;
    }

    std::string query(input);

    // Ensure yt-dlp is available
    if (!EnsureYtDlp(errorMsg)) {
        return false;
    }

    // Check if input is already a YouTube URL
    bool isUrl = (query.find("youtube.com/") != std::string::npos ||
                  query.find("youtu.be/") != std::string::npos ||
                  query.find("music.youtube.com/") != std::string::npos);

    if (isUrl) {
        // Direct URL — skip search, just queue it
        // Still show quality dialog with a single item
        std::vector<YouTubeSearchResult> results;

        // Use yt-dlp to get video info
        std::string ytdlpPath = GetYtDlpPath();
        std::string cmd = "\"" + ytdlpPath + "\" --no-download -j \"" + query + "\"";

        std::string output = RunProcess(cmd, 30000);
        if (!output.empty()) {
            // Parse each line as a JSON object (playlists may have multiple)
            std::istringstream iss(output);
            std::string line;
            while (std::getline(iss, line)) {
                if (line.empty() || line[0] != '{') continue;
                YouTubeSearchResult r;
                r.id = ExtractJsonString(line, "id");
                r.title = ExtractJsonString(line, "title");
                r.artist = ExtractJsonString(line, "channel");
                if (r.artist.empty()) r.artist = ExtractJsonString(line, "uploader");
                r.duration = ExtractJsonInt(line, "duration");
                r.viewCount = ExtractJsonInt64(line, "view_count");
                r.uploadDate = ExtractJsonString(line, "upload_date");
                if (!r.id.empty()) results.push_back(std::move(r));
            }
        }

        if (results.empty()) {
            errorMsg = "Could not extract video info from URL.";
            return false;
        }

        std::vector<int> selectedIndices;
        int qualityIdx = 0;
        if (!ShowSelectionDialog(results, selectedIndices, qualityIdx)) {
            errorMsg = "";
            return false;
        }

        if (selectedIndices.empty()) {
            errorMsg = "No tracks selected.";
            return false;
        }

        for (int idx : selectedIndices) {
            const auto& r = results[idx];
            DownloadItem item;
            item.url = "https://www.youtube.com/watch?v=" + r.id;
            item.title = r.artist.empty() ? r.title : (r.artist + " - " + r.title);
            item.artist = r.artist;
            item.useYtDlp = true;
            item.audioFormat = g_qualities[qualityIdx].format;
            item.audioQuality = g_qualities[qualityIdx].quality;
            items.push_back(std::move(item));
        }
        return true;
    }

    // Search mode
    auto results = Search(query, errorMsg);
    if (results.empty()) {
        if (errorMsg.empty()) errorMsg = "No results found for: " + query;
        return false;
    }

    std::vector<int> selectedIndices;
    int qualityIdx = 0;
    if (!ShowSelectionDialog(results, selectedIndices, qualityIdx)) {
        errorMsg = "";
        return false;
    }

    if (selectedIndices.empty()) {
        errorMsg = "No tracks selected.";
        return false;
    }

    for (int idx : selectedIndices) {
        const auto& r = results[idx];
        DownloadItem item;
        item.url = "https://www.youtube.com/watch?v=" + r.id;
        item.title = r.artist.empty() ? r.title : (r.artist + " - " + r.title);
        item.artist = r.artist;
        item.useYtDlp = true;
        item.audioFormat = g_qualities[qualityIdx].format;
        item.audioQuality = g_qualities[qualityIdx].quality;
        items.push_back(std::move(item));
    }

    return true;
}

std::vector<YouTubeSearchResult> YouTubeSource::Search(const std::string& query, std::string& errorMsg) {
    std::vector<YouTubeSearchResult> results;

    std::string ytdlpPath = GetYtDlpPath();
    FB2K_console_formatter() << "[foo_downloader] YouTube search: " << query.c_str();

    // Use --flat-playlist for fast search
    std::string cmd = "\"" + ytdlpPath + "\" \"ytsearch15:" + query + "\" --flat-playlist -j --no-download --no-warnings";
    std::string output = RunProcess(cmd, 30000);

    if (output.empty()) {
        errorMsg = "yt-dlp search failed. Check console for details.";
        return results;
    }

    // Each line is a JSON object
    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty() || line[0] != '{') continue;

        YouTubeSearchResult r;
        r.id = ExtractJsonString(line, "id");
        r.title = ExtractJsonString(line, "title");
        r.artist = ExtractJsonString(line, "channel");
        if (r.artist.empty()) r.artist = ExtractJsonString(line, "uploader");
        r.duration = ExtractJsonInt(line, "duration");
        r.viewCount = ExtractJsonInt64(line, "view_count");
        r.uploadDate = ExtractJsonString(line, "upload_date");

        if (!r.id.empty() && !r.title.empty()) {
            results.push_back(std::move(r));
        }
    }

    FB2K_console_formatter() << "[foo_downloader] YouTube: found " << (uint32_t)results.size() << " result(s)";
    return results;
}

bool YouTubeSource::ShowSelectionDialog(const std::vector<YouTubeSearchResult>& results,
                                         std::vector<int>& selectedIndices,
                                         int& qualityIdx) {
    CYouTubeResultsDialog dlg(results);
    if (dlg.DoModal(core_api::get_main_window()) == IDOK) {
        selectedIndices = dlg.m_selected;
        qualityIdx = dlg.m_qualityIdx;
        if (qualityIdx < 0 || qualityIdx >= g_numQualities) qualityIdx = 0;
        return true;
    }
    return false;
}

// ============================================================================
// yt-dlp path and auto-download
// ============================================================================

std::string YouTubeSource::GetYtDlpPath() {
    // Look for yt-dlp.exe next to our DLL
    char dllPath[MAX_PATH] = {};
    HMODULE hMod = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&GetYtDlpPath, &hMod);
    if (hMod) {
        GetModuleFileNameA(hMod, dllPath, MAX_PATH);
        std::string dir(dllPath);
        auto lastSlash = dir.find_last_of("\\/");
        if (lastSlash != std::string::npos) {
            dir = dir.substr(0, lastSlash + 1);
        }
        return dir + "yt-dlp.exe";
    }
    return "yt-dlp.exe";
}

bool YouTubeSource::EnsureYtDlp(std::string& errorMsg) {
    std::string path = GetYtDlpPath();
    DWORD attr = GetFileAttributesA(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) {
        return true; // Already exists
    }

    FB2K_console_formatter() << "[foo_downloader] yt-dlp.exe not found, downloading...";

    if (!DownloadYtDlp()) {
        errorMsg = "Failed to download yt-dlp.exe. Please place it manually next to the component DLL.";
        return false;
    }

    // Verify it exists now
    attr = GetFileAttributesA(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        errorMsg = "yt-dlp.exe download seemed to succeed but file not found.";
        return false;
    }

    FB2K_console_formatter() << "[foo_downloader] yt-dlp.exe downloaded successfully.";
    return true;
}

bool YouTubeSource::DownloadYtDlp() {
    std::string destPath = GetYtDlpPath();
    std::string url = "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe";

    // Use PowerShell to download
    std::string psCmd = "powershell -NoProfile -ExecutionPolicy Bypass -Command \""
        "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; "
        "Invoke-WebRequest -Uri '" + url + "' -OutFile '" + destPath + "'\"";

    FB2K_console_formatter() << "[foo_downloader] Downloading yt-dlp.exe...";

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};

    std::vector<char> cmdBuf(psCmd.begin(), psCmd.end());
    cmdBuf.push_back(0);

    if (!CreateProcessA(NULL, cmdBuf.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        FB2K_console_formatter() << "[foo_downloader] Failed to start PowerShell for yt-dlp download.";
        return false;
    }

    WaitForSingleObject(pi.hProcess, 120000); // 2 min timeout
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return exitCode == 0;
}

// ============================================================================
// Process execution utility
// ============================================================================

std::string YouTubeSource::RunProcess(const std::string& cmdLine, int timeoutMs) {
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

    std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(0);

    if (!CreateProcessA(NULL, cmdBuf.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        FB2K_console_formatter() << "[foo_downloader] Failed to run: " << cmdLine.c_str();
        return "";
    }

    CloseHandle(hWritePipe); // Close write end in parent
    CloseHandle(pi.hThread);

    // Non-blocking read loop with message pumping to keep UI responsive
    std::string output;
    DWORD startTime = GetTickCount();

    while (true) {
        // Pump Windows messages so foobar2000 doesn't freeze
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // Check for data on pipe
        DWORD avail = 0;
        if (PeekNamedPipe(hReadPipe, NULL, 0, NULL, &avail, NULL) && avail > 0) {
            char buf[4096];
            DWORD toRead = (avail < sizeof(buf) - 1) ? avail : (DWORD)(sizeof(buf) - 1);
            DWORD bytesRead = 0;
            if (ReadFile(hReadPipe, buf, toRead, &bytesRead, NULL) && bytesRead > 0) {
                buf[bytesRead] = 0;
                output += buf;
            }
            continue; // Keep reading if data was available
        }

        // Check if process has exited
        DWORD exitCode = STILL_ACTIVE;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        if (exitCode != STILL_ACTIVE) {
            // Drain any remaining pipe data
            while (PeekNamedPipe(hReadPipe, NULL, 0, NULL, &avail, NULL) && avail > 0) {
                char buf[4096];
                DWORD toRead = (avail < sizeof(buf) - 1) ? avail : (DWORD)(sizeof(buf) - 1);
                DWORD bytesRead = 0;
                if (ReadFile(hReadPipe, buf, toRead, &bytesRead, NULL) && bytesRead > 0) {
                    buf[bytesRead] = 0;
                    output += buf;
                } else {
                    break;
                }
            }

            if (exitCode != 0) {
                FB2K_console_formatter() << "[foo_downloader] yt-dlp exited with code " << (uint32_t)exitCode;
            }
            break;
        }

        // Timeout check
        if (GetTickCount() - startTime > (DWORD)timeoutMs) {
            FB2K_console_formatter() << "[foo_downloader] yt-dlp timed out after " << timeoutMs << "ms";
            TerminateProcess(pi.hProcess, 1);
            break;
        }

        Sleep(10); // Don't spin too hard
    }

    CloseHandle(hReadPipe);
    CloseHandle(pi.hProcess);

    return output;
}

// ============================================================================
// JSON helpers (minimal, same pattern as FLAC downloader)
// ============================================================================

std::string YouTubeSource::ExtractJsonString(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\": \"";
    auto pos = json.find(search);
    if (pos == std::string::npos) {
        search = "\"" + key + "\":\"";
        pos = json.find(search);
        if (pos == std::string::npos) return "";
    }
    pos += search.size();

    std::string result;
    while (pos < json.size()) {
        char c = json[pos];
        if (c == '"') break;
        if (c == '\\' && pos + 1 < json.size()) {
            char next = json[pos + 1];
            if (next == '\\') { result += '\\'; pos += 2; continue; }
            if (next == '"')  { result += '"';  pos += 2; continue; }
            if (next == '/')  { result += '/';  pos += 2; continue; }
            if (next == 'n')  { result += '\n'; pos += 2; continue; }
            if (next == 'r')  { result += '\r'; pos += 2; continue; }
            if (next == 't')  { result += '\t'; pos += 2; continue; }
            result += next; pos += 2; continue;
        }
        result += c;
        pos++;
    }
    return result;
}

int YouTubeSource::ExtractJsonInt(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\": ";
    auto pos = json.find(search);
    if (pos == std::string::npos) {
        search = "\"" + key + "\":";
        pos = json.find(search);
        if (pos == std::string::npos) return 0;
    }
    pos += search.size();
    while (pos < json.size() && json[pos] == ' ') pos++;

    std::string num;
    while (pos < json.size() && (isdigit(json[pos]) || json[pos] == '-')) {
        num += json[pos++];
    }
    if (num.empty()) return 0;
    try { return std::stoi(num); } catch (...) { return 0; }
}

int64_t YouTubeSource::ExtractJsonInt64(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\": ";
    auto pos = json.find(search);
    if (pos == std::string::npos) {
        search = "\"" + key + "\":";
        pos = json.find(search);
        if (pos == std::string::npos) return 0;
    }
    pos += search.size();
    while (pos < json.size() && json[pos] == ' ') pos++;

    std::string num;
    while (pos < json.size() && (isdigit(json[pos]) || json[pos] == '-')) {
        num += json[pos++];
    }
    if (num.empty()) return 0;
    try { return std::stoll(num); } catch (...) { return 0; }
}

std::string YouTubeSource::FormatViewCount(int64_t count) {
    if (count <= 0) return "-";
    if (count >= 1000000000) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1fB", count / 1000000000.0);
        return buf;
    }
    if (count >= 1000000) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1fM", count / 1000000.0);
        return buf;
    }
    if (count >= 1000) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1fK", count / 1000.0);
        return buf;
    }
    return std::to_string(count);
}

std::string YouTubeSource::FormatUploadDate(const std::string& yyyymmdd) {
    // Input: "20231215" -> Output: "2023-12-15"
    if (yyyymmdd.size() != 8) return yyyymmdd;
    return yyyymmdd.substr(0, 4) + "-" + yyyymmdd.substr(4, 2) + "-" + yyyymmdd.substr(6, 2);
}

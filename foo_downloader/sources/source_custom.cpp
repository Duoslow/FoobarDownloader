#include "../stdafx.h"
#include "source_custom.h"
#include "../resource.h"
#include "../aria2_rpc.h"

#include <helpers/atl-misc.h>
#include <helpers/DarkMode.h>
#include <commctrl.h>
#include <sstream>

extern const char* GetConfigCustomSourceUrl();

// ============================================================================
// Search results selection dialog
// ============================================================================

class CSearchResultsDialog : public CDialogImpl<CSearchResultsDialog> {
public:
    enum { IDD = IDD_SEARCH_RESULTS };

    const std::vector<CustomSearchResult>& m_results;
    std::vector<int> m_selected;

    CSearchResultsDialog(const std::vector<CustomSearchResult>& results)
        : m_results(results) {}

    BEGIN_MSG_MAP_EX(CSearchResultsDialog)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_ID_HANDLER_EX(IDOK, OnOk)
        COMMAND_ID_HANDLER_EX(IDCANCEL, OnCancel)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM) {
        m_dark.AddDialogWithControls(*this);
        CenterWindow(GetParent());

        // Set info text
        pfc::string_formatter info;
        info << "Found " << (unsigned)m_results.size() << " result(s). Select tracks to download:";
        uSetDlgItemText(*this, IDC_RESULTS_INFO, info);

        // Setup ListView
        CListViewCtrl list(GetDlgItem(IDC_RESULTS_LIST));
        list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_CHECKBOXES);

        list.InsertColumn(0, L"Title", LVCFMT_LEFT, 160);
        list.InsertColumn(1, L"Artist", LVCFMT_LEFT, 100);
        list.InsertColumn(2, L"Album", LVCFMT_LEFT, 120);
        list.InsertColumn(3, L"Duration", LVCFMT_LEFT, 55);

        for (int i = 0; i < (int)m_results.size(); i++) {
            const auto& r = m_results[i];

            pfc::stringcvt::string_wide_from_utf8 wTitle(r.title.c_str());
            list.InsertItem(i, wTitle);

            pfc::stringcvt::string_wide_from_utf8 wArtist(r.artist.c_str());
            list.SetItemText(i, 1, wArtist);

            pfc::stringcvt::string_wide_from_utf8 wAlbum(r.album.c_str());
            list.SetItemText(i, 2, wAlbum);

            // Format duration as m:ss
            char durBuf[16];
            snprintf(durBuf, sizeof(durBuf), "%d:%02d", r.duration / 60, r.duration % 60);
            pfc::stringcvt::string_wide_from_utf8 wDur(durBuf);
            list.SetItemText(i, 3, wDur);

        }

        return FALSE;
    }

    void OnOk(UINT, int, CWindow) {
        CListViewCtrl list(GetDlgItem(IDC_RESULTS_LIST));
        m_selected.clear();

        for (int i = 0; i < (int)m_results.size(); i++) {
            if (list.GetCheckState(i)) {
                m_selected.push_back(i);
            }
        }

        EndDialog(IDOK);
    }

    void OnCancel(UINT, int, CWindow) {
        EndDialog(IDCANCEL);
    }

private:
    fb2k::CDarkModeHooks m_dark;
};

// ============================================================================
// CustomSource implementation
// ============================================================================

bool CustomSource::Resolve(const char* input, std::vector<DownloadItem>& items, std::string& errorMsg) {
    std::string baseUrl = GetConfigCustomSourceUrl();
    if (baseUrl.empty()) {
        errorMsg = "Custom source URL is not configured. Set it in Preferences > Tools > Downloader.";
        return false;
    }

    // Strip trailing slash
    while (!baseUrl.empty() && baseUrl.back() == '/') baseUrl.pop_back();

    if (!input || !*input) {
        errorMsg = "Please enter a search query (e.g., artist name or song title).";
        return false;
    }

    std::string query(input);

    // Search the API
    auto results = Search(query, errorMsg);
    if (results.empty()) {
        if (errorMsg.empty()) errorMsg = "No results found for: " + query;
        return false;
    }

    // Show selection dialog
    std::vector<int> selectedIndices;
    if (!ShowSelectionDialog(results, selectedIndices)) {
        errorMsg = ""; // User cancelled — not an error
        return false;
    }

    if (selectedIndices.empty()) {
        errorMsg = "No tracks selected.";
        return false;
    }

    // Convert selected results to download items
    for (int idx : selectedIndices) {
        const auto& r = results[idx];

        DownloadItem item;
        item.url = baseUrl + "/flac/download?t=" + r.id + "&f=FLAC";
        item.filename = r.artist + " - " + r.title + ".flac";
        item.title = r.artist + " - " + r.title;
        item.artist = r.artist;
        item.album = r.album;

        // Set headers for this source
        item.headers.push_back("Referer: " + baseUrl + "/");
        item.headers.push_back("User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/144.0.0.0 Safari/537.36");
        item.headers.push_back("Accept: */*");
        item.headers.push_back("Accept-Language: en-US,en;q=0.9");
        item.headers.push_back("Sec-Fetch-Dest: empty");
        item.headers.push_back("Sec-Fetch-Mode: cors");
        item.headers.push_back("Sec-Fetch-Site: same-origin");

        // Sanitize filename (remove invalid chars)
        for (char& c : item.filename) {
            if (c == '\\' || c == '/' || c == ':' || c == '*' ||
                c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
                c = '_';
            }
        }

        items.push_back(std::move(item));
    }

    return true;
}

std::vector<CustomSearchResult> CustomSource::Search(const std::string& query, std::string& errorMsg) {
    std::vector<CustomSearchResult> results;

    std::string baseUrl = GetConfigCustomSourceUrl();
    if (baseUrl.empty()) {
        errorMsg = "Custom source URL is not configured.";
        return results;
    }

    // Strip trailing slash
    while (!baseUrl.empty() && baseUrl.back() == '/') baseUrl.pop_back();

    std::string url = baseUrl + "/flac/search?query=" + UrlEncode(query);

    FB2K_console_formatter() << "[foo_downloader] Custom source search: " << query.c_str();

    std::string response = Aria2RpcClient::HttpGetUrl(url);

    if (response.empty()) {
        errorMsg = "Failed to connect to custom source";
        return results;
    }

    // Parse the JSON response: {"data": [...], "total": N}
    auto dataArray = ExtractJsonArray(response, "data");

    for (const auto& obj : dataArray) {
        CustomSearchResult r;

        // Extract track ID (numeric, convert to string)
        int id = ExtractJsonInt(obj, "id");
        if (id > 0) {
            r.id = std::to_string(id);
        } else {
            continue; // Skip entries without valid ID
        }

        r.title = ExtractJsonString(obj, "title");
        r.duration = ExtractJsonInt(obj, "duration");

        // Artist and album are nested objects
        r.artist = ExtractNestedString(obj, "artist", "name");
        r.album = ExtractNestedString(obj, "album", "title");

        if (!r.title.empty()) {
            results.push_back(std::move(r));
        }
    }

    FB2K_console_formatter() << "[foo_downloader] Found " << (uint32_t)results.size() << " result(s)";

    return results;
}

bool CustomSource::ShowSelectionDialog(const std::vector<CustomSearchResult>& results,
                                                std::vector<int>& selectedIndices) {
    CSearchResultsDialog dlg(results);
    if (dlg.DoModal(core_api::get_main_window()) == IDOK) {
        selectedIndices = dlg.m_selected;
        return true;
    }
    return false;
}

// ============================================================================
// Utility functions
// ============================================================================

std::string CustomSource::UrlEncode(const std::string& str) {
    std::string result;
    for (unsigned char c : str) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            result += c;
        } else if (c == ' ') {
            result += "%20";
        } else {
            char hex[4];
            snprintf(hex, sizeof(hex), "%%%02X", c);
            result += hex;
        }
    }
    return result;
}

std::string CustomSource::ExtractJsonString(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) {
        search = "\"" + key + "\": \"";
        pos = json.find(search);
        if (pos == std::string::npos) return "";
    }
    pos += search.size();

    // Read until closing quote, handling escapes
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

int CustomSource::ExtractJsonInt(const std::string& json, const std::string& key) {
    // Look for "key":number (not quoted)
    std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) {
        search = "\"" + key + "\": ";
        pos = json.find(search);
        if (pos == std::string::npos) return 0;
    }
    pos += search.size();

    // Skip whitespace
    while (pos < json.size() && json[pos] == ' ') pos++;

    // Read digits
    std::string num;
    while (pos < json.size() && (isdigit(json[pos]) || json[pos] == '-')) {
        num += json[pos++];
    }

    if (num.empty()) return 0;
    try { return std::stoi(num); } catch (...) { return 0; }
}

std::string CustomSource::ExtractNestedString(const std::string& json,
                                                       const std::string& object,
                                                       const std::string& key) {
    // Find the nested object first: "object":{...}
    std::string search = "\"" + object + "\":{";
    auto pos = json.find(search);
    if (pos == std::string::npos) {
        search = "\"" + object + "\": {";
        pos = json.find(search);
        if (pos == std::string::npos) return "";
    }

    // Find the closing brace of this object
    auto braceStart = json.find('{', pos);
    if (braceStart == std::string::npos) return "";

    int depth = 1;
    size_t endPos = braceStart + 1;
    while (endPos < json.size() && depth > 0) {
        if (json[endPos] == '{') depth++;
        else if (json[endPos] == '}') depth--;
        endPos++;
    }

    std::string nestedObj = json.substr(braceStart, endPos - braceStart);
    return ExtractJsonString(nestedObj, key);
}

std::vector<std::string> CustomSource::ExtractJsonArray(const std::string& json, const std::string& key) {
    std::vector<std::string> result;

    std::string search = "\"" + key + "\":[";
    auto pos = json.find(search);
    if (pos == std::string::npos) {
        search = "\"" + key + "\": [";
        pos = json.find(search);
        if (pos == std::string::npos) return result;
    }

    pos = json.find('[', pos);
    if (pos == std::string::npos) return result;

    pos++; // skip '['
    while (pos < json.size()) {
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == ',' || json[pos] == '\n' || json[pos] == '\r')) pos++;

        if (pos >= json.size() || json[pos] == ']') break;

        if (json[pos] == '{') {
            int depth = 1;
            size_t start = pos;
            pos++;
            while (pos < json.size() && depth > 0) {
                if (json[pos] == '{') depth++;
                else if (json[pos] == '}') depth--;
                pos++;
            }
            result.push_back(json.substr(start, pos - start));
        } else {
            pos++;
        }
    }

    return result;
}

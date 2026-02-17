#include "stdafx.h"
#include "guids.h"

#include <SDK/cfg_var.h>

#ifdef _WIN32
#include "resource.h"
#include <helpers/atl-misc.h>
#include <helpers/DarkMode.h>

#include "aria2_rpc.h"
#include "sources/source_youtube.h"

// ============================================================================
// Persistent configuration variables
// ============================================================================

// Download settings
static cfg_string cfg_output_folder(guid_cfg_output_folder, "");
static cfg_uint   cfg_max_concurrent(guid_cfg_max_concurrent, 3);
static cfg_uint   cfg_retry_count(guid_cfg_retry_count, 3);

// Playlist
static cfg_string cfg_playlist_name(guid_cfg_playlist_name, "Downloaded");
static cfg_bool   cfg_auto_playlist(guid_cfg_auto_playlist, true);

// Source enable/disable
static cfg_bool   cfg_enable_custom_source(guid_cfg_enable_custom_source, true);
static cfg_bool   cfg_enable_youtube(guid_cfg_enable_youtube, true);
static cfg_bool   cfg_enable_direct_url(guid_cfg_enable_direct_url, true);


// Custom Source
static cfg_string cfg_custom_source_url(guid_cfg_custom_source_url, "");

// YouTube / yt-dlp
static cfg_string cfg_ytdlp_path(guid_cfg_ytdlp_path, "");
static cfg_uint   cfg_yt_quality(guid_cfg_yt_quality, 0);
static cfg_bool   cfg_embed_metadata(guid_cfg_embed_metadata, true);
static cfg_string cfg_ytdlp_extra_flags(guid_cfg_ytdlp_extra_flags, "");

// aria2
static cfg_string cfg_aria2_path(guid_cfg_aria2_path, "");
static cfg_uint   cfg_aria2_port(guid_cfg_aria2_port, 6800);

// ============================================================================
// Quality labels (same order as source_youtube.cpp)
// ============================================================================

static const char* g_pref_quality_labels[] = {
    "FLAC (lossless)",
    "WAV (lossless)",
    "MP3 320kbps (best)",
    "MP3 256kbps",
    "MP3 192kbps",
    "MP3 128kbps",
    "MP3 96kbps (low)",
    "AAC 256kbps",
    "AAC 128kbps",
    "Opus (best)",
    "Vorbis (best)",
};
static const int g_numPrefQualities = sizeof(g_pref_quality_labels) / sizeof(g_pref_quality_labels[0]);

// ============================================================================
// Helper: get the directory containing our component DLL
// ============================================================================
static std::string GetComponentDir() {
    char dllPath[MAX_PATH] = {};
    HMODULE hMod = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&GetComponentDir, &hMod);
    if (hMod) {
        GetModuleFileNameA(hMod, dllPath, MAX_PATH);
        std::string dir(dllPath);
        auto pos = dir.find_last_of("\\/");
        if (pos != std::string::npos) return dir.substr(0, pos + 1);
    }
    return "";
}

// ============================================================================
// Public accessors
// ============================================================================

const char* GetConfigOutputFolder() { return cfg_output_folder; }
int GetConfigMaxConcurrent() { return (int)cfg_max_concurrent.get(); }
const char* GetConfigPlaylistName() { return cfg_playlist_name; }
bool GetConfigAutoPlaylist() { return cfg_auto_playlist; }
bool GetConfigEnableCustomSource() { return cfg_enable_custom_source && strlen(cfg_custom_source_url) > 0; }
const char* GetConfigCustomSourceUrl() { return cfg_custom_source_url; }
bool GetConfigEnableYoutube() { return cfg_enable_youtube; }
bool GetConfigEnableDirectUrl() { return cfg_enable_direct_url; }
const char* GetConfigYtDlpPath() { return cfg_ytdlp_path; }
int GetConfigYtQuality() { return (int)cfg_yt_quality.get(); }
bool GetConfigEmbedMetadata() { return cfg_embed_metadata; }
const char* GetConfigYtDlpExtraFlags() { return cfg_ytdlp_extra_flags; }
const char* GetConfigAria2Path() { return cfg_aria2_path; }
int GetConfigAria2Port() { return (int)cfg_aria2_port.get(); }
int GetConfigRetryCount() { return (int)cfg_retry_count.get(); }

namespace {

// ============================================================================
// Main preferences page: Download Settings + Playlist + Sources
// ============================================================================

class CMainPreferences : public CDialogImpl<CMainPreferences>, public preferences_page_instance {
public:
    CMainPreferences(preferences_page_callback::ptr callback) : m_callback(callback) {}

    enum { IDD = IDD_PREFERENCES };

    t_uint32 get_state() {
        t_uint32 state = preferences_state::resettable | preferences_state::dark_mode_supported;
        if (HasChanged()) state |= preferences_state::changed;
        return state;
    }

    void apply() {
        pfc::string8 folder, playlistName;
        uGetDlgItemText(*this, IDC_OUTPUT_FOLDER, folder);
        uGetDlgItemText(*this, IDC_PLAYLIST_NAME, playlistName);

        cfg_output_folder = folder;

        UINT maxDl = GetDlgItemInt(IDC_MAX_CONCURRENT, nullptr, FALSE);
        if (maxDl > 0 && maxDl <= 16) cfg_max_concurrent = maxDl;

        UINT retryCount = GetDlgItemInt(IDC_RETRY_COUNT, nullptr, FALSE);
        if (retryCount <= 99) cfg_retry_count = retryCount;

        cfg_auto_playlist = (IsDlgButtonChecked(IDC_AUTO_ADD_PLAYLIST) == BST_CHECKED);
        if (playlistName.length() > 0) cfg_playlist_name = playlistName;

        cfg_enable_custom_source = (IsDlgButtonChecked(IDC_ENABLE_CUSTOM_SOURCE) == BST_CHECKED);
        cfg_enable_youtube = (IsDlgButtonChecked(IDC_ENABLE_YOUTUBE) == BST_CHECKED);
        cfg_enable_direct_url = (IsDlgButtonChecked(IDC_ENABLE_DIRECT_URL) == BST_CHECKED);

        // Apply output dir to aria2
        auto& aria2 = Aria2RpcClient::instance();
        if (folder.length() > 0) aria2.SetOutputDir(folder.get_ptr());
        aria2.SetMaxConcurrent((int)cfg_max_concurrent.get());

        OnChanged();
    }

    void reset() {
        uSetDlgItemText(*this, IDC_OUTPUT_FOLDER, GetDefaultOutputFolder().c_str());
        SetDlgItemInt(IDC_MAX_CONCURRENT, 3, FALSE);
        SetDlgItemInt(IDC_RETRY_COUNT, 3, FALSE);
        CheckDlgButton(IDC_AUTO_ADD_PLAYLIST, BST_CHECKED);
        uSetDlgItemText(*this, IDC_PLAYLIST_NAME, "Downloaded");
        CheckDlgButton(IDC_ENABLE_CUSTOM_SOURCE, BST_CHECKED);
        CheckDlgButton(IDC_ENABLE_YOUTUBE, BST_CHECKED);
        CheckDlgButton(IDC_ENABLE_DIRECT_URL, BST_CHECKED);
        OnChanged();
    }

    BEGIN_MSG_MAP_EX(CMainPreferences)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_HANDLER_EX(IDC_BROWSE_FOLDER, BN_CLICKED, OnBrowseFolder)
        COMMAND_HANDLER_EX(IDC_OPEN_FOLDER_BTN, BN_CLICKED, OnOpenFolder)
        COMMAND_HANDLER_EX(IDC_OUTPUT_FOLDER, EN_CHANGE, OnEditChange)
        COMMAND_HANDLER_EX(IDC_MAX_CONCURRENT, EN_CHANGE, OnEditChange)
        COMMAND_HANDLER_EX(IDC_RETRY_COUNT, EN_CHANGE, OnEditChange)
        COMMAND_HANDLER_EX(IDC_PLAYLIST_NAME, EN_CHANGE, OnEditChange)
        COMMAND_HANDLER_EX(IDC_AUTO_ADD_PLAYLIST, BN_CLICKED, OnEditChange)
        COMMAND_HANDLER_EX(IDC_ENABLE_CUSTOM_SOURCE, BN_CLICKED, OnEditChange)
        COMMAND_HANDLER_EX(IDC_ENABLE_YOUTUBE, BN_CLICKED, OnEditChange)
        COMMAND_HANDLER_EX(IDC_ENABLE_DIRECT_URL, BN_CLICKED, OnEditChange)
    END_MSG_MAP()

private:
    static std::string GetDefaultOutputFolder() {
        char musicPath[MAX_PATH];
        if (SHGetFolderPathA(NULL, CSIDL_MYMUSIC, NULL, 0, musicPath) == S_OK) {
            return std::string(musicPath) + "\\foo_downloader";
        }
        return "C:\\Music\\foo_downloader";
    }

    BOOL OnInitDialog(CWindow, LPARAM) {
        m_dark.AddDialogWithControls(*this);
        pfc::string8 folder(cfg_output_folder);
        if (folder.is_empty()) {
            folder = GetDefaultOutputFolder().c_str();
        }
        uSetDlgItemText(*this, IDC_OUTPUT_FOLDER, folder);
        SetDlgItemInt(IDC_MAX_CONCURRENT, (UINT)cfg_max_concurrent.get(), FALSE);
        SetDlgItemInt(IDC_RETRY_COUNT, (UINT)cfg_retry_count.get(), FALSE);
        CheckDlgButton(IDC_AUTO_ADD_PLAYLIST, cfg_auto_playlist ? BST_CHECKED : BST_UNCHECKED);
        uSetDlgItemText(*this, IDC_PLAYLIST_NAME, cfg_playlist_name);
        CheckDlgButton(IDC_ENABLE_CUSTOM_SOURCE, cfg_enable_custom_source ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_ENABLE_YOUTUBE, cfg_enable_youtube ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_ENABLE_DIRECT_URL, cfg_enable_direct_url ? BST_CHECKED : BST_UNCHECKED);
        return FALSE;
    }

    void OnBrowseFolder(UINT, int, CWindow) {
        pfc::string8 path;
        if (uBrowseForFolder(*this, "Select download output folder", path)) {
            uSetDlgItemText(*this, IDC_OUTPUT_FOLDER, path);
            OnChanged();
        }
    }

    void OnOpenFolder(UINT, int, CWindow) {
        pfc::string8 folder;
        uGetDlgItemText(*this, IDC_OUTPUT_FOLDER, folder);
        if (folder.is_empty()) {
            char musicPath[MAX_PATH];
            if (SHGetFolderPathA(NULL, CSIDL_MYMUSIC, NULL, 0, musicPath) == S_OK) {
                folder = musicPath;
                folder += "\\foo_downloader";
            }
        }
        if (!folder.is_empty()) {
            CreateDirectoryA(folder.get_ptr(), NULL);
            pfc::stringcvt::string_wide_from_utf8 wFolder(folder);
            ShellExecuteW(NULL, L"open", wFolder, NULL, NULL, SW_SHOWNORMAL);
        }
    }

    void OnEditChange(UINT, int, CWindow) { OnChanged(); }

    bool HasChanged() {
        pfc::string8 folder, playlistName;
        uGetDlgItemText(*this, IDC_OUTPUT_FOLDER, folder);
        uGetDlgItemText(*this, IDC_PLAYLIST_NAME, playlistName);
        UINT maxDl = GetDlgItemInt(IDC_MAX_CONCURRENT, nullptr, FALSE);
        UINT retryCount = GetDlgItemInt(IDC_RETRY_COUNT, nullptr, FALSE);
        bool autoPlaylist = (IsDlgButtonChecked(IDC_AUTO_ADD_PLAYLIST) == BST_CHECKED);
        bool enCustom = (IsDlgButtonChecked(IDC_ENABLE_CUSTOM_SOURCE) == BST_CHECKED);
        bool enYt = (IsDlgButtonChecked(IDC_ENABLE_YOUTUBE) == BST_CHECKED);
        bool enDirect = (IsDlgButtonChecked(IDC_ENABLE_DIRECT_URL) == BST_CHECKED);

        return strcmp(folder, cfg_output_folder) != 0
            || strcmp(playlistName, cfg_playlist_name) != 0
            || maxDl != cfg_max_concurrent.get()
            || retryCount != cfg_retry_count.get()
            || autoPlaylist != (bool)cfg_auto_playlist
            || enCustom != (bool)cfg_enable_custom_source
            || enYt != (bool)cfg_enable_youtube
            || enDirect != (bool)cfg_enable_direct_url;
    }

    void OnChanged() { m_callback->on_state_changed(); }

    const preferences_page_callback::ptr m_callback;
    fb2k::CDarkModeHooks m_dark;
};

// ============================================================================
// YouTube / yt-dlp sub-page
// ============================================================================

class CYouTubePreferences : public CDialogImpl<CYouTubePreferences>, public preferences_page_instance {
public:
    CYouTubePreferences(preferences_page_callback::ptr callback) : m_callback(callback) {}

    enum { IDD = IDD_PREF_YOUTUBE };

    t_uint32 get_state() {
        t_uint32 state = preferences_state::resettable | preferences_state::dark_mode_supported;
        if (HasChanged()) state |= preferences_state::changed;
        return state;
    }

    void apply() {
        pfc::string8 ytdlpPath, extraFlags;
        uGetDlgItemText(*this, IDC_YTDLP_PATH, ytdlpPath);
        uGetDlgItemText(*this, IDC_YTDLP_EXTRA_FLAGS, extraFlags);
        cfg_ytdlp_path = ytdlpPath;
        cfg_ytdlp_extra_flags = extraFlags;

        CComboBox qualCombo(GetDlgItem(IDC_YT_DEFAULT_QUALITY));
        int qualIdx = qualCombo.GetCurSel();
        if (qualIdx >= 0 && qualIdx < g_numPrefQualities) {
            cfg_yt_quality = (t_uint32)qualIdx;
        }

        cfg_embed_metadata = (IsDlgButtonChecked(IDC_EMBED_METADATA) == BST_CHECKED);
        OnChanged();
    }

    void reset() {
        std::string defaultYtDlp = GetComponentDir() + "yt-dlp.exe";
        uSetDlgItemText(*this, IDC_YTDLP_PATH, defaultYtDlp.c_str());
        uSetDlgItemText(*this, IDC_YTDLP_EXTRA_FLAGS, "");
        CComboBox qualCombo(GetDlgItem(IDC_YT_DEFAULT_QUALITY));
        qualCombo.SetCurSel(0);
        CheckDlgButton(IDC_EMBED_METADATA, BST_CHECKED);
        OnChanged();
    }

    BEGIN_MSG_MAP_EX(CYouTubePreferences)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_HANDLER_EX(IDC_BROWSE_YTDLP, BN_CLICKED, OnBrowseYtDlp)
        COMMAND_HANDLER_EX(IDC_YTDLP_PATH, EN_CHANGE, OnEditChange)
        COMMAND_HANDLER_EX(IDC_YTDLP_EXTRA_FLAGS, EN_CHANGE, OnEditChange)
        COMMAND_HANDLER_EX(IDC_YT_DEFAULT_QUALITY, CBN_SELCHANGE, OnEditChange)
        COMMAND_HANDLER_EX(IDC_EMBED_METADATA, BN_CLICKED, OnEditChange)
    END_MSG_MAP()

private:
    BOOL OnInitDialog(CWindow, LPARAM) {
        m_dark.AddDialogWithControls(*this);

        // Show configured path, or auto-detect if empty
        pfc::string8 ytPath(cfg_ytdlp_path);
        if (ytPath.is_empty()) {
            std::string detected = GetComponentDir() + "yt-dlp.exe";
            ytPath = detected.c_str();
        }
        uSetDlgItemText(*this, IDC_YTDLP_PATH, ytPath);
        uSetDlgItemText(*this, IDC_YTDLP_EXTRA_FLAGS, cfg_ytdlp_extra_flags);

        CComboBox qualCombo(GetDlgItem(IDC_YT_DEFAULT_QUALITY));
        for (int i = 0; i < g_numPrefQualities; i++) {
            pfc::stringcvt::string_wide_from_utf8 wLabel(g_pref_quality_labels[i]);
            qualCombo.AddString(wLabel);
        }
        int selQual = (int)cfg_yt_quality.get();
        if (selQual < 0 || selQual >= g_numPrefQualities) selQual = 0;
        qualCombo.SetCurSel(selQual);

        CheckDlgButton(IDC_EMBED_METADATA, cfg_embed_metadata ? BST_CHECKED : BST_UNCHECKED);
        return FALSE;
    }

    void OnBrowseYtDlp(UINT, int, CWindow) {
        pfc::string8 path;
        if (uGetOpenFileName(*this, "Executables|*.exe", 0, nullptr, "Select yt-dlp.exe", nullptr, path, FALSE)) {
            uSetDlgItemText(*this, IDC_YTDLP_PATH, path);
            OnChanged();
        }
    }

    void OnEditChange(UINT, int, CWindow) { OnChanged(); }

    bool HasChanged() {
        pfc::string8 ytdlpPath, extraFlags;
        uGetDlgItemText(*this, IDC_YTDLP_PATH, ytdlpPath);
        uGetDlgItemText(*this, IDC_YTDLP_EXTRA_FLAGS, extraFlags);
        CComboBox qualCombo(GetDlgItem(IDC_YT_DEFAULT_QUALITY));
        int qualIdx = qualCombo.GetCurSel();
        if (qualIdx < 0) qualIdx = 0;
        bool embedMeta = (IsDlgButtonChecked(IDC_EMBED_METADATA) == BST_CHECKED);

        return strcmp(ytdlpPath, cfg_ytdlp_path) != 0
            || strcmp(extraFlags, cfg_ytdlp_extra_flags) != 0
            || (UINT)qualIdx != cfg_yt_quality.get()
            || embedMeta != (bool)cfg_embed_metadata;
    }

    void OnChanged() { m_callback->on_state_changed(); }

    const preferences_page_callback::ptr m_callback;
    fb2k::CDarkModeHooks m_dark;
};

// ============================================================================
// aria2 Engine sub-page
// ============================================================================

class CAria2Preferences : public CDialogImpl<CAria2Preferences>, public preferences_page_instance {
public:
    CAria2Preferences(preferences_page_callback::ptr callback) : m_callback(callback) {}

    enum { IDD = IDD_PREF_ARIA2 };

    t_uint32 get_state() {
        t_uint32 state = preferences_state::resettable | preferences_state::dark_mode_supported;
        if (HasChanged()) state |= preferences_state::changed;
        return state;
    }

    void apply() {
        pfc::string8 aria2path;
        uGetDlgItemText(*this, IDC_ARIA2_PATH, aria2path);
        cfg_aria2_path = aria2path;

        UINT port = GetDlgItemInt(IDC_ARIA2_PORT, nullptr, FALSE);
        if (port > 0 && port < 65536) cfg_aria2_port = port;

        auto& aria2 = Aria2RpcClient::instance();
        aria2.SetPort((int)cfg_aria2_port.get());
        if (aria2path.length() > 0) aria2.SetAria2Path(aria2path.get_ptr());

        OnChanged();
    }

    void reset() {
        std::string defaultAria2 = GetComponentDir() + "aria2c.exe";
        uSetDlgItemText(*this, IDC_ARIA2_PATH, defaultAria2.c_str());
        SetDlgItemInt(IDC_ARIA2_PORT, 6800, FALSE);
        OnChanged();
    }

    BEGIN_MSG_MAP_EX(CAria2Preferences)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_HANDLER_EX(IDC_BROWSE_ARIA2, BN_CLICKED, OnBrowseAria2)
        COMMAND_HANDLER_EX(IDC_ARIA2_PATH, EN_CHANGE, OnEditChange)
        COMMAND_HANDLER_EX(IDC_ARIA2_PORT, EN_CHANGE, OnEditChange)
    END_MSG_MAP()

private:
    BOOL OnInitDialog(CWindow, LPARAM) {
        m_dark.AddDialogWithControls(*this);

        // Show configured path, or auto-detect if empty
        pfc::string8 ariaPath(cfg_aria2_path);
        if (ariaPath.is_empty()) {
            std::string detected = GetComponentDir() + "aria2c.exe";
            ariaPath = detected.c_str();
        }
        uSetDlgItemText(*this, IDC_ARIA2_PATH, ariaPath);
        SetDlgItemInt(IDC_ARIA2_PORT, (UINT)cfg_aria2_port.get(), FALSE);
        return FALSE;
    }

    void OnBrowseAria2(UINT, int, CWindow) {
        pfc::string8 path;
        if (uGetOpenFileName(*this, "Executables|*.exe", 0, nullptr, "Select aria2c.exe", nullptr, path, FALSE)) {
            uSetDlgItemText(*this, IDC_ARIA2_PATH, path);
            OnChanged();
        }
    }

    void OnEditChange(UINT, int, CWindow) { OnChanged(); }

    bool HasChanged() {
        pfc::string8 aria2path;
        uGetDlgItemText(*this, IDC_ARIA2_PATH, aria2path);
        UINT port = GetDlgItemInt(IDC_ARIA2_PORT, nullptr, FALSE);

        return strcmp(aria2path, cfg_aria2_path) != 0
            || port != cfg_aria2_port.get();
    }

    void OnChanged() { m_callback->on_state_changed(); }

    const preferences_page_callback::ptr m_callback;
    fb2k::CDarkModeHooks m_dark;
};

// ============================================================================
// Custom Source sub-page
// ============================================================================

class CCustomSourcePreferences : public CDialogImpl<CCustomSourcePreferences>, public preferences_page_instance {
public:
    CCustomSourcePreferences(preferences_page_callback::ptr callback) : m_callback(callback) {}

    enum { IDD = IDD_PREF_CUSTOM_SOURCE };

    t_uint32 get_state() {
        t_uint32 state = preferences_state::resettable | preferences_state::dark_mode_supported;
        if (HasChanged()) state |= preferences_state::changed;
        return state;
    }

    void apply() {
        pfc::string8 url;
        uGetDlgItemText(*this, IDC_CUSTOM_SOURCE_URL, url);
        cfg_custom_source_url = url;
        OnChanged();
    }

    void reset() {
        uSetDlgItemText(*this, IDC_CUSTOM_SOURCE_URL, "");
        OnChanged();
    }

    BEGIN_MSG_MAP_EX(CCustomSourcePreferences)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_HANDLER_EX(IDC_CUSTOM_SOURCE_URL, EN_CHANGE, OnEditChange)
        COMMAND_HANDLER_EX(IDC_TEST_CUSTOM_SOURCE, BN_CLICKED, OnTest)
    END_MSG_MAP()

private:
    BOOL OnInitDialog(CWindow, LPARAM) {
        m_dark.AddDialogWithControls(*this);
        uSetDlgItemText(*this, IDC_CUSTOM_SOURCE_URL, cfg_custom_source_url);
        return FALSE;
    }

    void OnEditChange(UINT, int, CWindow) { OnChanged(); }

    void OnTest(UINT, int, CWindow) {
        pfc::string8 url;
        uGetDlgItemText(*this, IDC_CUSTOM_SOURCE_URL, url);

        if (url.is_empty()) {
            popup_message::g_show("Please enter a base URL first.", "Custom Source", popup_message::icon_error);
            return;
        }

        // Strip trailing slash
        std::string baseUrl(url.get_ptr());
        while (!baseUrl.empty() && baseUrl.back() == '/') baseUrl.pop_back();

        // Try a test search request
        std::string testUrl = baseUrl + "/flac/search?query=test";
        std::string response = Aria2RpcClient::HttpGetUrl(testUrl);

        if (response.empty()) {
            popup_message::g_show("Connection failed. Could not reach the server.", "Custom Source", popup_message::icon_error);
        } else if (response.find("\"data\"") != std::string::npos) {
            popup_message::g_show("Connection successful! The server responded with valid data.", "Custom Source", popup_message::icon_information);
        } else {
            popup_message::g_show("Connected, but the response does not look like a valid API.\n\nExpected JSON with a \"data\" array.", "Custom Source", popup_message::icon_error);
        }
    }

    bool HasChanged() {
        pfc::string8 url;
        uGetDlgItemText(*this, IDC_CUSTOM_SOURCE_URL, url);
        return strcmp(url, cfg_custom_source_url) != 0;
    }

    void OnChanged() { m_callback->on_state_changed(); }

    const preferences_page_callback::ptr m_callback;
    fb2k::CDarkModeHooks m_dark;
};

// ============================================================================
// Page factories
// ============================================================================

// Main page: Tools > Downloader
class preferences_page_main : public preferences_page_impl<CMainPreferences> {
public:
    const char* get_name() { return "Downloader"; }
    GUID get_guid() { return guid_downloader_preferences; }
    GUID get_parent_guid() { return guid_tools; }
};

// Sub-page: Tools > Downloader > YouTube / yt-dlp
class preferences_page_youtube : public preferences_page_impl<CYouTubePreferences> {
public:
    const char* get_name() { return "YouTube / yt-dlp"; }
    GUID get_guid() { return guid_pref_youtube; }
    GUID get_parent_guid() { return guid_downloader_preferences; }
};

// Sub-page: Tools > Downloader > aria2 Engine
class preferences_page_aria2 : public preferences_page_impl<CAria2Preferences> {
public:
    const char* get_name() { return "aria2 Engine"; }
    GUID get_guid() { return guid_pref_aria2; }
    GUID get_parent_guid() { return guid_downloader_preferences; }
};

// Sub-page: Tools > Downloader > Custom Source
class preferences_page_custom_source : public preferences_page_impl<CCustomSourcePreferences> {
public:
    const char* get_name() { return "Custom Source"; }
    GUID get_guid() { return guid_pref_custom_source; }
    GUID get_parent_guid() { return guid_downloader_preferences; }
};

static preferences_page_factory_t<preferences_page_main> g_pref_main_factory;
static preferences_page_factory_t<preferences_page_youtube> g_pref_youtube_factory;
static preferences_page_factory_t<preferences_page_aria2> g_pref_aria2_factory;
static preferences_page_factory_t<preferences_page_custom_source> g_pref_custom_source_factory;

} // namespace

#endif // _WIN32

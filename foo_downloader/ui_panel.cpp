#include "stdafx.h"
#include "guids.h"
#include "resource.h"
#include "download_manager.h"
#include "source_manager.h"

#include <libPPUI/win32_utility.h>
#include <libPPUI/win32_op.h>
#include <helpers/atl-misc.h>
#include <libPPUI/DarkMode.h>

#include <commctrl.h>
#include <shellapi.h>

namespace {

static const int TOOLBAR_HEIGHT = 24;
static const int STATUSBAR_HEIGHT = 18;
static const int MARGIN = 2;

enum {
    ID_CTX_PLAY = 5000,
    ID_CTX_OPEN_FOLDER,
    ID_CTX_REMOVE,
    ID_CTX_CANCEL,
    ID_CTX_PAUSE,
    ID_CTX_RESUME,
};

class CDownloaderPanel : public CDialogImpl<CDownloaderPanel>, public ui_element_instance {
public:
    CDownloaderPanel(ui_element_config::ptr cfg, ui_element_instance_callback::ptr cb)
        : m_callback(cb) {}

    enum { IDD = IDD_DOWNLOADER_PANEL };

    BEGIN_MSG_MAP_EX(CDownloaderPanel)
        MSG_WM_INITDIALOG(OnInitDialog)
        MSG_WM_DESTROY(OnDestroy)
        MSG_WM_SIZE(OnSize)
        MSG_WM_TIMER(OnTimer)
        COMMAND_HANDLER_EX(IDC_DOWNLOAD_BTN, BN_CLICKED, OnDownloadClick)
        COMMAND_HANDLER_EX(IDC_CLEAR_COMPLETED_BTN, BN_CLICKED, OnClearClick)
        COMMAND_HANDLER_EX(IDC_SOURCE_COMBO, CBN_SELCHANGE, OnSourceChanged)
        MSG_WM_CONTEXTMENU(OnContextMenu)
    END_MSG_MAP()

    void initialize_window(HWND parent) { WIN32_OP(Create(parent) != NULL); }
    HWND get_wnd() { return m_hWnd; }

    void set_configuration(ui_element_config::ptr config) {}
    ui_element_config::ptr get_configuration() {
        return ui_element_config::g_create_empty(g_get_guid());
    }

    static GUID g_get_guid() { return guid_downloader_ui_element; }
    static GUID g_get_subclass() { return ui_element_subclass_utility; }
    static void g_get_name(pfc::string_base& out) { out = "Downloader"; }
    static const char* g_get_description() { return "Download manager panel with URL input and queue display"; }
    static ui_element_config::ptr g_get_default_configuration() {
        return ui_element_config::g_create_empty(g_get_guid());
    }

    void notify(const GUID& p_what, t_size, const void*, t_size) override {
        if (p_what == ui_element_notify_colors_changed) {
            applyDark();
        }
    }

private:
    void applyDark() {
        t_ui_color color = 0;
        if (m_callback->query_color(ui_color_darkmode, color)) {
            m_dark.SetDark(color == 0);
        }
    }

    BOOL OnInitDialog(CWindow, LPARAM) {
        applyDark();
        m_dark.AddDialogWithControls(*this);

        // Set cue text on URL input
        CEdit urlEdit(GetDlgItem(IDC_URL_INPUT));
        urlEdit.SetCueBannerText(L"Search or enter URL...");

        // Populate source dropdown (only enabled sources)
        PopulateSourceCombo();

        InitListView();
        SetTimer(1, 500);

        return FALSE;
    }

    void OnDestroy() {
        KillTimer(1);

    }

    void OnSize(UINT, CSize size) {
        if (!IsWindow()) return;

        CRect rc;
        GetClientRect(&rc);
        int w = rc.right;
        int h = rc.bottom;

        if (w < 50 || h < 50) return;

        // Top toolbar: [Source combo] [URL input...........] [Download]
        int btnW = 66;
        int comboW = 80;
        int urlX = MARGIN + comboW + 4;
        int urlW = w - urlX - btnW - MARGIN - 4;
        if (urlW < 40) urlW = 40;

        ::MoveWindow(GetDlgItem(IDC_SOURCE_COMBO), MARGIN, MARGIN, comboW, 200, TRUE);
        ::MoveWindow(GetDlgItem(IDC_URL_INPUT), urlX, MARGIN, urlW, 22, TRUE);
        ::MoveWindow(GetDlgItem(IDC_DOWNLOAD_BTN), w - btnW - MARGIN, MARGIN, btnW, 22, TRUE);

        // ListView fills the middle
        int listTop = TOOLBAR_HEIGHT + MARGIN;
        int listH = h - listTop - STATUSBAR_HEIGHT - MARGIN;
        if (listH < 20) listH = 20;
        ::MoveWindow(GetDlgItem(IDC_QUEUE_LIST), MARGIN, listTop, w - MARGIN * 2, listH, TRUE);

        // Auto-size list columns based on width
        AutoSizeColumns(w - MARGIN * 2);

        // Bottom bar: [Clear Done] [Status text........................]
        int bottomY = h - STATUSBAR_HEIGHT;
        int clearBtnW = 64;
        int clearBtnH = 18;
        ::MoveWindow(GetDlgItem(IDC_CLEAR_COMPLETED_BTN), MARGIN, bottomY, clearBtnW, clearBtnH, TRUE);
        ::MoveWindow(GetDlgItem(IDC_STATUS_BAR), MARGIN + clearBtnW + 6, bottomY + 3, w - clearBtnW - MARGIN * 2 - 8, 12, TRUE);
    }

    void AutoSizeColumns(int listWidth) {
        CListViewCtrl list(GetDlgItem(IDC_QUEUE_LIST));
        if (!list.IsWindow()) return;

        // Subtract scrollbar width and a small margin
        int available = listWidth - GetSystemMetrics(SM_CXVSCROLL) - 4;
        if (available < 100) return;

        // Title: 40%, Status: 15%, Progress: 15%, Speed: 15%, Source: 15%
        list.SetColumnWidth(0, available * 40 / 100);
        list.SetColumnWidth(1, available * 15 / 100);
        list.SetColumnWidth(2, available * 15 / 100);
        list.SetColumnWidth(3, available * 15 / 100);
        list.SetColumnWidth(4, available * 15 / 100);
    }

    void OnTimer(UINT_PTR id) {
        if (id == 1) RefreshDownloadList();
    }

    void OnDownloadClick(UINT, int, CWindow) {
        CComboBox combo(GetDlgItem(IDC_SOURCE_COMBO));
        int selIdx = combo.GetCurSel();
        if (selIdx < 0) {
            popup_message::g_show("Please select a download source.", "foo_downloader", popup_message::icon_error);
            return;
        }

        if (selIdx >= (int)m_enabledSources.size()) return;
        std::string sourceId = m_enabledSources[selIdx]->GetId();

        pfc::string8 url;
        uGetDlgItemText(*this, IDC_URL_INPUT, url);

        if (url.is_empty()) {
            popup_message::g_show("Please enter a URL.", "foo_downloader", popup_message::icon_error);
            return;
        }

        if (DownloadManager::instance().StartDownload(sourceId, url.get_ptr())) {
            uSetDlgItemText(*this, IDC_URL_INPUT, "");
        }
    }

    void OnClearClick(UINT, int, CWindow) {
        DownloadManager::instance().ClearCompleted();
        RefreshDownloadList();
    }

    void OnSourceChanged(UINT, int, CWindow) {
        CComboBox combo(GetDlgItem(IDC_SOURCE_COMBO));
        UpdateButtonLabel(combo.GetCurSel());
    }

    void PopulateSourceCombo() {
        CComboBox combo(GetDlgItem(IDC_SOURCE_COMBO));
        combo.ResetContent();
        m_enabledSources = SourceManager::instance().GetEnabled();
        for (auto* src : m_enabledSources) {
            combo.AddString(pfc::stringcvt::string_wide_from_utf8(src->GetName()));
        }
        if (!m_enabledSources.empty()) {
            combo.SetCurSel(0);
            UpdateButtonLabel(0);
        }
    }

    void UpdateButtonLabel(int sourceIdx) {
        const char* label = "Download";
        if (sourceIdx >= 0 && sourceIdx < (int)m_enabledSources.size()) {
            label = m_enabledSources[sourceIdx]->GetActionLabel();
        }
        uSetDlgItemText(*this, IDC_DOWNLOAD_BTN, label);
    }

    void OnContextMenu(CWindow wnd, CPoint pt) {
        // Only handle right-click on the queue list
        if (wnd.m_hWnd != GetDlgItem(IDC_QUEUE_LIST)) {
            SetMsgHandled(FALSE);
            return;
        }

        CListViewCtrl list(GetDlgItem(IDC_QUEUE_LIST));

        // Hit test to find which item was clicked
        POINT clientPt = pt;
        list.ScreenToClient(&clientPt);
        LVHITTESTINFO hti = {};
        hti.pt = clientPt;
        int idx = list.HitTest(&hti);
        if (idx < 0) return;

        auto downloads = DownloadManager::instance().GetDownloads();
        if (idx >= (int)downloads.size()) return;

        const auto& dl = downloads[idx];

        // Build context menu based on status
        CMenu menu;
        menu.CreatePopupMenu();

        if (dl.status == "complete" && !dl.outputPath.empty()) {
            menu.AppendMenu(MF_STRING, ID_CTX_PLAY, L"Play");
            menu.AppendMenu(MF_STRING, ID_CTX_OPEN_FOLDER, L"Open folder");
            menu.AppendMenu(MF_SEPARATOR);
        }

        if (dl.status == "active" || dl.status == "queued") {
            if (dl.engine == "aria2") {
                menu.AppendMenu(MF_STRING, ID_CTX_PAUSE, L"Pause");
            }
            menu.AppendMenu(MF_STRING, ID_CTX_CANCEL, L"Cancel");
            menu.AppendMenu(MF_SEPARATOR);
        }

        if (dl.status == "paused") {
            menu.AppendMenu(MF_STRING, ID_CTX_RESUME, L"Resume");
            menu.AppendMenu(MF_STRING, ID_CTX_CANCEL, L"Cancel");
            menu.AppendMenu(MF_SEPARATOR);
        }

        menu.AppendMenu(MF_STRING, ID_CTX_REMOVE, L"Remove");

        int cmd = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, m_hWnd);

        if (cmd == ID_CTX_PLAY) {
            PlayFile(dl.outputPath);
        } else if (cmd == ID_CTX_OPEN_FOLDER) {
            OpenContainingFolder(dl.outputPath);
        } else if (cmd == ID_CTX_PAUSE) {
            DownloadManager::instance().PauseByIndex(idx);
            RefreshDownloadList();
        } else if (cmd == ID_CTX_RESUME) {
            DownloadManager::instance().ResumeByIndex(idx);
            RefreshDownloadList();
        } else if (cmd == ID_CTX_CANCEL) {
            DownloadManager::instance().CancelByIndex(idx);
            RefreshDownloadList();
        } else if (cmd == ID_CTX_REMOVE) {
            DownloadManager::instance().RemoveByIndex(idx);
            RefreshDownloadList();
        }
    }

    void PlayFile(const std::string& path) {
        if (path.empty()) return;

        auto mdb = metadb::get();
        playable_location_impl loc;
        loc.set_path(path.c_str());
        loc.set_subsong(0);

        metadb_handle_ptr handle;
        mdb->handle_create(handle, loc);

        if (handle.is_valid()) {
            metadb_handle_list items;
            items += handle;

            // Add to active playlist and play
            auto pm = playlist_manager::get();
            size_t active = pm->get_active_playlist();
            if (active == ~size_t(0)) {
                active = pm->create_playlist("Default", ~size_t(0), ~size_t(0));
            }
            size_t base = pm->playlist_get_item_count(active);
            pm->playlist_insert_items(active, base, items, pfc::bit_array_false());
            pm->playlist_set_focus_item(active, base);

            auto pc = playback_control::get();
            pc->play_start(playback_control::track_command_settrack);
        }
    }

    static void OpenContainingFolder(const std::string& path) {
        if (path.empty()) return;
        // Convert to wide string and use ShellExecute to open explorer
        pfc::stringcvt::string_wide_from_utf8 wPath(path.c_str());
        std::wstring param = L"/select,\"";
        param += (const wchar_t*)wPath;
        param += L"\"";
        ShellExecuteW(NULL, L"open", L"explorer.exe", param.c_str(), NULL, SW_SHOWNORMAL);
    }

    void InitListView() {
        CListViewCtrl list(GetDlgItem(IDC_QUEUE_LIST));
        list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

        list.InsertColumn(0, L"Title", LVCFMT_LEFT, 200);
        list.InsertColumn(1, L"Status", LVCFMT_LEFT, 70);
        list.InsertColumn(2, L"Progress", LVCFMT_LEFT, 70);
        list.InsertColumn(3, L"Speed", LVCFMT_LEFT, 70);
        list.InsertColumn(4, L"Source", LVCFMT_LEFT, 70);
    }

    void RefreshDownloadList() {
        CListViewCtrl list(GetDlgItem(IDC_QUEUE_LIST));
        if (!list.IsWindow()) return;

        auto downloads = DownloadManager::instance().GetDownloads();

        // Only rebuild if count changed, otherwise update in-place
        int existingCount = list.GetItemCount();
        int newCount = (int)downloads.size();

        // Add/remove rows to match
        while (existingCount < newCount) {
            list.InsertItem(existingCount, L"");
            existingCount++;
        }
        while (existingCount > newCount) {
            existingCount--;
            list.DeleteItem(existingCount);
        }

        uint64_t totalSpeed = 0;
        int activeCount = 0;
        int completeCount = 0;
        int errorCount = 0;

        for (int i = 0; i < newCount; i++) {
            const auto& dl = downloads[i];

            pfc::stringcvt::string_wide_from_utf8 wTitle(dl.title.c_str());
            list.SetItemText(i, 0, wTitle);

            // Format status display
            std::string statusDisplay;
            if (dl.status == "active") statusDisplay = "Downloading";
            else if (dl.status == "complete") statusDisplay = "Done";
            else if (dl.status == "error") statusDisplay = "Failed";
            else if (dl.status == "queued") statusDisplay = "Queued";
            else if (dl.status == "paused") statusDisplay = "Paused";
            else statusDisplay = dl.status;

            pfc::stringcvt::string_wide_from_utf8 wStatus(statusDisplay.c_str());
            list.SetItemText(i, 1, wStatus);

            // Format progress
            char progBuf[32];
            if (dl.status == "complete") {
                snprintf(progBuf, sizeof(progBuf), "100%%");
            } else if (dl.totalSize > 0) {
                snprintf(progBuf, sizeof(progBuf), "%.1f%%", dl.progress);
            } else if (dl.status == "active") {
                snprintf(progBuf, sizeof(progBuf), "...");
            } else {
                progBuf[0] = '-'; progBuf[1] = 0;
            }
            pfc::stringcvt::string_wide_from_utf8 wProg(progBuf);
            list.SetItemText(i, 2, wProg);

            // Format speed
            char speedBuf[32];
            FormatSpeed(dl.speed, speedBuf, sizeof(speedBuf));
            pfc::stringcvt::string_wide_from_utf8 wSpeed(speedBuf);
            list.SetItemText(i, 3, wSpeed);

            pfc::stringcvt::string_wide_from_utf8 wSource(dl.sourceId.c_str());
            list.SetItemText(i, 4, wSource);

            if (dl.status == "active") {
                totalSpeed += dl.speed;
                activeCount++;
            } else if (dl.status == "complete") {
                completeCount++;
            } else if (dl.status == "error") {
                errorCount++;
            }
        }

        // Build status bar text
        pfc::string_formatter statusMsg;
        if (activeCount > 0) {
            char speedStr[32];
            FormatSpeed(totalSpeed, speedStr, sizeof(speedStr));
            statusMsg << activeCount << " downloading @ " << speedStr;
            if (completeCount > 0) statusMsg << " | " << completeCount << " done";
        } else if (completeCount > 0 || errorCount > 0) {
            if (completeCount > 0) statusMsg << completeCount << " done";
            if (errorCount > 0) {
                if (completeCount > 0) statusMsg << " | ";
                statusMsg << errorCount << " failed";
            }
        } else if (newCount > 0) {
            statusMsg << newCount << " in queue";
        } else {
            statusMsg << "Ready";
        }
        uSetDlgItemText(*this, IDC_STATUS_BAR, statusMsg);
    }

    static void FormatSpeed(uint64_t bytesPerSec, char* buf, size_t bufSize) {
        if (bytesPerSec == 0) {
            snprintf(buf, bufSize, "-");
        } else if (bytesPerSec < 1024) {
            snprintf(buf, bufSize, "%llu B/s", (unsigned long long)bytesPerSec);
        } else if (bytesPerSec < 1024 * 1024) {
            snprintf(buf, bufSize, "%.1f KB/s", bytesPerSec / 1024.0);
        } else {
            snprintf(buf, bufSize, "%.1f MB/s", bytesPerSec / (1024.0 * 1024.0));
        }
    }

    const ui_element_instance_callback::ptr m_callback;
    std::vector<ISourceProvider*> m_enabledSources;

    DarkMode::CHooks m_dark;
};

static service_factory_single_t<ui_element_impl<CDownloaderPanel>> g_downloader_panel_factory;

} // namespace

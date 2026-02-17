#include "stdafx.h"
#include "guids.h"
#include "resource.h"
#include "download_manager.h"
#include "source_manager.h"

#include <helpers/atl-misc.h>
#include <helpers/DarkMode.h>

namespace {

// Context menu group: "Downloader" submenu
static contextmenu_group_popup_factory g_downloader_group(
    guid_downloader_contextmenu_group,
    contextmenu_groups::root,
    "Downloader",
    0);

// "Download from URL..." modal dialog
class CDownloadUrlDialog : public CDialogImpl<CDownloadUrlDialog> {
public:
    enum { IDD = IDD_DOWNLOAD_URL };

    std::string m_sourceId;
    std::string m_url;

    BEGIN_MSG_MAP_EX(CDownloadUrlDialog)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_ID_HANDLER_EX(IDOK, OnOk)
        COMMAND_ID_HANDLER_EX(IDCANCEL, OnCancel)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM) {
        m_dark.AddDialogWithControls(*this);
        CenterWindow(GetParent());

        CComboBox combo(GetDlgItem(IDC_CM_SOURCE_COMBO));
        const auto& sources = SourceManager::instance().GetAll();
        for (const auto& src : sources) {
            combo.AddString(pfc::stringcvt::string_wide_from_utf8(src->GetName()));
        }
        if (!sources.empty()) {
            combo.SetCurSel(0);
        }

        // Try clipboard for URL
        if (OpenClipboard()) {
            HANDLE hData = GetClipboardData(CF_UNICODETEXT);
            if (hData) {
                const wchar_t* clipText = static_cast<const wchar_t*>(GlobalLock(hData));
                if (clipText) {
                    pfc::stringcvt::string_utf8_from_wide utf8clip(clipText);
                    pfc::string8 clip(utf8clip);
                    if (clip.length() > 0 && (
                        pfc::string_find_first(clip, "http") == 0 ||
                        pfc::string_find_first(clip, "ftp") == 0 ||
                        pfc::string_find_first(clip, "magnet:") == 0)) {
                        uSetDlgItemText(*this, IDC_CM_URL_INPUT, clip);
                    }
                    GlobalUnlock(hData);
                }
            }
            CloseClipboard();
        }

        ::SetFocus(GetDlgItem(IDC_CM_URL_INPUT));
        return FALSE;
    }

    void OnOk(UINT, int, CWindow) {
        CComboBox combo(GetDlgItem(IDC_CM_SOURCE_COMBO));
        int selIdx = combo.GetCurSel();
        if (selIdx >= 0) {
            const auto& sources = SourceManager::instance().GetAll();
            if (selIdx < (int)sources.size()) {
                m_sourceId = sources[selIdx]->GetId();
            }
        }

        pfc::string8 url;
        uGetDlgItemText(*this, IDC_CM_URL_INPUT, url);
        m_url = url.get_ptr();

        EndDialog(IDOK);
    }

    void OnCancel(UINT, int, CWindow) {
        EndDialog(IDCANCEL);
    }

private:
    fb2k::CDarkModeHooks m_dark;
};

// Context menu item
class DownloaderContextMenuItem : public contextmenu_item_simple {
public:
    unsigned get_num_items() override { return 1; }

    GUID get_parent() override { return guid_downloader_contextmenu_group; }

    void get_item_name(unsigned p_index, pfc::string_base& p_out) override {
        if (p_index == 0) p_out = "Download from URL...";
    }

    GUID get_item_guid(unsigned p_index) override {
        if (p_index == 0) return guid_downloader_contextmenu;
        return pfc::guid_null;
    }

    bool get_item_description(unsigned p_index, pfc::string_base& p_out) override {
        if (p_index == 0) {
            p_out = "Download an audio file from a URL and add to Downloaded playlist";
            return true;
        }
        return false;
    }

    void context_command(unsigned p_index, metadb_handle_list_cref p_data, const GUID& p_caller) override {
        if (p_index != 0) return;

        CDownloadUrlDialog dlg;
        if (dlg.DoModal(core_api::get_main_window()) == IDOK) {
            if (!dlg.m_url.empty() && !dlg.m_sourceId.empty()) {
                DownloadManager::instance().StartDownload(dlg.m_sourceId, dlg.m_url);
            }
        }
    }
};

static contextmenu_item_factory_t<DownloaderContextMenuItem> g_contextmenu_factory;

} // namespace

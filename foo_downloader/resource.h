#pragma once

// ============================================================================
// Dialog control IDs for foo_downloader
// ============================================================================

// Preferences dialog (IDD_PREFERENCES)
#define IDD_PREFERENCES             1000
#define IDC_OUTPUT_FOLDER           1001
#define IDC_BROWSE_FOLDER           1002
#define IDC_ARIA2_PORT              1003
#define IDC_ARIA2_PATH              1004
#define IDC_BROWSE_ARIA2            1005
#define IDC_MAX_CONCURRENT          1006
#define IDC_YTDLP_PATH              1007
#define IDC_BROWSE_YTDLP            1008
#define IDC_YT_DEFAULT_QUALITY      1009
#define IDC_PLAYLIST_NAME           1010
#define IDC_AUTO_ADD_PLAYLIST       1011
#define IDC_EMBED_METADATA          1012
#define IDC_OPEN_FOLDER_BTN         1013

// UI Panel dialog (IDD_DOWNLOADER_PANEL)
#define IDD_DOWNLOADER_PANEL        2000
#define IDC_SOURCE_COMBO            2001
#define IDC_URL_INPUT               2002
#define IDC_DOWNLOAD_BTN            2003
#define IDC_QUEUE_LIST              2004
#define IDC_STATUS_BAR              2005
#define IDC_CLEAR_COMPLETED_BTN     2006

// Context menu download dialog (IDD_DOWNLOAD_URL)
#define IDD_DOWNLOAD_URL            3000
#define IDC_CM_SOURCE_COMBO         3001
#define IDC_CM_URL_INPUT            3002

// Search results selection dialog
#define IDD_SEARCH_RESULTS          4000
#define IDC_RESULTS_LIST            4001
#define IDC_RESULTS_INFO            4002

// YouTube search results dialog (with quality selector)
#define IDD_YT_SEARCH_RESULTS       5000
#define IDC_YT_RESULTS_LIST         5001
#define IDC_YT_RESULTS_INFO         5002
#define IDC_YT_QUALITY_COMBO        5003
#define IDC_YT_QUALITY_LABEL        5004

// Retry count (IDD_PREFERENCES)
#define IDC_RETRY_COUNT             1018

// Provider enable/disable checkboxes (IDD_PREFERENCES)
#define IDC_ENABLE_CUSTOM_SOURCE    1014
#define IDC_ENABLE_YOUTUBE          1015
#define IDC_ENABLE_DIRECT_URL       1016

// YouTube extra flags
#define IDC_YTDLP_EXTRA_FLAGS       1017

// Custom source (IDD_PREF_CUSTOM_SOURCE)
#define IDC_CUSTOM_SOURCE_URL       1019
#define IDC_TEST_CUSTOM_SOURCE      1020

// Sub-preference pages
#define IDD_PREF_YOUTUBE            6000
#define IDD_PREF_ARIA2              6001
#define IDD_PREF_CUSTOM_SOURCE      6002

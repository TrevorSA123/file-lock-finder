#pragma once

// Menu command IDs, sent to MainWindow via WM_COMMAND.
enum MenuCommandId : int
{
    IDM_FILE_OPEN_FILE = 1001,
    IDM_FILE_OPEN_FOLDER,
    IDM_FILE_REFRESH,
    IDM_FILE_EXIT,

    IDM_ACTIONS_OPEN_LOCATION,
    IDM_ACTIONS_COPY_RESULTS,
    IDM_ACTIONS_RUN_AS_ADMIN,
    IDM_ACTIONS_ADVANCED_SCAN,
    IDM_ACTIONS_SCHEDULE_DELETE,
    IDM_ACTIONS_SCHEDULE_RENAME,

    IDM_OPTIONS_PREFERENCES,

    IDM_HELP_ABOUT,
};

// Child control IDs within MainWindow.
enum MainControlId : int
{
    IDC_DROP_PANEL = 2001,
    IDC_PATH_LABEL,
    IDC_STATUS_LABEL,
    IDC_ADVICE_LABEL,
    IDC_BTN_BROWSE_FILE,
    IDC_BTN_BROWSE_FOLDER,
    IDC_BTN_REFRESH,
    IDC_BTN_ADVANCED_SCAN,
    IDC_BTN_RUN_AS_ADMIN,
    IDC_RESULTS_LISTVIEW,
    IDC_STATUS_BAR,
};

// Control IDs within PreferencesDialog.
enum PreferencesControlId : int
{
    IDC_PREF_CONTEXT_MENU = 3001,
    IDC_PREF_CONTEXT_MENU_NOTE,
    IDC_PREF_INCLUDE_FOLDERS,
    IDC_PREF_AUTO_SCAN,
    IDC_PREF_ADVANCED_SCAN_DEFAULT,
    IDC_PREF_CONFIRM_REBOOT,
    IDC_PREF_CHECK_AT_STARTUP,
    IDC_PREF_COMPACT_POPUP,
    IDC_PREF_OK,
    IDC_PREF_CANCEL,
    IDC_PREF_APPLY,
};

// Control IDs within AboutDialog.
enum AboutControlId : int
{
    IDC_ABOUT_OK = 4001,
};

// Control IDs within QuickScanDialog (the compact context-menu popup).
enum QuickScanControlId : int
{
    IDC_QS_PATH_LABEL = 5001,
    IDC_QS_STATUS_LABEL,
    IDC_QS_LISTVIEW,
    IDC_QS_BTN_REFRESH,
    IDC_QS_BTN_COPY,
    IDC_QS_BTN_OPEN_FULL,
    IDC_QS_BTN_CLOSE,
};

// A private "scan complete" message posted from the background scan thread
// back to MainWindow/QuickScanDialog. wParam = scan generation, lParam =
// LockResult* (owned by the message, deleted by the handler).
#define WM_APP_SCAN_COMPLETE (WM_APP + 1)

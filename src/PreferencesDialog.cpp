#include "PreferencesDialog.h"
#include "UiHelpers.h"
#include "Resource.h"
#include "../Core/SettingsService.h"
#include "../WindowsApi/ContextMenuIntegrationService.h"

namespace
{
    const wchar_t* kClassName = L"FileLockFinder_PreferencesDialog";

    struct DialogState
    {
        AppSettings* outSettings = nullptr;
        bool* appliedFlag = nullptr; // Points at a local in PreferencesDialog::Show that outlives this struct.
    };

    bool IsChecked(HWND hwnd, int id)
    {
        return IsDlgButtonChecked(hwnd, id) == BST_CHECKED;
    }

    void ApplyCurrentState(HWND hwnd, DialogState* state)
    {
        AppSettings settings;
        settings.addToContextMenu = IsChecked(hwnd, IDC_PREF_CONTEXT_MENU);
        settings.includeFoldersInContextMenu = IsChecked(hwnd, IDC_PREF_INCLUDE_FOLDERS);
        settings.autoScanOnOpen = IsChecked(hwnd, IDC_PREF_AUTO_SCAN);
        settings.enableAdvancedScanByDefault = IsChecked(hwnd, IDC_PREF_ADVANCED_SCAN_DEFAULT);
        settings.confirmBeforeRebootActions = IsChecked(hwnd, IDC_PREF_CONFIRM_REBOOT);
        settings.checkContextMenuIntegrationAtStartup = IsChecked(hwnd, IDC_PREF_CHECK_AT_STARTUP);
        settings.useCompactPopupFromContextMenu = IsChecked(hwnd, IDC_PREF_COMPACT_POPUP);

        std::wstring error;
        if (!SettingsService::Save(settings, error))
        {
            MessageBoxW(hwnd, (L"Preferences could not be saved:\r\n" + error).c_str(),
                L"File Lock Finder", MB_OK | MB_ICONWARNING);
        }

        // Registry access can fail (locked-down machine, permissions, etc.);
        // never let that crash the app - show a friendly message instead.
        std::wstring contextMenuError;
        if (!ContextMenuIntegrationService::ApplyPreferences(settings, contextMenuError))
        {
            MessageBoxW(hwnd, (L"Context-menu registration could not be updated:\r\n" + contextMenuError).c_str(),
                L"File Lock Finder", MB_OK | MB_ICONWARNING);
        }

        if (state->outSettings)
            *state->outSettings = settings;
        if (state->appliedFlag)
            *state->appliedFlag = true;
    }

    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
            case WM_COMMAND:
            {
                auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
                int id = LOWORD(wParam);
                if (id == IDC_PREF_OK)
                {
                    ApplyCurrentState(hwnd, state);
                    DestroyWindow(hwnd);
                    return 0;
                }
                if (id == IDC_PREF_APPLY)
                {
                    ApplyCurrentState(hwnd, state);
                    return 0;
                }
                if (id == IDC_PREF_CANCEL || id == IDCANCEL)
                {
                    DestroyWindow(hwnd);
                    return 0;
                }
                break;
            }

            case WM_CLOSE:
                DestroyWindow(hwnd);
                return 0;

            case WM_NCDESTROY:
            {
                auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
                delete state;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                break;
            }
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    void RegisterClassOnce(HINSTANCE hInstance)
    {
        static bool registered = false;
        if (registered) return;

        WNDCLASSEXW wc{ sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = kClassName;
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        RegisterClassExW(&wc);
        registered = true;
    }
}

bool PreferencesDialog::Show(HWND owner, AppSettings& settings)
{
    HINSTANCE hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE));
    RegisterClassOnce(hInstance);

    auto S = [](int v) { return UiHelpers::Scale(v); };

    const int width = S(460), height = S(460);
    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME, kClassName, L"Preferences",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height,
        owner, nullptr, hInstance, nullptr);
    if (!hwnd)
        return false;

    bool applied = false;
    auto* state = new DialogState();
    state->outSettings = &settings;
    state->appliedFlag = &applied;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

    RECT client{};
    GetClientRect(hwnd, &client);
    int contentWidth = client.right - client.left;
    int x = S(20), w = contentWidth - S(40);
    int y = S(16);

    UiHelpers::CreateCheckbox(hwnd, hInstance, L"Add \"Find locking processes\" to Explorer context menu",
        x, y, w, S(20), IDC_PREF_CONTEXT_MENU);
    y += S(22);

    UiHelpers::CreateLabel(hwnd, hInstance, L"On Windows 11 this may appear under \"Show more options\".",
        x + S(20), y, w - S(20), S(32), IDC_PREF_CONTEXT_MENU_NOTE);
    y += S(36);

    UiHelpers::CreateCheckbox(hwnd, hInstance, L"Include folders as well as files",
        x, y, w, S(20), IDC_PREF_INCLUDE_FOLDERS);
    y += S(30);

    UiHelpers::CreateCheckbox(hwnd, hInstance, L"Run scans automatically when a file is selected",
        x, y, w, S(20), IDC_PREF_AUTO_SCAN);
    y += S(30);

    UiHelpers::CreateCheckbox(hwnd, hInstance, L"Enable Advanced Handle Scan by default",
        x, y, w, S(20), IDC_PREF_ADVANCED_SCAN_DEFAULT);
    y += S(30);

    UiHelpers::CreateCheckbox(hwnd, hInstance, L"Show confirmation before scheduling delete/rename on reboot",
        x, y, w, S(20), IDC_PREF_CONFIRM_REBOOT);
    y += S(30);

    UiHelpers::CreateCheckbox(hwnd, hInstance, L"Check context-menu integration at startup",
        x, y, w, S(20), IDC_PREF_CHECK_AT_STARTUP);
    y += S(30);

    UiHelpers::CreateCheckbox(hwnd, hInstance, L"Show a compact popup with just the locking processes when launched from the context menu",
        x, y, w, S(34), IDC_PREF_COMPACT_POPUP);
    y += S(40);

    CheckDlgButton(hwnd, IDC_PREF_CONTEXT_MENU, settings.addToContextMenu ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_PREF_INCLUDE_FOLDERS, settings.includeFoldersInContextMenu ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_PREF_AUTO_SCAN, settings.autoScanOnOpen ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_PREF_ADVANCED_SCAN_DEFAULT, settings.enableAdvancedScanByDefault ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_PREF_CONFIRM_REBOOT, settings.confirmBeforeRebootActions ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_PREF_CHECK_AT_STARTUP, settings.checkContextMenuIntegrationAtStartup ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_PREF_COMPACT_POPUP, settings.useCompactPopupFromContextMenu ? BST_CHECKED : BST_UNCHECKED);

    const int buttonWidth = S(90), buttonHeight = S(28);
    int buttonY = height - S(70);
    UiHelpers::CreateButtonControl(hwnd, hInstance, L"OK", contentWidth - S(20) - (buttonWidth * 3 + S(20)), buttonY, buttonWidth, buttonHeight, IDC_PREF_OK);
    UiHelpers::CreateButtonControl(hwnd, hInstance, L"Cancel", contentWidth - S(20) - (buttonWidth * 2 + S(10)), buttonY, buttonWidth, buttonHeight, IDC_PREF_CANCEL);
    UiHelpers::CreateButtonControl(hwnd, hInstance, L"Apply", contentWidth - S(20) - buttonWidth, buttonY, buttonWidth, buttonHeight, IDC_PREF_APPLY);

    UiHelpers::CenterOverOwner(hwnd, owner);
    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0))
    {
        if (!IsDialogMessageW(hwnd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!IsWindow(hwnd))
            break;
    }

    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);

    return applied;
}

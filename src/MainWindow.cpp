#include "MainWindow.h"
#include "Resource.h"
#include "UiHelpers.h"
#include "Dialogs.h"
#include "AboutDialog.h"
#include "PreferencesDialog.h"
#include "ResultsListView.h"
#include "ResultsFormatting.h"

#include "../Core/FileLockAnalyzer.h"
#include "../Core/PathNormalizer.h"
#include "../Core/SettingsService.h"
#include "../Core/LockStatus.h"
#include "../WindowsApi/ContextMenuIntegrationService.h"
#include "../WindowsApi/MoveFileExApi.h"

#include <commctrl.h>
#include <shellapi.h>
#include <thread>
#include <iterator>

#pragma comment(lib, "Comctl32.lib")

namespace
{
    const wchar_t* kDropPanelClassName = L"FileLockFinder_DropPanel";

    bool IsProcessElevated()
    {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
            return false;

        TOKEN_ELEVATION elevation{};
        DWORD size = sizeof(elevation);
        bool elevated = false;
        if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size))
            elevated = elevation.TokenIsElevated != 0;

        CloseHandle(token);
        return elevated;
    }

    LRESULT CALLBACK DropPanelWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        MainWindow* owner = nullptr;
        if (msg == WM_NCCREATE)
        {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            owner = reinterpret_cast<MainWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(owner));
        }
        else
        {
            owner = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        switch (msg)
        {
            case WM_ERASEBKGND:
                return 1; // We paint the whole client area in WM_PAINT.

            case WM_PAINT:
            {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                RECT rc;
                GetClientRect(hwnd, &rc);
                FillRect(hdc, &rc, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));

                int borderInset = UiHelpers::Scale(8);
                HPEN pen = CreatePen(PS_DASH, UiHelpers::Scale(2), RGB(120, 120, 120));
                HGDIOBJ oldPen = SelectObject(hdc, pen);
                HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Rectangle(hdc, rc.left + borderInset, rc.top + borderInset, rc.right - borderInset, rc.bottom - borderInset);
                SelectObject(hdc, oldBrush);
                SelectObject(hdc, oldPen);
                DeleteObject(pen);

                const wchar_t* text = L"Drag and drop a file or folder here\n\nor use File > Open File... / Open Folder...";
                RECT textRect = rc;
                InflateRect(&textRect, -UiHelpers::Scale(30), -UiHelpers::Scale(30));
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(90, 90, 90));
                HGDIOBJ oldFont = SelectObject(hdc, UiHelpers::GetUiFont());
                DrawTextW(hdc, text, -1, &textRect, DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX);
                SelectObject(hdc, oldFont);

                EndPaint(hwnd, &ps);
                return 0;
            }

            case WM_DROPFILES:
                if (owner)
                    owner->OnDropFiles(reinterpret_cast<HDROP>(wParam));
                return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    void RegisterDropPanelClassOnce(HINSTANCE hInstance)
    {
        static bool registered = false;
        if (registered) return;

        WNDCLASSEXW wc{ sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = DropPanelWndProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr; // Painted manually.
        wc.lpszClassName = kDropPanelClassName;
        RegisterClassExW(&wc);
        registered = true;
    }
}

ATOM MainWindow::RegisterWindowClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wc{ sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"FileLockFinder_MainWindow";
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    return RegisterClassExW(&wc);
}

MainWindow* MainWindow::Create(HINSTANCE hInstance, int nCmdShow, const std::wstring& initialPath)
{
    RegisterWindowClass(hInstance);

    auto* window = new MainWindow();
    HWND hwnd = CreateWindowExW(
        WS_EX_ACCEPTFILES, L"FileLockFinder_MainWindow", L"File Lock Finder",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, UiHelpers::Scale(940), UiHelpers::Scale(640),
        nullptr, nullptr, hInstance, window);

    if (!hwnd)
    {
        delete window;
        return nullptr;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    if (!initialPath.empty())
        window->SetSelectedPath(initialPath, true);

    return window;
}

LRESULT CALLBACK MainWindow::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    MainWindow* self;
    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<MainWindow*>(cs->lpCreateParams);
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self)
        return self->HandleMessage(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MainWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_CREATE:
        {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            OnCreate(m_hwnd, cs->hInstance);
            return 0;
        }
        case WM_SIZE:
            OnSize(LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_COMMAND:
            OnCommand(LOWORD(wParam));
            return 0;
        case WM_DROPFILES:
            OnDropFiles(reinterpret_cast<HDROP>(wParam));
            return 0;
        case WM_APP_SCAN_COMPLETE:
            OnScanComplete(static_cast<int>(wParam), reinterpret_cast<LockResult*>(lParam));
            return 0;
        case WM_GETMINMAXINFO:
        {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            mmi->ptMinTrackSize.x = UiHelpers::Scale(640);
            mmi->ptMinTrackSize.y = UiHelpers::Scale(480);
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(m_hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

void MainWindow::OnCreate(HWND hwnd, HINSTANCE hInstance)
{
    m_hwnd = hwnd;
    m_settings = SettingsService::Load();
    m_isElevated = IsProcessElevated();

    BuildMenu();
    CreateControls(hInstance);
    UpdateElevationUi();

    RECT rc;
    GetClientRect(hwnd, &rc);
    LayoutControls(rc.right - rc.left, rc.bottom - rc.top);

    SetWindowTextW(m_pathLabel, L"Selected: (none)");
    SetWindowTextW(m_statusLabel, ToStatusMessage(LockStatus::NoPathSelected, false));

    CheckContextMenuIntegrationAtStartup();
}

void MainWindow::BuildMenu()
{
    m_menu = CreateMenu();

    HMENU fileMenu = CreatePopupMenu();
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_OPEN_FILE, L"Open File...");
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_OPEN_FOLDER, L"Open Folder...");
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_REFRESH, L"Refresh");
    AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_EXIT, L"Exit");
    AppendMenuW(m_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(fileMenu), L"File");

    HMENU actionsMenu = CreatePopupMenu();
    AppendMenuW(actionsMenu, MF_STRING, IDM_ACTIONS_OPEN_LOCATION, L"Open File Location");
    AppendMenuW(actionsMenu, MF_STRING, IDM_ACTIONS_COPY_RESULTS, L"Copy Results");
    AppendMenuW(actionsMenu, MF_STRING, IDM_ACTIONS_RUN_AS_ADMIN, L"Run as Administrator");
    AppendMenuW(actionsMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(actionsMenu, MF_STRING, IDM_ACTIONS_ADVANCED_SCAN, L"Advanced Scan");
    AppendMenuW(actionsMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(actionsMenu, MF_STRING, IDM_ACTIONS_SCHEDULE_DELETE, L"Schedule Delete on Reboot...");
    AppendMenuW(actionsMenu, MF_STRING, IDM_ACTIONS_SCHEDULE_RENAME, L"Schedule Rename on Reboot...");
    AppendMenuW(m_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(actionsMenu), L"Actions");

    HMENU optionsMenu = CreatePopupMenu();
    AppendMenuW(optionsMenu, MF_STRING, IDM_OPTIONS_PREFERENCES, L"Preferences...");
    AppendMenuW(m_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(optionsMenu), L"Options");

    HMENU helpMenu = CreatePopupMenu();
    AppendMenuW(helpMenu, MF_STRING, IDM_HELP_ABOUT, L"About");
    AppendMenuW(m_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(helpMenu), L"Help");

    SetMenu(m_hwnd, m_menu);
}

void MainWindow::CreateControls(HINSTANCE hInstance)
{
    m_pathLabel = UiHelpers::CreateLabel(m_hwnd, hInstance, L"Selected: (none)", 0, 0, 0, 0, IDC_PATH_LABEL);
    m_statusLabel = UiHelpers::CreateLabel(m_hwnd, hInstance, L"", 0, 0, 0, 0, IDC_STATUS_LABEL);
    m_adviceLabel = UiHelpers::CreateLabel(m_hwnd, hInstance, L"", 0, 0, 0, 0, IDC_ADVICE_LABEL);

    m_btnBrowseFile = UiHelpers::CreateButtonControl(m_hwnd, hInstance, L"Browse File...", 0, 0, 0, 0, IDC_BTN_BROWSE_FILE);
    m_btnBrowseFolder = UiHelpers::CreateButtonControl(m_hwnd, hInstance, L"Browse Folder...", 0, 0, 0, 0, IDC_BTN_BROWSE_FOLDER);
    m_btnRefresh = UiHelpers::CreateButtonControl(m_hwnd, hInstance, L"Refresh", 0, 0, 0, 0, IDC_BTN_REFRESH);
    m_btnAdvancedScan = UiHelpers::CreateButtonControl(m_hwnd, hInstance, L"Advanced Scan", 0, 0, 0, 0, IDC_BTN_ADVANCED_SCAN);
    m_btnRunAsAdmin = UiHelpers::CreateButtonControl(m_hwnd, hInstance, L"Run as Administrator", 0, 0, 0, 0, IDC_BTN_RUN_AS_ADMIN);

    m_listView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | WS_TABSTOP,
        0, 0, 0, 0, m_hwnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_RESULTS_LISTVIEW)), hInstance, nullptr);
    ListView_SetExtendedListViewStyle(m_listView, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    ResultsListView::SetupColumns(m_listView);

    RegisterDropPanelClassOnce(hInstance);
    m_dropPanel = CreateWindowExW(WS_EX_CLIENTEDGE, kDropPanelClassName, L"",
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, m_hwnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_DROP_PANEL)), hInstance, this);
    DragAcceptFiles(m_dropPanel, TRUE);

    m_statusBar = CreateWindowExW(0, STATUSCLASSNAMEW, L"",
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, m_hwnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_STATUS_BAR)), hInstance, nullptr);
    UpdateStatusBar(L"Ready.");
}

void MainWindow::OnSize(int width, int height)
{
    if (width <= 0 || height <= 0)
        return;
    LayoutControls(width, height);
}

void MainWindow::LayoutControls(int width, int height)
{
    if (!m_statusBar)
        return;

    SendMessageW(m_statusBar, WM_SIZE, 0, 0);
    RECT statusRect{};
    GetWindowRect(m_statusBar, &statusRect);
    int statusHeight = statusRect.bottom - statusRect.top;

    auto S = [](int v) { return UiHelpers::Scale(v); };

    const int margin = S(10);
    int y = margin;
    int contentWidth = width - 2 * margin;

    MoveWindow(m_pathLabel, margin, y, contentWidth, S(20), TRUE);
    y += S(24);
    MoveWindow(m_statusLabel, margin, y, contentWidth, S(20), TRUE);
    y += S(24);
    MoveWindow(m_adviceLabel, margin, y, contentWidth, S(40), TRUE);
    y += S(46);

    const int buttonHeight = S(28), gap = S(8);
    int bx = margin;
    MoveWindow(m_btnBrowseFile, bx, y, S(120), buttonHeight, TRUE); bx += S(120) + gap;
    MoveWindow(m_btnBrowseFolder, bx, y, S(120), buttonHeight, TRUE); bx += S(120) + gap;
    MoveWindow(m_btnRefresh, bx, y, S(90), buttonHeight, TRUE); bx += S(90) + gap;
    MoveWindow(m_btnAdvancedScan, bx, y, S(120), buttonHeight, TRUE); bx += S(120) + gap;
    MoveWindow(m_btnRunAsAdmin, bx, y, S(170), buttonHeight, TRUE);
    y += buttonHeight + margin;

    int bodyHeight = height - statusHeight;
    int remainingHeight = bodyHeight - y - margin;
    if (remainingHeight < 0)
        remainingHeight = 0;

    bool showDropPanel = m_currentPath.empty();
    ShowWindow(m_dropPanel, showDropPanel ? SW_SHOW : SW_HIDE);
    ShowWindow(m_listView, showDropPanel ? SW_HIDE : SW_SHOW);

    if (showDropPanel)
        MoveWindow(m_dropPanel, margin, y, contentWidth, remainingHeight, TRUE);
    else
        MoveWindow(m_listView, margin, y, contentWidth, remainingHeight, TRUE);
}

void MainWindow::UpdateElevationUi()
{
    ShowWindow(m_btnRunAsAdmin, m_isElevated ? SW_HIDE : SW_SHOW);
    EnableWindow(m_btnRunAsAdmin, !m_isElevated);
    EnableMenuItem(m_menu, IDM_ACTIONS_RUN_AS_ADMIN, MF_BYCOMMAND | (m_isElevated ? MF_GRAYED : MF_ENABLED));
}

void MainWindow::UpdateStatusBar(const std::wstring& text)
{
    SendMessageW(m_statusBar, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(text.c_str()));
}

void MainWindow::SetControlsEnabled(bool enabled)
{
    EnableWindow(m_btnBrowseFile, enabled);
    EnableWindow(m_btnBrowseFolder, enabled);
    EnableWindow(m_btnRefresh, enabled);
    EnableWindow(m_btnAdvancedScan, enabled);
    EnableWindow(m_btnRunAsAdmin, enabled && !m_isElevated);

    UINT flag = enabled ? MF_ENABLED : MF_GRAYED;
    EnableMenuItem(m_menu, IDM_FILE_OPEN_FILE, MF_BYCOMMAND | flag);
    EnableMenuItem(m_menu, IDM_FILE_OPEN_FOLDER, MF_BYCOMMAND | flag);
    EnableMenuItem(m_menu, IDM_FILE_REFRESH, MF_BYCOMMAND | flag);
    EnableMenuItem(m_menu, IDM_ACTIONS_ADVANCED_SCAN, MF_BYCOMMAND | flag);
}

void MainWindow::OnCommand(int id)
{
    switch (id)
    {
        case IDM_FILE_OPEN_FILE:
        case IDC_BTN_BROWSE_FILE:
            ActionOpenFile();
            break;
        case IDM_FILE_OPEN_FOLDER:
        case IDC_BTN_BROWSE_FOLDER:
            ActionOpenFolder();
            break;
        case IDM_FILE_REFRESH:
        case IDC_BTN_REFRESH:
            ActionRefresh();
            break;
        case IDM_FILE_EXIT:
            DestroyWindow(m_hwnd);
            break;
        case IDM_ACTIONS_OPEN_LOCATION:
            ActionOpenFileLocation();
            break;
        case IDM_ACTIONS_COPY_RESULTS:
            ActionCopyResults();
            break;
        case IDM_ACTIONS_RUN_AS_ADMIN:
        case IDC_BTN_RUN_AS_ADMIN:
            ActionRunAsAdministrator();
            break;
        case IDM_ACTIONS_ADVANCED_SCAN:
        case IDC_BTN_ADVANCED_SCAN:
            ActionAdvancedScan();
            break;
        case IDM_ACTIONS_SCHEDULE_DELETE:
            ActionScheduleDeleteOnReboot();
            break;
        case IDM_ACTIONS_SCHEDULE_RENAME:
            ActionScheduleRenameOnReboot();
            break;
        case IDM_OPTIONS_PREFERENCES:
            ActionPreferences();
            break;
        case IDM_HELP_ABOUT:
            ActionAbout();
            break;
    }
}

void MainWindow::OnDropFiles(HDROP hDrop)
{
    wchar_t buffer[32768];
    if (DragQueryFileW(hDrop, 0, buffer, static_cast<UINT>(std::size(buffer))) > 0)
        SetSelectedPath(buffer, false); // Respect AutoScanOnOpen for drag-and-drop.
    DragFinish(hDrop);
}

void MainWindow::SetSelectedPath(const std::wstring& path, bool forceScan)
{
    m_currentPath = PathNormalizer::TrimAndUnquote(path);

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    LayoutControls(rc.right - rc.left, rc.bottom - rc.top);

    SetWindowTextW(m_pathLabel, (L"Selected: " + (m_currentPath.empty() ? L"(none)" : m_currentPath)).c_str());

    if (m_currentPath.empty())
    {
        SetWindowTextW(m_statusLabel, ToStatusMessage(LockStatus::NoPathSelected, false));
        SetWindowTextW(m_adviceLabel, L"");
        ListView_DeleteAllItems(m_listView);
        return;
    }

    if (forceScan || m_settings.autoScanOnOpen)
    {
        StartScan(m_settings.enableAdvancedScanByDefault);
    }
    else
    {
        SetWindowTextW(m_statusLabel, L"Ready to scan. Click Refresh.");
        SetWindowTextW(m_adviceLabel, L"");
    }
}

void MainWindow::StartScan(bool advanced)
{
    if (m_currentPath.empty())
        return;

    if (m_activeCancelToken)
        m_activeCancelToken->store(true); // Ask any still-running scan to wind down early.

    auto cancelToken = std::make_shared<std::atomic<bool>>(false);
    m_activeCancelToken = cancelToken;

    ++m_scanGeneration;
    int generation = m_scanGeneration;

    SetControlsEnabled(false);
    SetWindowTextW(m_statusLabel, L"Scanning...");
    SetWindowTextW(m_adviceLabel, L"");
    UpdateStatusBar(L"Scanning...");

    HWND hwnd = m_hwnd;
    std::wstring path = m_currentPath;

    std::thread([hwnd, path, advanced, generation, cancelToken]() {
        auto* result = new LockResult(FileLockAnalyzer::Scan(path, advanced, cancelToken.get()));
        PostMessageW(hwnd, WM_APP_SCAN_COMPLETE, static_cast<WPARAM>(generation), reinterpret_cast<LPARAM>(result));
    }).detach();
}

void MainWindow::OnScanComplete(int generation, LockResult* result)
{
    std::unique_ptr<LockResult> owned(result);
    if (generation != m_scanGeneration)
        return; // Superseded by a newer scan - discard silently.

    SetControlsEnabled(true);
    ApplyResult(*owned);
}

void MainWindow::ApplyResult(const LockResult& result)
{
    m_lastResult = result;

    SetWindowTextW(m_statusLabel, result.statusMessage.c_str());
    SetWindowTextW(m_adviceLabel, result.advice.c_str());

    ResultsListView::Populate(m_listView, result);

    std::wstring statusBarText = L"Processes found: " + std::to_wstring(result.processes.size());
    if (!result.errors.empty())
        statusBarText += L" | Notes: " + std::to_wstring(result.errors.size());
    UpdateStatusBar(statusBarText);
}

void MainWindow::ActionOpenFile()
{
    std::wstring path;
    if (Dialogs::ShowOpenFileDialog(m_hwnd, path))
        SetSelectedPath(path, true);
}

void MainWindow::ActionOpenFolder()
{
    std::wstring path;
    if (Dialogs::ShowOpenFolderDialog(m_hwnd, path))
        SetSelectedPath(path, true);
}

void MainWindow::ActionRefresh()
{
    if (m_currentPath.empty())
    {
        MessageBoxW(m_hwnd, L"No file selected. Use File > Open File/Folder or drop a file onto the window.",
            L"File Lock Finder", MB_OK | MB_ICONINFORMATION);
        return;
    }
    StartScan(m_settings.enableAdvancedScanByDefault);
}

void MainWindow::ActionAdvancedScan()
{
    if (m_currentPath.empty())
    {
        MessageBoxW(m_hwnd, L"No file selected. Use File > Open File/Folder or drop a file onto the window.",
            L"File Lock Finder", MB_OK | MB_ICONINFORMATION);
        return;
    }
    StartScan(true);
}

void MainWindow::ActionOpenFileLocation()
{
    if (m_currentPath.empty())
    {
        MessageBoxW(m_hwnd, L"No file selected.", L"File Lock Finder", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (GetFileAttributesW(m_currentPath.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        MessageBoxW(m_hwnd, L"The selected path no longer exists.", L"File Lock Finder", MB_OK | MB_ICONWARNING);
        return;
    }

    if (m_lastResult.isDirectory)
    {
        ShellExecuteW(m_hwnd, L"open", m_currentPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    else
    {
        std::wstring params = L"/select,\"" + m_currentPath + L"\"";
        ShellExecuteW(m_hwnd, L"open", L"explorer.exe", params.c_str(), nullptr, SW_SHOWNORMAL);
    }
}

void MainWindow::ActionCopyResults()
{
    UiHelpers::CopyTextToClipboard(m_hwnd, ResultsFormatting::ToClipboardText(m_lastResult));
    UpdateStatusBar(L"Results copied to clipboard.");
}

void MainWindow::ActionRunAsAdministrator()
{
    std::wstring exePath = ContextMenuIntegrationService::GetCurrentExecutablePath();
    std::wstring params = m_currentPath.empty() ? L"" : (L"\"" + m_currentPath + L"\"");

    SHELLEXECUTEINFOW sei{ sizeof(sei) };
    sei.fMask = SEE_MASK_FLAG_NO_UI;
    sei.hwnd = m_hwnd;
    sei.lpVerb = L"runas";
    sei.lpFile = exePath.c_str();
    sei.lpParameters = params.c_str();
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei))
    {
        DWORD err = GetLastError();
        if (err != ERROR_CANCELLED)
        {
            MessageBoxW(m_hwnd,
                L"Could not restart with administrator rights. Some system/protected processes require elevation to inspect.",
                L"File Lock Finder", MB_OK | MB_ICONWARNING);
        }
    }
}

void MainWindow::ActionPreferences()
{
    bool applied = PreferencesDialog::Show(m_hwnd, m_settings);
    if (applied)
    {
        UpdateStatusBar(L"Preferences saved.");
        CheckContextMenuIntegrationAtStartup();
    }
}

void MainWindow::ActionScheduleDeleteOnReboot()
{
    if (m_currentPath.empty())
    {
        MessageBoxW(m_hwnd, L"No file selected.", L"File Lock Finder", MB_OK | MB_ICONINFORMATION);
        return;
    }

    std::wstring message = L"This will schedule DELETION of:\r\n\r\n" + m_currentPath +
        L"\r\n\r\nThe file or folder will be removed the next time Windows restarts. "
        L"This tool never deletes anything immediately or forcibly closes other processes' handles.\r\n\r\nContinue?";
    if (MessageBoxW(m_hwnd, message.c_str(), L"Confirm Delete on Reboot", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        return;

    if (m_settings.confirmBeforeRebootActions)
    {
        if (MessageBoxW(m_hwnd, L"Are you absolutely sure? This will take effect at next restart.",
                L"Final Confirmation", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
            return;
    }

    std::wstring error;
    if (MoveFileExApi::ScheduleDeleteOnReboot(m_currentPath, error))
        MessageBoxW(m_hwnd, L"Deletion has been scheduled for the next restart.", L"File Lock Finder", MB_OK | MB_ICONINFORMATION);
    else
        MessageBoxW(m_hwnd, (L"Could not schedule deletion:\r\n" + error).c_str(), L"File Lock Finder", MB_OK | MB_ICONERROR);
}

void MainWindow::ActionScheduleRenameOnReboot()
{
    if (m_currentPath.empty())
    {
        MessageBoxW(m_hwnd, L"No file selected.", L"File Lock Finder", MB_OK | MB_ICONINFORMATION);
        return;
    }

    std::wstring target;
    if (!UiHelpers::PromptForText(m_hwnd, L"Schedule Rename on Reboot", L"New path/name:", m_currentPath, target))
        return;

    target = PathNormalizer::TrimAndUnquote(target);
    if (target.empty())
        return;

    std::wstring message = L"This will schedule a RENAME:\r\n\r\nFrom: " + m_currentPath + L"\r\nTo: " + target +
        L"\r\n\r\nThis will happen the next time Windows restarts.\r\n\r\nContinue?";
    if (MessageBoxW(m_hwnd, message.c_str(), L"Confirm Rename on Reboot", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        return;

    if (m_settings.confirmBeforeRebootActions)
    {
        if (MessageBoxW(m_hwnd, L"Are you absolutely sure? This will take effect at next restart.",
                L"Final Confirmation", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
            return;
    }

    std::wstring error;
    if (MoveFileExApi::ScheduleRenameOnReboot(m_currentPath, target, error))
        MessageBoxW(m_hwnd, L"Rename has been scheduled for the next restart.", L"File Lock Finder", MB_OK | MB_ICONINFORMATION);
    else
        MessageBoxW(m_hwnd, (L"Could not schedule rename:\r\n" + error).c_str(), L"File Lock Finder", MB_OK | MB_ICONERROR);
}

void MainWindow::ActionAbout()
{
    AboutDialog::Show(m_hwnd);
}

void MainWindow::CheckContextMenuIntegrationAtStartup()
{
    if (!m_settings.checkContextMenuIntegrationAtStartup || !m_settings.addToContextMenu)
        return;

    if (!ContextMenuIntegrationService::IsContextMenuRegisteredForCurrentExecutable())
        UpdateStatusBar(L"Context-menu integration needs repair. Open Preferences to repair it.");
}

#include "QuickScanDialog.h"
#include "Resource.h"
#include "UiHelpers.h"
#include "ResultsListView.h"
#include "ResultsFormatting.h"

#include "../Core/FileLockAnalyzer.h"
#include "../Core/SettingsService.h"
#include "../Core/LockStatus.h"

#include <commctrl.h>
#include <thread>
#include <memory>
#include <atomic>

namespace
{
    const wchar_t* kClassName = L"FileLockFinder_QuickScanDialog";

    // Deliberately mirrors MainWindow's async-scan pattern (background
    // thread + generation counter + PostMessage back to the UI thread) but
    // trimmed down to only what the compact popup needs.
    class QuickScanWindow
    {
    public:
        static QuickScanDialog::ExitAction RunModal(HINSTANCE hInstance, int nCmdShow, const std::wstring& path);
        static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    private:
        LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

        void OnCreate(HWND hwnd, HINSTANCE hInstance);
        void OnSize(int width, int height);
        void OnCommand(int id);
        void StartScan();
        void OnScanComplete(int generation, LockResult* result);
        void ApplyResult(const LockResult& result);

        HWND m_hwnd = nullptr;
        HWND m_pathLabel = nullptr;
        HWND m_statusLabel = nullptr;
        HWND m_listView = nullptr;
        HWND m_btnRefresh = nullptr;
        HWND m_btnCopy = nullptr;
        HWND m_btnOpenFull = nullptr;
        HWND m_btnClose = nullptr;

        std::wstring m_path;
        AppSettings m_settings;
        LockResult m_lastResult;
        int m_scanGeneration = 0;
        std::shared_ptr<std::atomic<bool>> m_activeCancelToken;
        QuickScanDialog::ExitAction m_exitAction = QuickScanDialog::ExitAction::Closed;
    };

    LRESULT CALLBACK QuickScanWindow::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        QuickScanWindow* self;
        if (msg == WM_NCCREATE)
        {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = reinterpret_cast<QuickScanWindow*>(cs->lpCreateParams);
            self->m_hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        else
        {
            self = reinterpret_cast<QuickScanWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (self)
            return self->HandleMessage(msg, wParam, lParam);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    LRESULT QuickScanWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
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
            case WM_APP_SCAN_COMPLETE:
                OnScanComplete(static_cast<int>(wParam), reinterpret_cast<LockResult*>(lParam));
                return 0;
            case WM_GETMINMAXINFO:
            {
                auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
                mmi->ptMinTrackSize.x = UiHelpers::Scale(480);
                mmi->ptMinTrackSize.y = UiHelpers::Scale(320);
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

    void QuickScanWindow::OnCreate(HWND hwnd, HINSTANCE hInstance)
    {
        m_hwnd = hwnd;

        m_pathLabel = UiHelpers::CreateLabel(m_hwnd, hInstance, (L"Selected: " + m_path).c_str(), 0, 0, 0, 0, IDC_QS_PATH_LABEL);
        m_statusLabel = UiHelpers::CreateLabel(m_hwnd, hInstance, L"Scanning...", 0, 0, 0, 0, IDC_QS_STATUS_LABEL);

        m_listView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | WS_TABSTOP,
            0, 0, 0, 0, m_hwnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_QS_LISTVIEW)), hInstance, nullptr);
        ListView_SetExtendedListViewStyle(m_listView, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        ResultsListView::SetupColumns(m_listView);

        m_btnRefresh = UiHelpers::CreateButtonControl(m_hwnd, hInstance, L"Refresh", 0, 0, 0, 0, IDC_QS_BTN_REFRESH);
        m_btnCopy = UiHelpers::CreateButtonControl(m_hwnd, hInstance, L"Copy Results", 0, 0, 0, 0, IDC_QS_BTN_COPY);
        m_btnOpenFull = UiHelpers::CreateButtonControl(m_hwnd, hInstance, L"Open Full Window", 0, 0, 0, 0, IDC_QS_BTN_OPEN_FULL);
        m_btnClose = UiHelpers::CreateButtonControl(m_hwnd, hInstance, L"Close", 0, 0, 0, 0, IDC_QS_BTN_CLOSE);

        RECT rc;
        GetClientRect(hwnd, &rc);
        OnSize(rc.right - rc.left, rc.bottom - rc.top);

        // The whole point of this popup is "scan and show" - always scan
        // immediately regardless of AutoScanOnOpen (which governs the main
        // window's drag/drop behaviour instead).
        StartScan();
    }

    void QuickScanWindow::OnSize(int width, int height)
    {
        if (width <= 0 || height <= 0)
            return;

        auto S = [](int v) { return UiHelpers::Scale(v); };

        const int margin = S(10);
        int contentWidth = width - 2 * margin;
        int y = margin;

        MoveWindow(m_pathLabel, margin, y, contentWidth, S(20), TRUE);
        y += S(24);
        MoveWindow(m_statusLabel, margin, y, contentWidth, S(20), TRUE);
        y += S(30);

        const int buttonHeight = S(28), buttonWidth = S(120), gap = S(8);
        int bottomY = height - margin - buttonHeight;

        MoveWindow(m_listView, margin, y, contentWidth, bottomY - y - margin, TRUE);

        int bx = margin;
        MoveWindow(m_btnRefresh, bx, bottomY, S(90), buttonHeight, TRUE); bx += S(90) + gap;
        MoveWindow(m_btnCopy, bx, bottomY, buttonWidth, buttonHeight, TRUE); bx += buttonWidth + gap;
        MoveWindow(m_btnOpenFull, bx, bottomY, S(140), buttonHeight, TRUE);
        MoveWindow(m_btnClose, width - margin - S(90), bottomY, S(90), buttonHeight, TRUE);
    }

    void QuickScanWindow::OnCommand(int id)
    {
        switch (id)
        {
            case IDC_QS_BTN_REFRESH:
                StartScan();
                break;
            case IDC_QS_BTN_COPY:
                UiHelpers::CopyTextToClipboard(m_hwnd, ResultsFormatting::ToClipboardText(m_lastResult));
                SetWindowTextW(m_statusLabel, (m_lastResult.statusMessage + L"  (results copied to clipboard)").c_str());
                break;
            case IDC_QS_BTN_OPEN_FULL:
                m_exitAction = QuickScanDialog::ExitAction::OpenFullWindow;
                DestroyWindow(m_hwnd);
                break;
            case IDC_QS_BTN_CLOSE:
                DestroyWindow(m_hwnd);
                break;
        }
    }

    void QuickScanWindow::StartScan()
    {
        if (m_path.empty())
            return;

        if (m_activeCancelToken)
            m_activeCancelToken->store(true);

        auto cancelToken = std::make_shared<std::atomic<bool>>(false);
        m_activeCancelToken = cancelToken;

        ++m_scanGeneration;
        int generation = m_scanGeneration;

        EnableWindow(m_btnRefresh, FALSE);
        SetWindowTextW(m_statusLabel, L"Scanning...");

        HWND hwnd = m_hwnd;
        std::wstring path = m_path;
        bool advanced = m_settings.enableAdvancedScanByDefault;

        std::thread([hwnd, path, advanced, generation, cancelToken]() {
            auto* result = new LockResult(FileLockAnalyzer::Scan(path, advanced, cancelToken.get()));
            PostMessageW(hwnd, WM_APP_SCAN_COMPLETE, static_cast<WPARAM>(generation), reinterpret_cast<LPARAM>(result));
        }).detach();
    }

    void QuickScanWindow::OnScanComplete(int generation, LockResult* result)
    {
        std::unique_ptr<LockResult> owned(result);
        if (generation != m_scanGeneration)
            return; // Superseded by a newer scan - discard silently.

        EnableWindow(m_btnRefresh, TRUE);
        ApplyResult(*owned);
    }

    void QuickScanWindow::ApplyResult(const LockResult& result)
    {
        m_lastResult = result;
        SetWindowTextW(m_statusLabel, result.statusMessage.c_str());
        ResultsListView::Populate(m_listView, result);
    }

    ATOM RegisterClassOnce(HINSTANCE hInstance)
    {
        static ATOM atom = 0;
        if (atom) return atom;

        WNDCLASSEXW wc{ sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = QuickScanWindow::StaticWndProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = kClassName;
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wc.hIconSm = wc.hIcon;
        atom = RegisterClassExW(&wc);
        return atom;
    }

    QuickScanDialog::ExitAction QuickScanWindow::RunModal(HINSTANCE hInstance, int nCmdShow, const std::wstring& path)
    {
        RegisterClassOnce(hInstance);

        QuickScanWindow window;
        window.m_path = path;
        window.m_settings = SettingsService::Load();

        HWND hwnd = CreateWindowExW(
            0, kClassName, L"File Lock Finder - Quick Scan",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME | WS_MAXIMIZEBOX,
            CW_USEDEFAULT, CW_USEDEFAULT, UiHelpers::Scale(640), UiHelpers::Scale(440),
            nullptr, nullptr, hInstance, &window);

        if (!hwnd)
            return QuickScanDialog::ExitAction::Closed;

        ShowWindow(hwnd, nCmdShow);
        UpdateWindow(hwnd);

        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        return window.m_exitAction;
    }
}

QuickScanDialog::ExitAction QuickScanDialog::Show(HINSTANCE hInstance, int nCmdShow, const std::wstring& path)
{
    return QuickScanWindow::RunModal(hInstance, nCmdShow, path);
}

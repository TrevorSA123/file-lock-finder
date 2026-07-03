#include "AboutDialog.h"
#include "UiHelpers.h"
#include "Resource.h"

namespace
{
    const wchar_t* kClassName = L"FileLockFinder_AboutDialog";

    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
            case WM_COMMAND:
                if (LOWORD(wParam) == IDC_ABOUT_OK || LOWORD(wParam) == IDCANCEL)
                {
                    DestroyWindow(hwnd);
                    return 0;
                }
                break;

            case WM_CLOSE:
                DestroyWindow(hwnd);
                return 0;
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

void AboutDialog::Show(HWND owner)
{
    HINSTANCE hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE));
    RegisterClassOnce(hInstance);

    auto S = [](int v) { return UiHelpers::Scale(v); };

    const int width = S(400), height = S(220);
    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME, kClassName, L"About File Lock Finder",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height,
        owner, nullptr, hInstance, nullptr);
    if (!hwnd)
        return;

    RECT client{};
    GetClientRect(hwnd, &client);
    int contentWidth = client.right - client.left;

    HWND title = UiHelpers::CreateLabel(hwnd, hInstance, L"File Lock Finder", S(20), S(20), contentWidth - S(40), S(24), 0, 0);
    if (title)
    {
        static HFONT boldFont = CreateFontW(-S(20), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(boldFont), TRUE);
    }

    UiHelpers::CreateLabel(hwnd, hInstance, L"Created by Trevor Woollacott", S(20), S(54), contentWidth - S(40), S(20), 0, 0);

    UiHelpers::CreateLabel(hwnd, hInstance,
        L"A small Windows utility for identifying processes that are currently using a file.",
        S(20), S(84), contentWidth - S(40), S(60), 0, SS_LEFT);

    UiHelpers::CreateButtonControl(hwnd, hInstance, L"OK",
        contentWidth - S(20) - S(90), S(150), S(90), S(28), IDC_ABOUT_OK);

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
}

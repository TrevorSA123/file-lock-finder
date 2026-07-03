#include "UiHelpers.h"
#include <iterator>

namespace
{
    const wchar_t* kPromptClassName = L"FileLockFinder_TextPrompt";
    constexpr int kPromptEditId = 101;
    constexpr int kPromptOkId = 102;
    constexpr int kPromptCancelId = 103;

    struct PromptState
    {
        bool* okClicked = nullptr;
        std::wstring* outValue = nullptr;
    };

    LRESULT CALLBACK PromptWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
            case WM_COMMAND:
            {
                auto* state = reinterpret_cast<PromptState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
                int id = LOWORD(wParam);
                if (id == kPromptOkId)
                {
                    // Read the edit text before DestroyWindow - the control
                    // (and its text) is gone once the window is destroyed.
                    if (state)
                    {
                        if (state->okClicked) *state->okClicked = true;
                        if (state->outValue)
                        {
                            wchar_t buffer[4096];
                            GetWindowTextW(GetDlgItem(hwnd, kPromptEditId), buffer, static_cast<int>(std::size(buffer)));
                            *state->outValue = buffer;
                        }
                    }
                    DestroyWindow(hwnd);
                    return 0;
                }
                if (id == kPromptCancelId || id == IDCANCEL)
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
                auto* state = reinterpret_cast<PromptState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
                delete state;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                break;
            }
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    void RegisterPromptClassOnce(HINSTANCE hInstance)
    {
        static bool registered = false;
        if (registered) return;
        WNDCLASSEXW wc{ sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = PromptWndProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = kPromptClassName;
        RegisterClassExW(&wc);
        registered = true;
    }
}

int UiHelpers::Scale(int value)
{
    // Cached: this process is System (not Per-Monitor) DPI aware, so the
    // effective DPI is fixed for the whole run - see FileLockFinder.manifest.
    static const int dpi = static_cast<int>(GetDpiForSystem());
    return MulDiv(value, dpi, USER_DEFAULT_SCREEN_DPI);
}

HFONT UiHelpers::GetUiFont()
{
    static HFONT font = []() -> HFONT {
        NONCLIENTMETRICSW metrics = { sizeof(NONCLIENTMETRICSW) };
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
            return CreateFontIndirectW(&metrics.lfMessageFont);
        return static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    }();
    return font;
}

void UiHelpers::CenterOverOwner(HWND hwnd, HWND owner)
{
    RECT ownerRect{}, selfRect{};
    if (owner && IsWindow(owner))
        GetWindowRect(owner, &ownerRect);
    else
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &ownerRect, 0);

    GetWindowRect(hwnd, &selfRect);
    int width = selfRect.right - selfRect.left;
    int height = selfRect.bottom - selfRect.top;
    int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
    int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;
    SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

HWND UiHelpers::CreateLabel(HWND parent, HINSTANCE hInstance, const wchar_t* text, int x, int y, int w, int h, int id, DWORD extraStyle)
{
    HWND hwnd = CreateWindowExW(0, L"STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_LEFT | extraStyle,
        x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), hInstance, nullptr);
    if (hwnd) SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(GetUiFont()), TRUE);
    return hwnd;
}

HWND UiHelpers::CreateButtonControl(HWND parent, HINSTANCE hInstance, const wchar_t* text, int x, int y, int w, int h, int id)
{
    HWND hwnd = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), hInstance, nullptr);
    if (hwnd) SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(GetUiFont()), TRUE);
    return hwnd;
}

HWND UiHelpers::CreateCheckbox(HWND parent, HINSTANCE hInstance, const wchar_t* text, int x, int y, int w, int h, int id)
{
    HWND hwnd = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX | BS_MULTILINE,
        x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), hInstance, nullptr);
    if (hwnd) SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(GetUiFont()), TRUE);
    return hwnd;
}

bool UiHelpers::PromptForText(HWND owner, const wchar_t* title, const wchar_t* promptText, const std::wstring& initialValue, std::wstring& outValue)
{
    HINSTANCE hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE));
    RegisterPromptClassOnce(hInstance);

    auto S = [](int v) { return Scale(v); };

    const int width = S(460), height = S(170);
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kPromptClassName, title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height,
        owner, nullptr, hInstance, nullptr);
    if (!hwnd)
        return false;

    bool okClicked = false;
    auto* state = new PromptState();
    state->okClicked = &okClicked;
    state->outValue = &outValue;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

    RECT client{};
    GetClientRect(hwnd, &client);
    int contentWidth = client.right - client.left;

    CreateLabel(hwnd, hInstance, promptText, S(20), S(16), contentWidth - S(40), S(20));

    HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", initialValue.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        S(20), S(40), contentWidth - S(40), S(24), hwnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kPromptEditId)), hInstance, nullptr);
    if (edit) SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(GetUiFont()), TRUE);

    const int buttonWidth = S(90), buttonHeight = S(28);
    int buttonY = height - S(70);
    CreateButtonControl(hwnd, hInstance, L"OK", contentWidth - S(20) - (buttonWidth * 2 + S(10)), buttonY, buttonWidth, buttonHeight, kPromptOkId);
    CreateButtonControl(hwnd, hInstance, L"Cancel", contentWidth - S(20) - buttonWidth, buttonY, buttonWidth, buttonHeight, kPromptCancelId);

    CenterOverOwner(hwnd, owner);
    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetFocus(edit);

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

    return okClicked;
}

void UiHelpers::CopyTextToClipboard(HWND hwnd, const std::wstring& text)
{
    if (!OpenClipboard(hwnd))
        return;

    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (mem)
    {
        void* ptr = GlobalLock(mem);
        if (ptr)
        {
            memcpy(ptr, text.c_str(), bytes);
            GlobalUnlock(mem);
            SetClipboardData(CF_UNICODETEXT, mem); // Clipboard now owns `mem`.
        }
    }
    CloseClipboard();
}

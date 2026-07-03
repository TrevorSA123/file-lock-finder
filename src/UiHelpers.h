#pragma once

#include <windows.h>
#include <string>

// Small shared helpers to cut down on repeated CreateWindowExW boilerplate
// across MainWindow/AboutDialog/PreferencesDialog. Deliberately minimal.
namespace UiHelpers
{
    HFONT GetUiFont();
    void CenterOverOwner(HWND hwnd, HWND owner);

    // This app declares System (not Per-Monitor) DPI awareness, so there is
    // a single effective DPI for the whole process lifetime. All fixed pixel
    // layout constants (button sizes, margins, window sizes, font heights)
    // must be passed through this before use, or they end up sized for
    // 96 DPI while the system font (already DPI-correct) grows around them -
    // the exact "text spills outside the button" bug this exists to prevent.
    int Scale(int value);

    HWND CreateLabel(HWND parent, HINSTANCE hInstance, const wchar_t* text, int x, int y, int w, int h, int id = 0, DWORD extraStyle = 0);
    HWND CreateButtonControl(HWND parent, HINSTANCE hInstance, const wchar_t* text, int x, int y, int w, int h, int id);
    HWND CreateCheckbox(HWND parent, HINSTANCE hInstance, const wchar_t* text, int x, int y, int w, int h, int id);

    // Small modal text-entry prompt (used by "Schedule Rename on Reboot...").
    // Returns true if the user clicked OK, in which case outValue holds the
    // (unmodified, caller should trim/normalize) entered text.
    bool PromptForText(HWND owner, const wchar_t* title, const wchar_t* promptText, const std::wstring& initialValue, std::wstring& outValue);

    // Copies text to the clipboard as CF_UNICODETEXT. Shared by MainWindow's
    // and QuickScanDialog's "Copy Results" actions.
    void CopyTextToClipboard(HWND hwnd, const std::wstring& text);
}

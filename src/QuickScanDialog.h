#pragma once

#include <windows.h>
#include <string>

// A small standalone popup used only when AppSettings::useCompactPopupFromContextMenu
// is enabled and the app is launched with a path on the command line (as the
// Explorer context-menu verb always does). It scans the path and shows just
// the locking-process list - for users who don't want the full main window.
// It offers an "Open Full Window" escape hatch for everything else
// (Advanced Scan, Run as Administrator, schedule delete/rename, etc.).
namespace QuickScanDialog
{
    enum class ExitAction
    {
        Closed,
        OpenFullWindow,
    };

    // Runs its own message loop internally and returns once the popup is
    // closed. `path` is scanned immediately on creation.
    ExitAction Show(HINSTANCE hInstance, int nCmdShow, const std::wstring& path);
}

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <commctrl.h>
#include <string>

#include "MainWindow.h"
#include "QuickScanDialog.h"
#include "../Core/SettingsService.h"
#include "../WindowsApi/ContextMenuIntegrationService.h"

#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Gdi32.lib")
#pragma comment(lib, "Shell32.lib")

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR /*pCmdLine*/, int nCmdShow)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    INITCOMMONCONTROLSEX icc{ sizeof(icc) };
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    // The first non-flag command-line argument (if any) is the file/folder
    // path to inspect immediately. CommandLineToArgvW correctly handles
    // quoted paths with spaces and Unicode characters. Separately, we look
    // for kContextMenuLaunchFlag, which only the registered context-menu
    // command line includes (see ContextMenuIntegrationService) - this is
    // what distinguishes "the user right-clicked and chose Find locking
    // processes" from any other launch that happens to carry a path, such
    // as a plain command line or MainWindow's own "Run as Administrator"
    // relaunch. Only the former should ever honor the compact-popup setting.
    std::wstring initialPath;
    bool launchedFromContextMenu = false;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv)
    {
        for (int i = 1; i < argc; ++i)
        {
            if (wcscmp(argv[i], kContextMenuLaunchFlag) == 0)
                launchedFromContextMenu = true;
            else if (initialPath.empty())
                initialPath = argv[i];
        }
        LocalFree(argv);
    }

    // If the user opted into the compact popup and this is a genuine
    // context-menu launch, show just the results popup instead of the full
    // main window. The popup's "Open Full Window" button falls through to
    // the normal MainWindow below.
    AppSettings settings = SettingsService::Load();
    if (launchedFromContextMenu && !initialPath.empty() && settings.useCompactPopupFromContextMenu)
    {
        QuickScanDialog::ExitAction action = QuickScanDialog::Show(hInstance, nCmdShow, initialPath);
        if (action != QuickScanDialog::ExitAction::OpenFullWindow)
        {
            CoUninitialize();
            return 0;
        }
    }

    MainWindow* window = MainWindow::Create(hInstance, nCmdShow, initialPath);
    if (!window)
    {
        CoUninitialize();
        return 0;
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return static_cast<int>(msg.wParam);
}

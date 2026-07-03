#pragma once

#include <windows.h>
#include "../Core/LockResult.h"

// The results grid (Process Name, PID, Executable Path, Type, Detection
// Source, Notes) is shared verbatim by MainWindow and the compact
// QuickScanDialog popup, so column setup and row population live here once.
namespace ResultsListView
{
    void SetupColumns(HWND listView);
    void Populate(HWND listView, const LockResult& result);
}

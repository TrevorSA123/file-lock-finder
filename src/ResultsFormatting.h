#pragma once

#include <string>
#include "../Core/LockResult.h"

// Builds the plain-text clipboard representation of a scan result, shared
// by MainWindow's and QuickScanDialog's "Copy Results" actions.
namespace ResultsFormatting
{
    std::wstring ToClipboardText(const LockResult& result);
}

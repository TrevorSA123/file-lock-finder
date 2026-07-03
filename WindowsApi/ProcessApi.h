#pragma once

#include <string>
#include <windows.h>

struct ProcessDetails
{
    bool resolved = false;
    std::wstring processName;    // File name only, e.g. "notepad.exe".
    std::wstring executablePath; // Full path, when it could be resolved.
};

namespace ProcessApi
{
    // Uses PROCESS_QUERY_LIMITED_INFORMATION only (least privilege) - never
    // PROCESS_ALL_ACCESS. Returns resolved=false rather than throwing if the
    // process cannot be inspected (e.g. a protected/elevated process).
    ProcessDetails QueryProcessDetails(DWORD pid);
}

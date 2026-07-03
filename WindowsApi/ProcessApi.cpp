#include "ProcessApi.h"
#include "Win32Handle.h"
#include <shlwapi.h>
#include <iterator>

#pragma comment(lib, "Shlwapi.lib")

ProcessDetails ProcessApi::QueryProcessDetails(DWORD pid)
{
    ProcessDetails details;

    Win32Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!process.IsValid())
        return details; // Likely a protected/system process - leave unresolved rather than fail the scan.

    wchar_t buffer[32768];
    DWORD size = static_cast<DWORD>(std::size(buffer));
    if (QueryFullProcessImageNameW(process.Get(), 0, buffer, &size))
    {
        details.executablePath.assign(buffer, size);
        details.resolved = true;

        const wchar_t* fileName = PathFindFileNameW(details.executablePath.c_str());
        details.processName = fileName ? fileName : details.executablePath;
    }

    return details;
}

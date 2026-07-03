#include "NativeFileApi.h"
#include "Win32Handle.h"

namespace
{
    std::wstring FormatWin32Error(DWORD err)
    {
        LPWSTR buffer = nullptr;
        DWORD len = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

        std::wstring result;
        if (len > 0 && buffer)
        {
            result.assign(buffer, len);
            while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n'))
                result.pop_back();
        }
        if (buffer)
            LocalFree(buffer);

        if (result.empty())
            result = L"Windows error " + std::to_wstring(err) + L".";
        return result;
    }
}

FileProbeResult NativeFileApi::ProbeLock(const std::wstring& path, bool isDirectory)
{
    FileProbeResult result;

    // We ask for DELETE plus the ability to read basic attributes - enough
    // to detect a sharing violation on delete/rename, without requesting
    // broad read/write access we do not need for a probe.
    DWORD desiredAccess = DELETE | FILE_READ_ATTRIBUTES;
    DWORD shareMode = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    DWORD flags = FILE_ATTRIBUTE_NORMAL;
    if (isDirectory)
        flags |= FILE_FLAG_BACKUP_SEMANTICS; // Required by CreateFile to open a directory handle.

    Win32Handle handle(CreateFileW(
        path.c_str(), desiredAccess, shareMode, nullptr,
        OPEN_EXISTING, flags, nullptr));

    if (handle.IsValid())
    {
        result.couldOpen = true;
        result.message = isDirectory
            ? L"The folder could be opened with delete-sharing access."
            : L"The file could be opened with delete-sharing access.";
        return result;
    }

    result.win32Error = GetLastError();

    switch (result.win32Error)
    {
        case ERROR_SHARING_VIOLATION:
            result.likelyLocked = true;
            result.message = L"Another process has this open in a way that blocks delete/rename.";
            break;

        case ERROR_ACCESS_DENIED:
            result.accessDenied = true;
            result.message = L"Access was denied. This may be a permissions or read-only attribute issue.";
            break;

        default:
            result.message = FormatWin32Error(result.win32Error);
            break;
    }

    return result;
}

#include "MoveFileExApi.h"

#include <windows.h>

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

bool MoveFileExApi::ScheduleDeleteOnReboot(const std::wstring& path, std::wstring& errorMessage)
{
    if (MoveFileExW(path.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT))
        return true;

    errorMessage = FormatWin32Error(GetLastError());
    return false;
}

bool MoveFileExApi::ScheduleRenameOnReboot(const std::wstring& sourcePath, const std::wstring& targetPath, std::wstring& errorMessage)
{
    if (MoveFileExW(sourcePath.c_str(), targetPath.c_str(), MOVEFILE_DELAY_UNTIL_REBOOT))
        return true;

    errorMessage = FormatWin32Error(GetLastError());
    return false;
}

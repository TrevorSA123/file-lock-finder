#pragma once

#include <string>

// Thin wrapper over MoveFileExW's MOVEFILE_DELAY_UNTIL_REBOOT flag. The UI
// is responsible for confirming with the user before calling either
// function - these never prompt themselves.
namespace MoveFileExApi
{
    bool ScheduleDeleteOnReboot(const std::wstring& path, std::wstring& errorMessage);
    bool ScheduleRenameOnReboot(const std::wstring& sourcePath, const std::wstring& targetPath, std::wstring& errorMessage);
}

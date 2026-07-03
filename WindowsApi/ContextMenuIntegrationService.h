#pragma once

#include <string>
#include "../Core/AppSettings.h"

// Marker argument that only the registered context-menu command line
// includes (see ContextMenuIntegrationService.cpp), so the app can tell
// "invoked via the Explorer right-click verb" apart from any other launch
// that happens to carry a path argument - a manual command line, a
// double-click file association, or MainWindow's own "Run as Administrator"
// relaunch. Only a true context-menu launch should ever honor
// AppSettings::useCompactPopupFromContextMenu; main.cpp checks for this
// exact token.
extern const wchar_t* const kContextMenuLaunchFlag;

enum class ContextMenuRegistrationStatus
{
    NotRegistered,
    CorrectlyRegistered,
    PointsToMissingExecutable,
    PointsToDifferentExecutable,
    PartiallyRegistered,
    Error
};

// Registers/unregisters a classic Explorer right-click verb under
// HKEY_CURRENT_USER\Software\Classes. This is deliberately the low-complexity
// integration path: per-user registry keys need no admin rights and no
// installer. On Windows 11 the entry may only appear under
// "Show more options" (the classic context menu), because native top-level
// integration on Windows 11 requires a packaged IExplorerCommand shell
// extension with an app identity/registration - a much larger undertaking
// that is intentionally out of scope for this version.
class ContextMenuIntegrationService
{
public:
    static std::wstring GetCurrentExecutablePath();

    // Creates/updates or removes the file (`*`) shell verb.
    static bool SetFileContextMenu(bool enabled, std::wstring& error);

    // Creates/updates or removes the Directory shell verb.
    static bool SetFolderContextMenu(bool enabled, std::wstring& error);

    // Applies both file/folder registrations from settings in one call,
    // repairing stale entries (pointing at an old exe path) along the way.
    static bool ApplyPreferences(const AppSettings& settings, std::wstring& error);

    static bool IsContextMenuRegisteredForCurrentExecutable();

    static ContextMenuRegistrationStatus GetRegistrationStatus(bool forFolder, std::wstring* detail = nullptr);
};

#include "ContextMenuIntegrationService.h"

#include <windows.h>
#include <vector>
#include <algorithm>
#include <cwctype>

#pragma comment(lib, "Advapi32.lib")

const wchar_t* const kContextMenuLaunchFlag = L"--quick-scan";

namespace
{
    const wchar_t* kVerbName = L"FileLockFinder";
    const wchar_t* kVerbLabel = L"Find locking processes";

    std::wstring VerbKeyPath(const std::wstring& subject)
    {
        return L"Software\\Classes\\" + subject + L"\\shell\\" + kVerbName;
    }

    std::wstring CommandKeyPath(const std::wstring& subject)
    {
        return VerbKeyPath(subject) + L"\\command";
    }

    bool EqualsCaseInsensitive(const std::wstring& a, const std::wstring& b)
    {
        if (a.size() != b.size())
            return false;
        return std::equal(a.begin(), a.end(), b.begin(), [](wchar_t l, wchar_t r) {
            return std::towlower(l) == std::towlower(r);
        });
    }

    bool TryParseExePathFromCommand(const std::wstring& commandValue, std::wstring& exePathOut)
    {
        if (commandValue.size() < 2 || commandValue.front() != L'"')
            return false;
        size_t closing = commandValue.find(L'"', 1);
        if (closing == std::wstring::npos)
            return false;
        exePathOut = commandValue.substr(1, closing - 1);
        return true;
    }

    bool ReadDefaultStringValue(HKEY root, const std::wstring& subKeyPath, std::wstring& valueOut)
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(root, subKeyPath.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
            return false;

        wchar_t buffer[4096] = {};
        DWORD size = sizeof(buffer);
        DWORD type = 0;
        LONG queryResult = RegQueryValueExW(key, nullptr, nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &size);
        RegCloseKey(key);

        if (queryResult != ERROR_SUCCESS || type != REG_SZ)
            return false;

        valueOut.assign(buffer);
        return true;
    }

    bool KeyExists(HKEY root, const std::wstring& subKeyPath)
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(root, subKeyPath.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
            return false;
        RegCloseKey(key);
        return true;
    }

    bool WriteStringValue(HKEY key, const wchar_t* valueName, const std::wstring& value)
    {
        DWORD sizeBytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
        return RegSetValueExW(key, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), sizeBytes) == ERROR_SUCCESS;
    }

    bool WriteVerbKeys(const std::wstring& subject, const std::wstring& exePath, std::wstring& error)
    {
        HKEY verbKey = nullptr;
        LONG rc = RegCreateKeyExW(HKEY_CURRENT_USER, VerbKeyPath(subject).c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &verbKey, nullptr);
        if (rc != ERROR_SUCCESS)
        {
            error = L"Could not create the context-menu registry key (error " + std::to_wstring(rc) + L").";
            return false;
        }

        bool ok = WriteStringValue(verbKey, nullptr, kVerbLabel) && WriteStringValue(verbKey, L"Icon", exePath);
        RegCloseKey(verbKey);
        if (!ok)
        {
            error = L"Could not write context-menu registry values.";
            return false;
        }

        HKEY commandKey = nullptr;
        rc = RegCreateKeyExW(HKEY_CURRENT_USER, CommandKeyPath(subject).c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &commandKey, nullptr);
        if (rc != ERROR_SUCCESS)
        {
            error = L"Could not create the context-menu command key (error " + std::to_wstring(rc) + L").";
            return false;
        }

        std::wstring commandValue = L"\"" + exePath + L"\" " + kContextMenuLaunchFlag + L" \"%1\"";
        ok = WriteStringValue(commandKey, nullptr, commandValue);
        RegCloseKey(commandKey);
        if (!ok)
        {
            error = L"Could not write the context-menu command value.";
            return false;
        }

        return true;
    }

    bool RemoveVerbKeys(const std::wstring& subject, std::wstring& error)
    {
        // RegDeleteTreeW removes exactly this key (and its "command" child)
        // and nothing else - never touches keys this application did not create.
        LONG rc = RegDeleteTreeW(HKEY_CURRENT_USER, VerbKeyPath(subject).c_str());
        if (rc != ERROR_SUCCESS && rc != ERROR_FILE_NOT_FOUND)
        {
            error = L"Could not remove the context-menu registry key (error " + std::to_wstring(rc) + L").";
            return false;
        }
        return true;
    }
}

std::wstring ContextMenuIntegrationService::GetCurrentExecutablePath()
{
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;)
    {
        DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (len == 0)
            return L"";
        if (len < buffer.size() - 1)
            return std::wstring(buffer.data(), len);
        buffer.resize(buffer.size() * 2);
    }
}

bool ContextMenuIntegrationService::SetFileContextMenu(bool enabled, std::wstring& error)
{
    if (!enabled)
        return RemoveVerbKeys(L"*", error);
    return WriteVerbKeys(L"*", GetCurrentExecutablePath(), error);
}

bool ContextMenuIntegrationService::SetFolderContextMenu(bool enabled, std::wstring& error)
{
    if (!enabled)
        return RemoveVerbKeys(L"Directory", error);
    return WriteVerbKeys(L"Directory", GetCurrentExecutablePath(), error);
}

bool ContextMenuIntegrationService::ApplyPreferences(const AppSettings& settings, std::wstring& error)
{
    std::wstring fileError, folderError;
    bool fileOk = SetFileContextMenu(settings.addToContextMenu, fileError);
    bool folderOk = SetFolderContextMenu(settings.addToContextMenu && settings.includeFoldersInContextMenu, folderError);

    if (!fileOk || !folderOk)
    {
        error = fileError;
        if (!folderError.empty())
        {
            if (!error.empty())
                error += L" ";
            error += folderError;
        }
        return false;
    }

    return true;
}

ContextMenuRegistrationStatus ContextMenuIntegrationService::GetRegistrationStatus(bool forFolder, std::wstring* detail)
{
    std::wstring subject = forFolder ? L"Directory" : L"*";

    bool verbExists = KeyExists(HKEY_CURRENT_USER, VerbKeyPath(subject));

    std::wstring commandValue;
    bool commandExists = ReadDefaultStringValue(HKEY_CURRENT_USER, CommandKeyPath(subject), commandValue);

    if (!verbExists && !commandExists)
        return ContextMenuRegistrationStatus::NotRegistered;

    if (verbExists != commandExists)
    {
        if (detail) *detail = L"Only part of the context-menu registration is present.";
        return ContextMenuRegistrationStatus::PartiallyRegistered;
    }

    std::wstring parsedExePath;
    if (!TryParseExePathFromCommand(commandValue, parsedExePath))
    {
        if (detail) *detail = L"The context-menu command value could not be parsed.";
        return ContextMenuRegistrationStatus::PartiallyRegistered;
    }

    if (GetFileAttributesW(parsedExePath.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        if (detail) *detail = L"Registered executable no longer exists: " + parsedExePath;
        return ContextMenuRegistrationStatus::PointsToMissingExecutable;
    }

    std::wstring currentExePath = GetCurrentExecutablePath();
    if (!EqualsCaseInsensitive(parsedExePath, currentExePath))
    {
        if (detail) *detail = L"Registered to a different executable: " + parsedExePath;
        return ContextMenuRegistrationStatus::PointsToDifferentExecutable;
    }

    return ContextMenuRegistrationStatus::CorrectlyRegistered;
}

bool ContextMenuIntegrationService::IsContextMenuRegisteredForCurrentExecutable()
{
    return GetRegistrationStatus(false) == ContextMenuRegistrationStatus::CorrectlyRegistered
        || GetRegistrationStatus(true) == ContextMenuRegistrationStatus::CorrectlyRegistered;
}

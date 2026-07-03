#include "SettingsService.h"

#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cwctype>

#pragma comment(lib, "Shell32.lib")

namespace
{
    std::wstring BoolToJson(bool value) { return value ? L"true" : L"false"; }

    // Extremely small, schema-specific reader: finds "Key": true|false.
    // Not a general JSON parser - deliberately so, since the document only
    // ever contains flat boolean fields.
    bool ExtractBool(const std::wstring& json, const wchar_t* key, bool defaultValue)
    {
        std::wstring needle = L"\"";
        needle += key;
        needle += L"\"";

        size_t pos = json.find(needle);
        if (pos == std::wstring::npos)
            return defaultValue;

        pos = json.find(L':', pos + needle.size());
        if (pos == std::wstring::npos)
            return defaultValue;

        size_t truePos = json.find(L"true", pos);
        size_t falsePos = json.find(L"false", pos);

        // Whichever keyword appears first (and reasonably close) after the
        // colon is the value for this key.
        if (truePos != std::wstring::npos && (falsePos == std::wstring::npos || truePos < falsePos))
            return true;
        if (falsePos != std::wstring::npos)
            return false;

        return defaultValue;
    }
}

std::wstring SettingsService::GetSettingsDirectory()
{
    PWSTR appDataPath = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appDataPath)))
    {
        result = appDataPath;
        result += L"\\FileLockFinder";
    }
    if (appDataPath)
        CoTaskMemFree(appDataPath);
    return result;
}

std::wstring SettingsService::GetSettingsFilePath()
{
    std::wstring dir = GetSettingsDirectory();
    if (dir.empty())
        return dir;
    return dir + L"\\settings.json";
}

AppSettings SettingsService::Load()
{
    AppSettings settings; // defaults

    std::wstring path = GetSettingsFilePath();
    if (path.empty())
        return settings;

    std::wifstream file(path, std::ios::binary);
    if (!file.is_open())
        return settings; // No file yet - defaults are correct.

    std::wstringstream buffer;
    buffer << file.rdbuf();
    std::wstring json = buffer.str();

    if (json.empty())
        return settings; // Recover gracefully from an empty/corrupt file.

    settings.addToContextMenu = ExtractBool(json, L"AddToContextMenu", settings.addToContextMenu);
    settings.includeFoldersInContextMenu = ExtractBool(json, L"IncludeFoldersInContextMenu", settings.includeFoldersInContextMenu);
    settings.autoScanOnOpen = ExtractBool(json, L"AutoScanOnOpen", settings.autoScanOnOpen);
    settings.enableAdvancedScanByDefault = ExtractBool(json, L"EnableAdvancedScanByDefault", settings.enableAdvancedScanByDefault);
    settings.confirmBeforeRebootActions = ExtractBool(json, L"ConfirmBeforeRebootActions", settings.confirmBeforeRebootActions);
    settings.checkContextMenuIntegrationAtStartup = ExtractBool(json, L"CheckContextMenuIntegrationAtStartup", settings.checkContextMenuIntegrationAtStartup);
    settings.useCompactPopupFromContextMenu = ExtractBool(json, L"UseCompactPopupFromContextMenu", settings.useCompactPopupFromContextMenu);

    return settings;
}

bool SettingsService::Save(const AppSettings& settings, std::wstring& errorMessage)
{
    std::wstring dir = GetSettingsDirectory();
    if (dir.empty())
    {
        errorMessage = L"Could not determine the AppData folder.";
        return false;
    }

    if (!CreateDirectoryW(dir.c_str(), nullptr))
    {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS)
        {
            errorMessage = L"Could not create the settings folder (error " + std::to_wstring(err) + L").";
            return false;
        }
    }

    std::wstringstream json;
    json << L"{\r\n"
         << L"  \"AddToContextMenu\": " << BoolToJson(settings.addToContextMenu) << L",\r\n"
         << L"  \"IncludeFoldersInContextMenu\": " << BoolToJson(settings.includeFoldersInContextMenu) << L",\r\n"
         << L"  \"AutoScanOnOpen\": " << BoolToJson(settings.autoScanOnOpen) << L",\r\n"
         << L"  \"EnableAdvancedScanByDefault\": " << BoolToJson(settings.enableAdvancedScanByDefault) << L",\r\n"
         << L"  \"ConfirmBeforeRebootActions\": " << BoolToJson(settings.confirmBeforeRebootActions) << L",\r\n"
         << L"  \"CheckContextMenuIntegrationAtStartup\": " << BoolToJson(settings.checkContextMenuIntegrationAtStartup) << L",\r\n"
         << L"  \"UseCompactPopupFromContextMenu\": " << BoolToJson(settings.useCompactPopupFromContextMenu) << L"\r\n"
         << L"}\r\n";

    std::wstring path = GetSettingsFilePath();
    std::wofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
    {
        errorMessage = L"Could not open settings.json for writing.";
        return false;
    }

    file << json.str();
    if (!file.good())
    {
        errorMessage = L"Could not write settings.json.";
        return false;
    }

    return true;
}

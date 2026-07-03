#pragma once

#include <string>
#include "AppSettings.h"

// Loads and saves AppSettings as JSON under %AppData%\FileLockFinder\settings.json.
//
// A full JSON library is unnecessary for a flat document with six boolean
// fields, so this reads/writes a small fixed-schema JSON document by hand.
// This keeps the dependency list at "Windows API only" as requested.
class SettingsService
{
public:
    static std::wstring GetSettingsFilePath();
    static std::wstring GetSettingsDirectory();

    // Returns defaults if the file does not exist or cannot be parsed.
    static AppSettings Load();

    // Creates the settings directory if needed. Returns false on failure
    // and fills errorMessage with a friendly (non-stack-trace) description.
    static bool Save(const AppSettings& settings, std::wstring& errorMessage);
};

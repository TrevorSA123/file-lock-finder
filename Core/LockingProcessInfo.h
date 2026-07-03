#pragma once

#include <string>

// One process (or service) that appears to be using the scanned file/folder.
struct LockingProcessInfo
{
    unsigned long processId = 0;
    std::wstring processName;
    std::wstring executablePath;   // May be empty if it could not be resolved.
    std::wstring applicationType;  // e.g. "Application", "Service", "Explorer", "Console", "Critical Process".
    std::wstring serviceShortName; // Populated when Restart Manager identifies a Windows service.
    std::wstring detectionSource;  // "Restart Manager", "Advanced Handle Scan", "Probe", or "Other".
    std::wstring notes;
};

#pragma once

#include <string>
#include <windows.h>

// Result of a safe, non-destructive probe of whether a file/folder can be
// opened with delete-share semantics. This never deletes, renames, or
// otherwise mutates the target - it only attempts to open it and immediately
// closes the handle.
struct FileProbeResult
{
    bool couldOpen = false;      // Handle opened successfully -> nothing else has it exclusively locked.
    bool likelyLocked = false;   // ERROR_SHARING_VIOLATION.
    bool accessDenied = false;   // ERROR_ACCESS_DENIED or similar permission/attribute issue.
    DWORD win32Error = 0;
    std::wstring message;        // Friendly, non-technical explanation.
};

namespace NativeFileApi
{
    // path must already be normalized (see PathNormalizer::Normalize).
    FileProbeResult ProbeLock(const std::wstring& path, bool isDirectory);
}

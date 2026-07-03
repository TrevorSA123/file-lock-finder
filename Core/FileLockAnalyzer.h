#pragma once

#include <string>
#include <atomic>
#include "LockResult.h"

// Coordinates a full scan of a single path: normalizes it, probes it
// directly, asks Restart Manager, optionally runs the Advanced Handle Scan,
// de-duplicates the results, and produces a user-facing status/advice.
//
// This class contains no UI code and no P/Invoke-equivalent native calls of
// its own - all native API access lives in WindowsApi/. The UI calls this
// class on a background thread and only touches HWNDs with the result.
class FileLockAnalyzer
{
public:
    static LockResult Scan(const std::wstring& rawPath, bool useAdvancedScan, std::atomic<bool>* cancelRequested);
};

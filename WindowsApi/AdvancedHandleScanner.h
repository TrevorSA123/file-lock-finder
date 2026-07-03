#pragma once

#include <string>
#include <vector>
#include <atomic>
#include "../Core/LockingProcessInfo.h"

// Optional fallback/advanced detection method: enumerates every open handle
// on the system via NtQuerySystemInformation and looks for file handles that
// resolve to the target path.
//
// This is best-effort by nature. NtQuerySystemInformation and its handle
// information structures are undocumented internal Windows behaviour (not a
// public, versioned contract like Restart Manager), so field layouts and
// behaviour can vary between Windows builds. All native/internal API code
// for this technique is isolated in this file/translation unit.
namespace AdvancedHandleScanner
{
    // Every path in normalizedPaths must already be absolute (see
    // PathNormalizer). A single file scan passes a one-element vector; a
    // folder scan passes the folder plus every file/subfolder inside it, so
    // a matching handle is reported no matter which item within the tree it
    // has open (see FileLockAnalyzer::BuildScanTargets). cancelRequested is
    // polled between processes so the caller can abort a long-running scan
    // (e.g. the user closed the form or started a new scan).
    std::vector<LockingProcessInfo> Scan(
        const std::vector<std::wstring>& normalizedPaths,
        std::atomic<bool>& cancelRequested,
        std::vector<std::wstring>& errors);
}

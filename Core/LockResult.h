#pragma once

#include <string>
#include <vector>
#include <ctime>
#include "LockStatus.h"
#include "LockingProcessInfo.h"

// Structured outcome of a single scan, returned by FileLockAnalyzer::Scan.
// The UI only ever reads this object; all decision-making happens in Core/.
struct LockResult
{
    std::wstring selectedPath;
    bool exists = false;
    bool isDirectory = false;

    LockStatus status = LockStatus::NoPathSelected;
    std::wstring statusMessage;
    std::wstring advice;

    std::vector<LockingProcessInfo> processes;
    std::vector<std::wstring> errors;

    bool wasRestartManagerUsed = false;
    bool wasAdvancedScanUsed = false;
    bool requiresElevationRecommended = false;

    std::time_t timestamp = 0;
};

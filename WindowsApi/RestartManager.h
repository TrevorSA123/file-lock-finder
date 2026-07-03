#pragma once

#include <string>
#include <vector>
#include "../Core/LockingProcessInfo.h"

// Restart Manager (Rstrtmgr.h) is the documented, stable, first-pass way to
// ask Windows "what is using this file?" - the same mechanism Windows Update
// and MSI installers use to decide what needs to be closed/restarted. It is
// preferred over raw handle enumeration because it is a supported public API
// with well-defined semantics, whereas handle enumeration relies on
// undocumented internals (see AdvancedHandleScanner).
namespace RestartManager
{
    // Every path must already be normalized. For a single file/folder,
    // pass a one-element vector. For a folder, callers pass the folder path
    // plus every file/subfolder inside it (see FileLockAnalyzer::BuildScanTargets)
    // so RM can report processes holding any of them open - a folder handle
    // itself is rarely what blocks a rename/delete, a file somewhere inside
    // it usually is. All paths are registered in a single RM session, which
    // is both more efficient and gives one merged process list. Never
    // restarts or shuts down any application/service - it only asks Restart
    // Manager to enumerate them.
    std::vector<LockingProcessInfo> Scan(const std::vector<std::wstring>& paths, std::vector<std::wstring>& errors);
}

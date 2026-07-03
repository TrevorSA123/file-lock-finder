#include "RestartManager.h"
#include "ProcessApi.h"

#include <windows.h>
#include <restartmanager.h>
#include <map>
#include <algorithm>

#pragma comment(lib, "Rstrtmgr.lib")

namespace
{
    const wchar_t* AppTypeToString(RM_APP_TYPE type)
    {
        switch (type)
        {
            case RmMainWindow:  return L"Application";
            case RmOtherWindow: return L"Application";
            case RmService:     return L"Service";
            case RmExplorer:    return L"Explorer";
            case RmConsole:     return L"Console";
            case RmCritical:    return L"Critical System Process";
            case RmUnknownApp:
            default:            return L"Other";
        }
    }

    // RAII so RmEndSession always runs, even if a step above throws or
    // returns early - mirrors "always call RmEndSession in a finally block".
    class RmSessionGuard
    {
    public:
        explicit RmSessionGuard(DWORD session) : m_session(session), m_active(true) {}
        ~RmSessionGuard() { if (m_active) RmEndSession(m_session); }
        RmSessionGuard(const RmSessionGuard&) = delete;
        RmSessionGuard& operator=(const RmSessionGuard&) = delete;
    private:
        DWORD m_session;
        bool m_active;
    };
}

std::vector<LockingProcessInfo> RestartManager::Scan(const std::vector<std::wstring>& paths, std::vector<std::wstring>& errors)
{
    std::vector<LockingProcessInfo> results;
    if (paths.empty())
        return results;

    DWORD session = 0;
    wchar_t sessionKey[CCH_RM_SESSION_KEY + 1] = {};

    DWORD startStatus = RmStartSession(&session, 0, sessionKey);
    if (startStatus != ERROR_SUCCESS)
    {
        errors.push_back(L"Restart Manager could not start a session (error " + std::to_wstring(startStatus) + L").");
        return results;
    }

    RmSessionGuard guard(session);

    // Registered in batches (rather than one call with the whole vector) to
    // stay well within practical per-call limits while still using a single
    // RM session - important when scanning a folder, which can mean
    // hundreds or thousands of paths (see FileLockAnalyzer::BuildScanTargets).
    constexpr size_t kBatchSize = 200;
    std::vector<const wchar_t*> batch;
    batch.reserve(std::min(kBatchSize, paths.size()));

    for (size_t offset = 0; offset < paths.size(); offset += kBatchSize)
    {
        batch.clear();
        size_t end = std::min(offset + kBatchSize, paths.size());
        for (size_t i = offset; i < end; ++i)
            batch.push_back(paths[i].c_str());

        DWORD registerStatus = RmRegisterResources(session, static_cast<UINT>(batch.size()), batch.data(), 0, nullptr, 0, nullptr);
        if (registerStatus != ERROR_SUCCESS)
        {
            errors.push_back(L"Restart Manager could not register " + std::to_wstring(batch.size()) +
                L" path(s) (error " + std::to_wstring(registerStatus) + L"); results may be incomplete.");
            // Keep going with whatever was registered so far rather than aborting entirely.
        }
    }

    UINT procInfoNeeded = 0;
    UINT procInfoCount = 0;
    DWORD rebootReasons = 0;

    // First call is expected to fail with ERROR_MORE_DATA and tell us how
    // many RM_PROCESS_INFO entries are needed.
    DWORD listStatus = RmGetList(session, &procInfoNeeded, &procInfoCount, nullptr, &rebootReasons);
    if (listStatus != ERROR_SUCCESS && listStatus != ERROR_MORE_DATA)
    {
        errors.push_back(L"Restart Manager could not enumerate processes (error " + std::to_wstring(listStatus) + L").");
        return results;
    }

    if (procInfoNeeded == 0)
        return results; // Nothing is using the resource according to Restart Manager.

    std::vector<RM_PROCESS_INFO> processInfos(procInfoNeeded);
    procInfoCount = procInfoNeeded;

    listStatus = RmGetList(session, &procInfoNeeded, &procInfoCount, processInfos.data(), &rebootReasons);
    if (listStatus != ERROR_SUCCESS)
    {
        errors.push_back(L"Restart Manager could not enumerate processes (error " + std::to_wstring(listStatus) + L").");
        return results;
    }

    results.reserve(procInfoCount);
    for (UINT i = 0; i < procInfoCount; ++i)
    {
        const RM_PROCESS_INFO& info = processInfos[i];

        LockingProcessInfo entry;
        entry.processId = info.Process.dwProcessId;
        entry.processName = info.strAppName;
        entry.applicationType = AppTypeToString(info.ApplicationType);
        entry.serviceShortName = info.strServiceShortName;
        entry.detectionSource = L"Restart Manager";

        ProcessDetails details = ProcessApi::QueryProcessDetails(entry.processId);
        if (details.resolved)
        {
            entry.executablePath = details.executablePath;
            if (entry.processName.empty())
                entry.processName = details.processName;
        }

        if (!info.bRestartable)
            entry.notes = L"Restart Manager reports this application cannot be automatically restarted.";

        results.push_back(std::move(entry));
    }

    return results;
}

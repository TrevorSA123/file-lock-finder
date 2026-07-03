#include "AdvancedHandleScanner.h"
#include "ProcessApi.h"

#include <windows.h>
#include <map>
#include <vector>
#include <unordered_set>
#include <algorithm>
#include <cwctype>
#include <cwchar>
#include <iterator>
#include <sstream>
#include <utility>

// ---------------------------------------------------------------------------
// Undocumented NTAPI structures/constants.
//
// These are NOT part of the public Windows SDK. Field layouts have been
// stable in practice for a long time, but Microsoft makes no compatibility
// guarantee for them, which is exactly why Restart Manager (a documented,
// public API) is used as the primary detection method and this scanner is
// only an optional fallback. All of this internal surface is isolated to
// this one translation unit.
// ---------------------------------------------------------------------------
namespace
{
    typedef LONG NTSTATUS_LOCAL;
    constexpr NTSTATUS_LOCAL kStatusSuccess = 0;
    constexpr NTSTATUS_LOCAL kStatusInfoLengthMismatch = (NTSTATUS_LOCAL)0xC0000004L;
    constexpr ULONG kSystemExtendedHandleInformation = 64;
    constexpr ULONG kObjectNameInformation = 1;

    struct UNICODE_STRING_LOCAL
    {
        USHORT Length;
        USHORT MaximumLength;
        PWSTR Buffer;
    };

    struct OBJECT_NAME_INFORMATION_LOCAL
    {
        UNICODE_STRING_LOCAL Name;
    };

#pragma pack(push, 8)
    struct SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX_LOCAL
    {
        PVOID Object;
        ULONG_PTR UniqueProcessId;
        ULONG_PTR HandleValue;
        ULONG GrantedAccess;
        USHORT CreatorBackTraceIndex;
        USHORT ObjectTypeIndex;
        ULONG HandleAttributes;
        ULONG Reserved;
    };

    struct SYSTEM_HANDLE_INFORMATION_EX_LOCAL
    {
        ULONG_PTR NumberOfHandles;
        ULONG_PTR Reserved;
        SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX_LOCAL Handles[1];
    };
#pragma pack(pop)

    using NtQuerySystemInformationFn = NTSTATUS_LOCAL(WINAPI*)(ULONG, PVOID, ULONG, PULONG);
    using NtQueryObjectFn = NTSTATUS_LOCAL(WINAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);

    NtQueryObjectFn GetNtQueryObject()
    {
        static NtQueryObjectFn fn = []() -> NtQueryObjectFn {
            HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
            if (!ntdll) return nullptr;
            return reinterpret_cast<NtQueryObjectFn>(GetProcAddress(ntdll, "NtQueryObject"));
        }();
        return fn;
    }

    // Maps NT device paths (e.g. "\Device\HarddiskVolume3\") to drive
    // letters, built once from the current drive list. Needed because the
    // NtQueryObject fallback below returns paths in NT device form, not the
    // "C:\..." form the rest of the app compares against.
    std::vector<std::pair<std::wstring, wchar_t>> BuildDeviceDriveMap()
    {
        std::vector<std::pair<std::wstring, wchar_t>> map;

        wchar_t drives[26 * 4 + 1] = {};
        DWORD len = GetLogicalDriveStringsW(static_cast<DWORD>(std::size(drives)), drives);

        for (wchar_t* p = drives; p < drives + len && *p; p += wcslen(p) + 1)
        {
            wchar_t driveRoot[3] = { p[0], L':', 0 };
            wchar_t target[MAX_PATH] = {};
            if (QueryDosDeviceW(driveRoot, target, static_cast<DWORD>(std::size(target))))
                map.emplace_back(target, p[0]);
        }

        return map;
    }

    std::wstring DevicePathToDosPath(const std::wstring& devicePath, const std::vector<std::pair<std::wstring, wchar_t>>& driveMap)
    {
        for (const auto& [devicePrefix, driveLetter] : driveMap)
        {
            if (devicePath.size() > devicePrefix.size() &&
                _wcsnicmp(devicePath.c_str(), devicePrefix.c_str(), devicePrefix.size()) == 0 &&
                devicePath[devicePrefix.size()] == L'\\')
            {
                return std::wstring(1, driveLetter) + L":" + devicePath.substr(devicePrefix.size());
            }
        }
        return L""; // No mapping found (e.g. a network path) - caller treats as unresolved.
    }

    std::wstring ToLowerCopy(const std::wstring& s)
    {
        std::wstring result = s;
        std::transform(result.begin(), result.end(), result.begin(), [](wchar_t c) { return std::towlower(c); });
        return result;
    }

    std::wstring StripExtendedPrefix(const std::wstring& path)
    {
        const std::wstring uncPrefix = LR"(\\?\UNC\)";
        const std::wstring extPrefix = LR"(\\?\)";

        if (path.compare(0, uncPrefix.size(), uncPrefix) == 0)
            return LR"(\\)" + path.substr(uncPrefix.size());
        if (path.compare(0, extPrefix.size(), extPrefix) == 0)
            return path.substr(extPrefix.size());
        return path;
    }

    // Opens the scan target once (read-attributes only, fully shared) so we
    // can compare candidate handles against the same canonical form that
    // GetFinalPathNameByHandleW produces - this correctly handles 8.3 names,
    // substituted drives, and symlinks. Falls back to the already-normalized
    // input path if the target cannot be opened for any reason.
    std::wstring ResolveCanonicalTargetPath(const std::wstring& normalizedPath)
    {
        HANDLE handle = CreateFileW(
            normalizedPath.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (handle == INVALID_HANDLE_VALUE)
        {
            handle = CreateFileW(
                normalizedPath.c_str(), FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        }

        if (handle == INVALID_HANDLE_VALUE)
            return StripExtendedPrefix(normalizedPath);

        wchar_t buffer[32770];
        DWORD len = GetFinalPathNameByHandleW(handle, buffer, static_cast<DWORD>(std::size(buffer)) - 1, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        CloseHandle(handle);

        if (len == 0 || len >= std::size(buffer))
            return StripExtendedPrefix(normalizedPath);

        return StripExtendedPrefix(std::wstring(buffer, len));
    }

    // Resolves every requested path to its canonical form and returns them
    // lowercased for O(1) matching against resolved handle paths. A folder
    // scan can mean thousands of targets, so a hash set (rather than a
    // linear scan per handle) keeps matching fast.
    std::unordered_set<std::wstring> BuildCanonicalTargetSet(const std::vector<std::wstring>& normalizedPaths)
    {
        std::unordered_set<std::wstring> targets;
        targets.reserve(normalizedPaths.size());
        for (const auto& path : normalizedPaths)
        {
            std::wstring canonical = ResolveCanonicalTargetPath(path);
            if (!canonical.empty())
                targets.insert(ToLowerCopy(canonical));
        }
        return targets;
    }

    std::vector<SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX_LOCAL> EnumerateAllHandles(std::vector<std::wstring>& errors)
    {
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        auto ntQuerySystemInformation = ntdll
            ? reinterpret_cast<NtQuerySystemInformationFn>(GetProcAddress(ntdll, "NtQuerySystemInformation"))
            : nullptr;

        if (!ntQuerySystemInformation)
        {
            errors.push_back(L"Advanced Handle Scan: NtQuerySystemInformation is not available on this system.");
            return {};
        }

        ULONG bufferSize = 1 << 20; // 1 MB starting point; grows if the system reports more is needed.
        std::vector<BYTE> buffer;

        for (int attempt = 0; attempt < 10; ++attempt)
        {
            buffer.assign(bufferSize, 0);
            ULONG returnLength = 0;
            NTSTATUS_LOCAL status = ntQuerySystemInformation(kSystemExtendedHandleInformation, buffer.data(), bufferSize, &returnLength);

            if (status == kStatusSuccess)
            {
                auto* info = reinterpret_cast<SYSTEM_HANDLE_INFORMATION_EX_LOCAL*>(buffer.data());
                return std::vector<SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX_LOCAL>(
                    info->Handles, info->Handles + info->NumberOfHandles);
            }

            if (status == kStatusInfoLengthMismatch)
            {
                bufferSize = returnLength > bufferSize ? returnLength + (1 << 16) : bufferSize * 2;
                continue;
            }

            std::wstringstream ss;
            ss << L"Advanced Handle Scan: NtQuerySystemInformation failed (status 0x"
               << std::hex << static_cast<unsigned long>(status) << L").";
            errors.push_back(ss.str());
            return {};
        }

        errors.push_back(L"Advanced Handle Scan: could not size the handle information buffer.");
        return {};
    }

    // Determines the ObjectTypeIndex value that corresponds to file/directory
    // handles on this running system, by opening a handle to a known path,
    // taking a fresh handle-table snapshot that necessarily includes it, and
    // reading back its ObjectTypeIndex. This lets the main scan skip every
    // non-file handle (threads, registry keys, events, sections, ALPC ports,
    // and so on - which vastly outnumber file handles on a typical system)
    // *before* ever duplicating it or spinning up a probe thread for it.
    // Without this, a full system-wide scan can burn its entire time budget
    // on bookkeeping handles alone and never reach the file handle that
    // actually matters - exactly what was observed on a real machine with a
    // large handle count. The type index isn't a stable, documented
    // constant (it varies by Windows build and even boot session), so it's
    // determined fresh here rather than hardcoded.
    bool TryGetObjectTypeIndexForPath(const std::wstring& path, DWORD flagsAndAttributes, USHORT& outTypeIndex)
    {
        HANDLE probe = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, flagsAndAttributes, nullptr);
        if (probe == INVALID_HANDLE_VALUE)
            return false;

        std::vector<std::wstring> localErrors; // Internal lookup detail - not surfaced to the caller.
        std::vector<SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX_LOCAL> handles = EnumerateAllHandles(localErrors);

        DWORD pid = GetCurrentProcessId();
        ULONG_PTR handleValue = reinterpret_cast<ULONG_PTR>(probe);
        bool found = false;

        for (const auto& entry : handles)
        {
            if (entry.UniqueProcessId == pid && entry.HandleValue == handleValue)
            {
                outTypeIndex = entry.ObjectTypeIndex;
                found = true;
                break;
            }
        }

        CloseHandle(probe);
        return found;
    }

    // Files and directories are believed to share the same NT object type
    // ("File") on all supported Windows versions, but that's exactly the
    // kind of undocumented-behaviour assumption that has already bitten this
    // scanner once - so rather than trust it, this probes with BOTH a file
    // handle (our own executable) and a directory handle (its containing
    // folder) and accepts a match against either resulting index. A
    // directory-held lock (e.g. a shell that `cd`'d into a folder) is
    // exactly the case this scanner needs to be able to find.
    std::vector<USHORT> DetermineFileObjectTypeIndices()
    {
        std::vector<USHORT> indices;

        wchar_t exePath[MAX_PATH];
        DWORD len = GetModuleFileNameW(nullptr, exePath, static_cast<DWORD>(std::size(exePath)));
        if (len == 0 || len >= std::size(exePath))
            return indices;

        USHORT fileIndex = 0;
        if (TryGetObjectTypeIndexForPath(exePath, FILE_ATTRIBUTE_NORMAL, fileIndex))
            indices.push_back(fileIndex);

        std::wstring dirPath(exePath);
        size_t slash = dirPath.find_last_of(L"\\/");
        if (slash != std::wstring::npos)
        {
            dirPath = dirPath.substr(0, slash);
            USHORT dirIndex = 0;
            if (TryGetObjectTypeIndexForPath(dirPath, FILE_FLAG_BACKUP_SEMANTICS, dirIndex))
            {
                if (std::find(indices.begin(), indices.end(), dirIndex) == indices.end())
                    indices.push_back(dirIndex);
            }
        }

        return indices;
    }

    // Resolves a duplicated handle to a comparable "C:\..." path, or an
    // empty string if it isn't a disk-based file/directory handle or its
    // path can't be determined. GetFinalPathNameByHandleW is tried first;
    // if it fails (which happens for handles opened with a limited access
    // mask - notably a process's current-directory tracking handle, e.g.
    // what cmd.exe holds after `cd`, exactly why Windows refuses to
    // rename/delete a folder a shell has `cd`'d into), NtQueryObject is used
    // instead, since it only needs a valid handle, not any particular
    // access rights - the same technique Sysinternals' handle.exe relies on
    // for cases like this.
    std::wstring ResolveHandlePath(HANDLE dupHandle, const std::vector<std::pair<std::wstring, wchar_t>>& driveMap)
    {
        DWORD fileType = GetFileType(dupHandle);
        if ((fileType & 0xFF) != FILE_TYPE_DISK)
            return L"";

        wchar_t buffer[32770];
        DWORD len = GetFinalPathNameByHandleW(dupHandle, buffer, static_cast<DWORD>(std::size(buffer)) - 1, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (len > 0 && len < std::size(buffer))
        {
            // GetFinalPathNameByHandleW (with VOLUME_NAME_DOS) returns the
            // \\?\C:\... extended-length form. ResolveCanonicalTargetPath
            // strips that same prefix before the target set is built, so
            // this must too - otherwise no candidate can ever match a
            // target, no matter how correctly everything else works.
            return StripExtendedPrefix(std::wstring(buffer, len));
        }

        auto ntQueryObject = GetNtQueryObject();
        if (!ntQueryObject)
            return L"";

        alignas(8) BYTE nameBuffer[8192];
        ULONG returnLength = 0;
        NTSTATUS_LOCAL status = ntQueryObject(dupHandle, kObjectNameInformation, nameBuffer, sizeof(nameBuffer), &returnLength);
        if (status != kStatusSuccess)
            return L"";

        auto* info = reinterpret_cast<OBJECT_NAME_INFORMATION_LOCAL*>(nameBuffer);
        if (!info->Name.Buffer || info->Name.Length == 0)
            return L"";

        std::wstring rawDevicePath(info->Name.Buffer, info->Name.Length / sizeof(wchar_t));
        return DevicePathToDosPath(rawDevicePath, driveMap);
    }

    // Everything ScanWorker needs, plus its output. Always heap-allocated
    // (see AdvancedHandleScanner::Scan) so that if the worker thread has to
    // be abandoned, it becomes a bounded leak rather than a dangling
    // pointer into a destroyed stack frame.
    struct ScanContext
    {
        std::vector<SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX_LOCAL> handles;
        std::unordered_set<std::wstring> canonicalTargets;
        std::vector<std::pair<std::wstring, wchar_t>> driveMap;
        std::vector<USHORT> fileTypeIndices;
        std::atomic<bool>* cancelRequested = nullptr;
        DWORD currentPid = 0;

        std::vector<LockingProcessInfo> results;   // Written only by the worker thread.
        std::vector<std::wstring> notes;           // Diagnostic notes generated during the scan.
    };

    // Runs the whole handle-table walk synchronously on one thread. Earlier
    // versions of this scanner spun up a separate thread *per candidate
    // handle* to enforce a per-handle timeout; on a real machine with over a
    // million system-wide handles, the thread-creation overhead alone (not
    // any actual hang) exhausted the entire scan's time budget before
    // reaching the handle that mattered. A synchronous walk of the same
    // (type-filtered) candidate set was measured at well under a second even
    // with 1.5M+ total handles, so the per-handle thread is gone; the
    // timeout safety net now wraps this whole function once instead (see
    // AdvancedHandleScanner::Scan).
    DWORD WINAPI ScanWorker(LPVOID param)
    {
        auto* ctx = reinterpret_cast<ScanContext*>(param);

        constexpr ULONGLONG kInternalBudgetMs = 15000; // Leaves headroom under Scan()'s outer wait timeout.
        const ULONGLONG startTick = GetTickCount64();

        std::map<DWORD, HANDLE> openedProcesses;
        std::map<DWORD, bool> processFailed;

        for (const auto& handleEntry : ctx->handles)
        {
            if (ctx->cancelRequested->load())
            {
                ctx->notes.push_back(L"Advanced Handle Scan was cancelled before completing; results may be incomplete.");
                break;
            }
            if (GetTickCount64() - startTick > kInternalBudgetMs)
            {
                ctx->notes.push_back(L"Advanced Handle Scan reached its time budget and stopped early; results may be incomplete.");
                break;
            }

            DWORD pid = static_cast<DWORD>(handleEntry.UniqueProcessId);
            if (pid == 0 || pid == 4 || pid == ctx->currentPid)
                continue; // System Idle Process, System, and ourselves.

            // Skip everything that isn't a file/directory handle before
            // paying for OpenProcess/DuplicateHandle - on a typical system,
            // non-file handles (threads, keys, events, sections, etc.)
            // outnumber file handles by a wide margin.
            if (!ctx->fileTypeIndices.empty() &&
                std::find(ctx->fileTypeIndices.begin(), ctx->fileTypeIndices.end(), handleEntry.ObjectTypeIndex) == ctx->fileTypeIndices.end())
                continue;

            HANDLE processHandle = nullptr;
            auto openedIt = openedProcesses.find(pid);
            if (openedIt != openedProcesses.end())
            {
                processHandle = openedIt->second;
            }
            else
            {
                if (processFailed.count(pid))
                    continue;

                // Minimum rights only - never PROCESS_ALL_ACCESS.
                processHandle = OpenProcess(PROCESS_DUP_HANDLE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
                if (!processHandle)
                {
                    processFailed[pid] = true;
                    continue; // Protected/system process - skip, not a scan failure.
                }
                openedProcesses[pid] = processHandle;
            }

            HANDLE sourceHandleValue = reinterpret_cast<HANDLE>(handleEntry.HandleValue);
            HANDLE dupHandle = nullptr;

            // DUPLICATE_SAME_ACCESS only, and never DUPLICATE_CLOSE_SOURCE:
            // the handle in the other process must never be closed by us.
            if (!DuplicateHandle(processHandle, sourceHandleValue, GetCurrentProcess(), &dupHandle, 0, FALSE, DUPLICATE_SAME_ACCESS))
                continue;

            std::wstring resolvedPath = ResolveHandlePath(dupHandle, ctx->driveMap);
            CloseHandle(dupHandle);

            if (resolvedPath.empty() || ctx->canonicalTargets.count(ToLowerCopy(resolvedPath)) == 0)
                continue;

            LockingProcessInfo entry;
            entry.processId = pid;
            entry.detectionSource = L"Advanced Handle Scan";
            entry.notes = L"Has an open handle to: " + resolvedPath;

            ProcessDetails details = ProcessApi::QueryProcessDetails(pid);
            if (details.resolved)
            {
                entry.processName = details.processName;
                entry.executablePath = details.executablePath;
            }
            else
            {
                entry.processName = L"(unknown)";
            }

            ctx->results.push_back(std::move(entry));
        }

        for (auto& kv : openedProcesses)
            CloseHandle(kv.second);

        return 0;
    }
}

std::vector<LockingProcessInfo> AdvancedHandleScanner::Scan(
    const std::vector<std::wstring>& normalizedPaths,
    std::atomic<bool>& cancelRequested,
    std::vector<std::wstring>& errors)
{
    std::vector<LockingProcessInfo> results;

    if (normalizedPaths.empty())
        return results; // Nothing to check (e.g. an empty folder) - not an error.

    std::unordered_set<std::wstring> canonicalTargets = BuildCanonicalTargetSet(normalizedPaths);
    if (canonicalTargets.empty())
    {
        errors.push_back(L"Advanced Handle Scan could not resolve a canonical path to compare against.");
        return results;
    }

    std::vector<SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX_LOCAL> handles = EnumerateAllHandles(errors);
    if (handles.empty())
        return results;

    auto* ctx = new ScanContext();
    ctx->handles = std::move(handles);
    ctx->canonicalTargets = std::move(canonicalTargets);
    ctx->driveMap = BuildDeviceDriveMap();
    ctx->fileTypeIndices = DetermineFileObjectTypeIndices();
    ctx->cancelRequested = &cancelRequested;
    ctx->currentPid = GetCurrentProcessId();

    HANDLE workerThread = CreateThread(nullptr, 0, ScanWorker, ctx, 0, nullptr);
    if (!workerThread)
    {
        delete ctx;
        errors.push_back(L"Advanced Handle Scan could not start its scanning thread.");
        return results;
    }

    // One watchdog for the whole scan, rather than one per handle (see
    // ScanWorker's comment for why). If the worker doesn't finish in time -
    // which in practice means one specific handle is blocked indefinitely
    // inside the kernel, most plausibly a synchronous named pipe waiting on
    // a peer that never responds - it is abandoned rather than terminated,
    // consistent with the rest of this scanner: killing a thread mid-syscall
    // can corrupt process state. Because the worker still owns `ctx` and may
    // still be writing to it, ctx cannot be read or freed in that case; it
    // is intentionally leaked, and this call yields no results rather than
    // risking a data race. Given the common case now completes in well
    // under a second even on a system with 1.5M+ total handles, this should
    // be a rare, bounded cost in practice.
    constexpr DWORD kOverallWaitMs = 20000;
    DWORD waitResult = WaitForSingleObject(workerThread, kOverallWaitMs);
    CloseHandle(workerThread);

    if (waitResult != WAIT_OBJECT_0)
    {
        errors.push_back(L"Advanced Handle Scan did not complete in time and was abandoned; results may be incomplete.");
        return results;
    }

    results = std::move(ctx->results);
    for (auto& note : ctx->notes)
        errors.push_back(note);
    delete ctx;

    return results;
}

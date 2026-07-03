#include "FileLockAnalyzer.h"
#include "PathNormalizer.h"
#include "../WindowsApi/NativeFileApi.h"
#include "../WindowsApi/RestartManager.h"
#include "../WindowsApi/AdvancedHandleScanner.h"

#include <windows.h>
#include <map>
#include <algorithm>
#include <cwctype>
#include <exception>

namespace
{
    std::wstring NarrowToWide(const char* s)
    {
        if (!s) return L"";
        std::wstring result;
        while (*s) result.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*s++)));
        return result;
    }

    std::wstring ToLower(const std::wstring& s)
    {
        std::wstring result = s;
        std::transform(result.begin(), result.end(), result.begin(), [](wchar_t c) { return std::towlower(c); });
        return result;
    }

    bool IsNetworkPath(const std::wstring& normalizedPath)
    {
        std::wstring path = normalizedPath;
        const std::wstring extPrefix = LR"(\\?\)";
        if (path.compare(0, extPrefix.size(), extPrefix) == 0)
            path = path.substr(extPrefix.size());

        if (path.compare(0, 4, LR"(UNC\)") == 0)
            return true;
        if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\')
            return true;

        if (path.size() >= 3)
        {
            std::wstring root = path.substr(0, 3); // e.g. "C:\"
            UINT driveType = GetDriveTypeW(root.c_str());
            return driveType == DRIVE_REMOTE;
        }
        return false;
    }

    void MergeProcess(std::vector<LockingProcessInfo>& into, const LockingProcessInfo& candidate)
    {
        std::wstring key = ToLower(!candidate.executablePath.empty() ? candidate.executablePath : candidate.processName);

        for (auto& existing : into)
        {
            std::wstring existingKey = ToLower(!existing.executablePath.empty() ? existing.executablePath : existing.processName);
            if (existing.processId == candidate.processId && existingKey == key)
            {
                if (existing.detectionSource.find(candidate.detectionSource) == std::wstring::npos)
                {
                    existing.detectionSource += L", " + candidate.detectionSource;
                }
                if (!candidate.notes.empty() && existing.notes.find(candidate.notes) == std::wstring::npos)
                {
                    if (!existing.notes.empty()) existing.notes += L" ";
                    existing.notes += candidate.notes;
                }
                if (existing.executablePath.empty() && !candidate.executablePath.empty())
                    existing.executablePath = candidate.executablePath;
                return;
            }
        }

        into.push_back(candidate);
    }

    constexpr size_t kMaxFolderScanTargets = 2000;
    constexpr ULONGLONG kFolderEnumerationBudgetMs = 5000;

    std::wstring JoinPath(const std::wstring& dir, const wchar_t* name)
    {
        std::wstring result = dir;
        if (!result.empty() && result.back() != L'\\')
            result += L'\\';
        result += name;
        return result;
    }

    // A folder usually can't be renamed/deleted because a file *inside* it
    // (at any depth) is locked, not because the folder's own directory
    // handle is - opening a directory handle rarely conflicts with anything.
    // So for a directory, the scan target list is every FILE inside it
    // (at any depth), capped and time-boxed so an accidental scan of
    // something huge (e.g. C:\Windows) doesn't run away.
    //
    // Deliberately files only, never directory paths (not even the root
    // folder itself): Restart Manager expects file paths, and registering a
    // directory alongside real files in the same RmRegisterResources batch
    // can fail that whole batch - silently hiding every genuinely locked
    // file that happened to share a batch with a directory entry. Folders
    // are only ever used here to recurse into, never added as a target.
    //
    // This is a hand-rolled FindFirstFileW/FindNextFileW stack walk rather
    // than std::filesystem::recursive_directory_iterator on purpose: the
    // latter aborts the *entire remaining* walk the moment any single entry
    // can't be enumerated (a permission-denied file, a OneDrive cloud
    // placeholder hiccup, a broken junction, etc.), which silently truncated
    // real-world folder scans and made the actual locked file invisible.
    // Here, a directory that can't be opened is simply skipped and every
    // other branch is still visited.
    std::vector<std::wstring> BuildScanTargets(const std::wstring& selectedPath, bool isDirectory, std::vector<std::wstring>& errors)
    {
        std::vector<std::wstring> targets;

        if (!isDirectory)
        {
            targets.push_back(selectedPath);
            return targets;
        }

        ULONGLONG start = GetTickCount64();
        bool truncated = false;
        bool anyDirectorySkipped = false;

        std::vector<std::wstring> pendingDirs;
        pendingDirs.push_back(selectedPath);

        while (!pendingDirs.empty())
        {
            if (targets.size() >= kMaxFolderScanTargets || GetTickCount64() - start > kFolderEnumerationBudgetMs)
            {
                truncated = true;
                break;
            }

            std::wstring dir = std::move(pendingDirs.back());
            pendingDirs.pop_back();

            std::wstring searchPattern = JoinPath(dir, L"*");

            WIN32_FIND_DATAW findData{};
            HANDLE findHandle = FindFirstFileW(searchPattern.c_str(), &findData);
            if (findHandle == INVALID_HANDLE_VALUE)
            {
                // Can't enumerate this one directory (permissions, a cloud
                // placeholder that failed to hydrate, etc.) - skip just this
                // branch and keep going with everything else already queued.
                anyDirectorySkipped = true;
                continue;
            }

            do
            {
                if (targets.size() >= kMaxFolderScanTargets)
                {
                    truncated = true;
                    break;
                }

                const wchar_t* name = findData.cFileName;
                if (wcscmp(name, L".") == 0 || wcscmp(name, L"..") == 0)
                    continue;

                std::wstring fullPath = JoinPath(dir, name);

                bool isDir = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                bool isReparsePoint = (findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

                if (isDir)
                {
                    if (!isReparsePoint) // Don't follow junctions/symlinks - avoids possible cycles.
                        pendingDirs.push_back(fullPath);
                }
                else
                {
                    targets.push_back(fullPath); // Files only - see comment above BuildScanTargets.
                }

            } while (FindNextFileW(findHandle, &findData));

            FindClose(findHandle);
        }

        if (truncated)
        {
            errors.push_back(L"This folder contains many items; only the first " +
                std::to_wstring(kMaxFolderScanTargets) + L" were checked for locks.");
        }
        if (anyDirectorySkipped)
        {
            errors.push_back(L"Some subfolders could not be read (permissions or a sync placeholder) and were skipped.");
        }

        return targets;
    }
}

LockResult FileLockAnalyzer::Scan(const std::wstring& rawPath, bool useAdvancedScan, std::atomic<bool>* cancelRequested)
{
    LockResult result;
    result.timestamp = std::time(nullptr);

    std::wstring trimmed = PathNormalizer::TrimAndUnquote(rawPath);
    if (trimmed.empty())
    {
        result.status = LockStatus::NoPathSelected;
        result.statusMessage = ToStatusMessage(result.status, false);
        return result;
    }

    std::wstring normalized = PathNormalizer::Normalize(rawPath);
    result.selectedPath = normalized.empty() ? trimmed : normalized;

    DWORD attrs = GetFileAttributesW(result.selectedPath.c_str());
    result.exists = attrs != INVALID_FILE_ATTRIBUTES;
    result.isDirectory = result.exists
        ? (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0
        : PathNormalizer::LooksLikeDirectoryPath(result.selectedPath);

    if (!result.exists)
    {
        result.status = LockStatus::PathDoesNotExist;
        result.statusMessage = ToStatusMessage(result.status, result.isDirectory);
        result.advice = L"Check that the path is spelled correctly and still exists.";
        return result;
    }

    static std::atomic<bool> noCancel{false};
    std::atomic<bool>& cancelFlag = cancelRequested ? *cancelRequested : noCancel;

    try
    {
        FileProbeResult probe = NativeFileApi::ProbeLock(result.selectedPath, result.isDirectory);

        std::vector<std::wstring> scanTargets = BuildScanTargets(result.selectedPath, result.isDirectory, result.errors);

        std::vector<LockingProcessInfo> combined;

        std::vector<LockingProcessInfo> rmResults = RestartManager::Scan(scanTargets, result.errors);
        result.wasRestartManagerUsed = true;
        for (auto& p : rmResults)
        {
            // Restart Manager reports processes affected by any registered
            // resource but not which one - for a folder that means "some
            // file inside," not necessarily the folder itself.
            if (result.isDirectory && p.notes.empty())
                p.notes = L"Detected via a file somewhere inside this folder.";
            MergeProcess(combined, p);
        }

        // A folder can also be locked via its own path directly - most
        // commonly because another process has it as its current working
        // directory (e.g. a cmd.exe window that `cd`'d into it), which
        // Windows enforces by opening the directory without
        // FILE_SHARE_DELETE. This is registered as its own isolated Restart
        // Manager call, deliberately never mixed into the same batch as the
        // file paths above - registering a directory alongside files in one
        // RmRegisterResources call was found to silently fail that whole
        // batch (see BuildScanTargets), hiding genuinely locked files.
        if (result.isDirectory)
        {
            std::vector<LockingProcessInfo> folderRmResults = RestartManager::Scan({ result.selectedPath }, result.errors);
            for (auto& p : folderRmResults)
            {
                if (p.notes.empty())
                    p.notes = L"May have this folder open directly (e.g. as its current directory) rather than a file inside it.";
                MergeProcess(combined, p);
            }
        }

        // Folders always get the deeper scan, even if the caller didn't ask
        // for it: the folder-level probe above is essentially uninformative
        // (opening a directory handle almost never conflicts, even when a
        // file inside it is genuinely locked), and Restart Manager alone has
        // real-world blind spots for some applications (notably some Office
        // document-locking scenarios) - exactly the "folder is open in Word
        // but nothing was found" case this exists to catch. Files still
        // respect the flag/preference since the probe already gives a
        // direct, reliable signal for a single file.
        if (useAdvancedScan || result.isDirectory)
        {
            // Unlike Restart Manager, handle enumeration matches by resolved
            // path directly rather than a registered-resource batch, so it's
            // safe to check the folder's own path alongside its files here.
            std::vector<std::wstring> advancedTargets = scanTargets;
            if (result.isDirectory)
                advancedTargets.push_back(result.selectedPath);

            std::vector<LockingProcessInfo> advResults = AdvancedHandleScanner::Scan(advancedTargets, cancelFlag, result.errors);
            result.wasAdvancedScanUsed = true;
            for (auto& p : advResults)
                MergeProcess(combined, p);
        }

        result.processes = std::move(combined);

        if (probe.accessDenied)
            result.status = LockStatus::AccessDenied;
        else if (probe.likelyLocked || !result.processes.empty())
            result.status = LockStatus::LikelyLocked;
        else if (probe.couldOpen)
            result.status = LockStatus::NotLocked;
        else
            result.status = LockStatus::Unknown;

        result.statusMessage = ToStatusMessage(result.status, result.isDirectory);

        std::vector<std::wstring> tips;
        if (!result.processes.empty())
            tips.push_back(L"Close the listed application and click Refresh.");
        if (result.status == LockStatus::LikelyLocked && result.processes.empty())
            tips.push_back(L"No process was identified. Try Advanced Scan or run as administrator.");
        if (result.status == LockStatus::AccessDenied)
            tips.push_back(L"This looks like a permissions issue rather than a file lock.");
        if ((result.status == LockStatus::LikelyLocked || result.status == LockStatus::Unknown) && IsNetworkPath(result.selectedPath))
            tips.push_back(L"This may be a network share lock, which may not be visible locally.");
        if (result.status == LockStatus::LikelyLocked && result.processes.empty() && result.wasAdvancedScanUsed)
            tips.push_back(L"This may be caused by a loaded DLL or memory-mapped file.");

        std::wstring advice;
        for (size_t i = 0; i < tips.size(); ++i)
        {
            if (i > 0) advice += L"\r\n";
            advice += tips[i];
        }
        result.advice = advice;

        result.requiresElevationRecommended =
            result.status == LockStatus::AccessDenied ||
            (result.status == LockStatus::LikelyLocked && result.processes.empty());
    }
    catch (const std::exception& ex)
    {
        result.status = LockStatus::ScanError;
        result.statusMessage = ToStatusMessage(result.status, result.isDirectory);
        result.advice = L"An unexpected error occurred while scanning.";
        result.errors.push_back(NarrowToWide(ex.what()));
    }
    catch (...)
    {
        result.status = LockStatus::ScanError;
        result.statusMessage = ToStatusMessage(result.status, result.isDirectory);
        result.advice = L"An unexpected error occurred while scanning.";
        result.errors.push_back(L"An unknown error occurred.");
    }

    return result;
}

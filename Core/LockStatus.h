#pragma once

// LockStatus describes the overall outcome of a scan of a single file or
// directory. It is intentionally a small, closed set of states so the UI
// can map each one to a single user-facing sentence.
enum class LockStatus
{
    NoPathSelected,
    PathDoesNotExist,
    NotLocked,
    LikelyLocked,
    AccessDenied,
    Unknown,
    ScanError
};

inline const wchar_t* ToStatusMessage(LockStatus status, bool isDirectory)
{
    switch (status)
    {
        case LockStatus::NoPathSelected:   return L"No file selected";
        case LockStatus::PathDoesNotExist: return isDirectory ? L"Folder does not exist" : L"File does not exist";
        case LockStatus::NotLocked:        return L"File is not currently locked for delete/rename";
        case LockStatus::LikelyLocked:     return isDirectory ? L"Folder appears locked" : L"File appears locked";
        case LockStatus::AccessDenied:     return L"Access denied / try running as administrator";
        case LockStatus::Unknown:          return L"No locking processes were identified";
        case LockStatus::ScanError:        return L"Error while scanning";
    }
    return L"Unknown status";
}

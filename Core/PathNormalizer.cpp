#include "PathNormalizer.h"

#include <windows.h>
#include <vector>
#include <cwctype>

namespace
{
    constexpr size_t kLongPathThreshold = MAX_PATH - 12; // Leave room for drive/prefix.

    bool StartsWith(const std::wstring& s, const wchar_t* prefix)
    {
        size_t len = wcslen(prefix);
        return s.size() >= len && s.compare(0, len, prefix) == 0;
    }
}

std::wstring PathNormalizer::TrimAndUnquote(const std::wstring& raw)
{
    std::wstring s = raw;

    size_t start = s.find_first_not_of(L" \t\r\n");
    size_t end = s.find_last_not_of(L" \t\r\n");
    if (start == std::wstring::npos)
        return L"";
    s = s.substr(start, end - start + 1);

    if (s.size() >= 2 && s.front() == L'"' && s.back() == L'"')
        s = s.substr(1, s.size() - 2);

    // Trim again in case there was whitespace inside the quotes.
    start = s.find_first_not_of(L" \t\r\n");
    end = s.find_last_not_of(L" \t\r\n");
    if (start == std::wstring::npos)
        return L"";
    return s.substr(start, end - start + 1);
}

std::wstring PathNormalizer::Normalize(const std::wstring& raw)
{
    std::wstring trimmed = TrimAndUnquote(raw);
    if (trimmed.empty())
        return L"";

    // Already in extended-length form: leave it alone.
    if (StartsWith(trimmed, LR"(\\?\)"))
        return trimmed;

    DWORD needed = GetFullPathNameW(trimmed.c_str(), 0, nullptr, nullptr);
    if (needed == 0)
        return trimmed; // Fall back to the trimmed input; caller will report "does not exist".

    std::vector<wchar_t> buffer(needed);
    DWORD written = GetFullPathNameW(trimmed.c_str(), needed, buffer.data(), nullptr);
    if (written == 0 || written >= needed)
        return trimmed;

    std::wstring full(buffer.data(), written);

    // Support long paths where practical by opting into the \\?\ prefix
    // once the path is close to MAX_PATH. This is safe for CreateFileW and
    // friends; we only do it for long paths to keep normal paths readable
    // in the UI and in registry/context-menu commands.
    if (full.size() >= kLongPathThreshold && !StartsWith(full, LR"(\\?\)"))
    {
        if (StartsWith(full, LR"(\\)"))
            full = LR"(\\?\UNC\)" + full.substr(2);
        else
            full = LR"(\\?\)" + full;
    }

    return full;
}

bool PathNormalizer::LooksLikeDirectoryPath(const std::wstring& normalizedPath)
{
    if (normalizedPath.empty())
        return false;

    wchar_t last = normalizedPath.back();
    if (last == L'\\' || last == L'/')
        return true;

    size_t slash = normalizedPath.find_last_of(L"\\/");
    std::wstring lastComponent = (slash == std::wstring::npos)
        ? normalizedPath
        : normalizedPath.substr(slash + 1);

    // No dot in the final component (and it isn't just a drive letter) reads
    // as "folder" more often than "file" in practice.
    return lastComponent.find(L'.') == std::wstring::npos;
}

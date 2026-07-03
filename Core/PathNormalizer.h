#pragma once

#include <string>

// Turns raw user/command-line/drag-drop input into a clean, absolute path
// suitable for the Win32 file APIs.
namespace PathNormalizer
{
    // Trims whitespace and strips a single pair of surrounding quotes, e.g.
    // from a path pasted or passed on the command line as `"C:\My File.txt"`.
    std::wstring TrimAndUnquote(const std::wstring& raw);

    // Expands to a full, absolute path via GetFullPathNameW and, for very
    // long paths, prefixes with \\?\ (or \\?\UNC\ for UNC paths) so long
    // path scenarios work without requiring the caller to opt in manually.
    std::wstring Normalize(const std::wstring& raw);

    // Heuristic used only when a path does not exist, so we can still show
    // "File does not exist" vs "Folder does not exist": a trailing
    // separator or an extension-less final component is treated as a folder.
    bool LooksLikeDirectoryPath(const std::wstring& normalizedPath);
}

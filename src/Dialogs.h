#pragma once

#include <windows.h>
#include <string>

// Wraps the modern common item dialog (IFileOpenDialog) for both the "Open
// File..." and "Open Folder..." menu actions - this is the current
// Windows-native replacement for the legacy OpenFileDialog/FolderBrowserDialog
// pickers, supporting both file and folder selection through the same API.
namespace Dialogs
{
    bool ShowOpenFileDialog(HWND owner, std::wstring& outPath);
    bool ShowOpenFolderDialog(HWND owner, std::wstring& outPath);
}

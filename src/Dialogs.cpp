#include "Dialogs.h"

#include <shobjidl.h>

#pragma comment(lib, "Ole32.lib")

namespace
{
    bool ShowFileSystemDialog(HWND owner, bool pickFolders, std::wstring& outPath)
    {
        IFileOpenDialog* dialog = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
        if (FAILED(hr) || !dialog)
            return false;

        DWORD options = 0;
        dialog->GetOptions(&options);
        options |= FOS_FORCEFILESYSTEM;
        if (pickFolders)
            options |= FOS_PICKFOLDERS;
        dialog->SetOptions(options);
        dialog->SetTitle(pickFolders ? L"Select a folder to scan" : L"Select a file to scan");

        hr = dialog->Show(owner);
        if (FAILED(hr))
        {
            dialog->Release();
            return false; // Includes the user cancelling.
        }

        IShellItem* item = nullptr;
        hr = dialog->GetResult(&item);
        if (FAILED(hr) || !item)
        {
            dialog->Release();
            return false;
        }

        PWSTR path = nullptr;
        hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
        bool ok = false;
        if (SUCCEEDED(hr) && path)
        {
            outPath = path;
            CoTaskMemFree(path);
            ok = true;
        }

        item->Release();
        dialog->Release();
        return ok;
    }
}

bool Dialogs::ShowOpenFileDialog(HWND owner, std::wstring& outPath)
{
    return ShowFileSystemDialog(owner, false, outPath);
}

bool Dialogs::ShowOpenFolderDialog(HWND owner, std::wstring& outPath)
{
    return ShowFileSystemDialog(owner, true, outPath);
}

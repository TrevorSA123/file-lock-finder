#pragma once

#include <windows.h>
#include <shellapi.h>
#include <string>
#include <atomic>
#include <memory>
#include "../Core/AppSettings.h"
#include "../Core/LockResult.h"

// Main application window. Owns all Win32 UI objects; business logic
// (scanning, settings, context-menu registration) lives in Core/ and
// WindowsApi/ and is only ever called from here, never reimplemented here.
class MainWindow
{
public:
    static ATOM RegisterWindowClass(HINSTANCE hInstance);
    static MainWindow* Create(HINSTANCE hInstance, int nCmdShow, const std::wstring& initialPath);

    HWND Handle() const { return m_hwnd; }

    // Called by the drop-panel child window's WndProc.
    void OnDropFiles(HDROP hDrop);

private:
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void OnCreate(HWND hwnd, HINSTANCE hInstance);
    void OnSize(int width, int height);
    void OnCommand(int id);

    void BuildMenu();
    void CreateControls(HINSTANCE hInstance);
    void LayoutControls(int width, int height);

    void SetSelectedPath(const std::wstring& path, bool forceScan);
    void StartScan(bool advanced);
    void OnScanComplete(int generation, LockResult* result);
    void ApplyResult(const LockResult& result);
    void UpdateElevationUi();
    void UpdateStatusBar(const std::wstring& text);
    void SetControlsEnabled(bool enabled);

    void ActionOpenFile();
    void ActionOpenFolder();
    void ActionRefresh();
    void ActionAdvancedScan();
    void ActionOpenFileLocation();
    void ActionCopyResults();
    void ActionRunAsAdministrator();
    void ActionPreferences();
    void ActionScheduleDeleteOnReboot();
    void ActionScheduleRenameOnReboot();
    void ActionAbout();

    void CheckContextMenuIntegrationAtStartup();

    HWND m_hwnd = nullptr;
    HWND m_dropPanel = nullptr;
    HWND m_pathLabel = nullptr;
    HWND m_statusLabel = nullptr;
    HWND m_adviceLabel = nullptr;
    HWND m_btnBrowseFile = nullptr;
    HWND m_btnBrowseFolder = nullptr;
    HWND m_btnRefresh = nullptr;
    HWND m_btnAdvancedScan = nullptr;
    HWND m_btnRunAsAdmin = nullptr;
    HWND m_listView = nullptr;
    HWND m_statusBar = nullptr;
    HMENU m_menu = nullptr;

    AppSettings m_settings;
    std::wstring m_currentPath;
    LockResult m_lastResult;
    int m_scanGeneration = 0;
    std::shared_ptr<std::atomic<bool>> m_activeCancelToken;
    bool m_isElevated = false;
};

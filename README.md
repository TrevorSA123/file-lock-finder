# File Lock Finder

A small Windows utility for finding out what is locking a file or folder.

This is for the familiar Windows problem where you try to delete, rename, or move something and get the classic "this file is being used by another process" message, but Windows does not tell you which process is responsible.

## What it does

File Lock Finder lets you pick a file or folder and tries to show which running processes are using it.

It is intentionally small, native, and fairly old-school:

* Written in C++17/20.
* Pure Win32 API.
* No .NET.
* No WinForms, WPF, WinUI, XAML, or designer-generated UI.
* UI is built directly in code using normal Win32 controls.
* Native and lower-level Windows API calls are isolated in the `WindowsApi/` folder so that the UI and core logic stay reasonably clean.

This started as a practical utility for the kind of thing I often want on a Windows machine: a quick way to answer "what is holding this file open?" without installing a large tool.

## Project layout

```text
FileLockFinder/
  CMakeLists.txt
  FileLockFinder.manifest        # asInvoker execution level, DPI awareness, common-controls v6

  src/                           # Win32 UI layer
    main.cpp                     # wWinMain, command-line parsing, message loop
    MainWindow.h/.cpp            # main application window
    QuickScanDialog.h/.cpp       # compact "just show me the processes" popup
    PreferencesDialog.h/.cpp     # Options > Preferences...
    AboutDialog.h/.cpp           # Help > About
    Dialogs.h/.cpp               # IFileOpenDialog wrappers
    ResultsListView.h/.cpp       # shared results grid setup/population
    ResultsFormatting.h/.cpp     # copy-results-to-clipboard formatting
    UiHelpers.h/.cpp             # small CreateWindowEx/control helpers
    Resource.h                   # control/menu command IDs

  Core/                          # business logic, no Win32 UI code
    FileLockAnalyzer.h/.cpp      # coordinates a scan; main entry point used by the UI
    LockResult.h
    LockingProcessInfo.h
    LockStatus.h
    PathNormalizer.h/.cpp
    AppSettings.h
    SettingsService.h/.cpp       # JSON settings load/save

  WindowsApi/                    # Win32/native API boundary
    NativeFileApi.h/.cpp         # CreateFile-based non-destructive lock probe
    ProcessApi.h/.cpp            # least-privilege process name/path lookup
    RestartManager.h/.cpp        # primary detection method using Rstrtmgr.h
    AdvancedHandleScanner.h/.cpp # optional fallback using raw handle enumeration
    MoveFileExApi.h/.cpp         # schedule delete/rename on reboot
    ContextMenuIntegrationService.h/.cpp  # per-user Explorer context-menu registration
    Win32Handle.h                # small RAII HANDLE wrapper
```

## Building

You need Visual Studio 2022, or the standalone MSVC Build Tools, with the **Desktop development with C++** workload installed.

You also need CMake. Visual Studio normally includes its own copy under:

```text
Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin
```

From a Developer Command Prompt or Developer PowerShell for VS 2022:

```powershell
cd FileLockFinder
cmake -G "Visual Studio 17 2022" -A x64 -B build
cmake --build build --config Release
```

The executable is created here:

```text
build\bin\FileLockFinder.exe
```

The project is also set up to build cleanly with `/W4` and zero warnings.

## Running

```powershell
FileLockFinder.exe
```

Opens the main window. You can drag and drop a file, or use **File > Open**.

```powershell
FileLockFinder.exe "C:\path\to\locked file.txt"
```

Opens the app and immediately scans that path.

Quoted paths with spaces and Unicode paths are handled properly. The command line is parsed with `CommandLineToArgvW`, and paths are also normalised by `PathNormalizer`.

## Compact popup mode

There is an optional preference to show a smaller popup when the app is launched with a path.

This is useful for Explorer integration, because you often just want the answer immediately:

* selected path
* lock status
* list of locking processes
* button to open the full window

The full window is still available for the extra actions, such as Advanced Scan, Run as Administrator, and scheduling delete/rename on reboot.

## Explorer integration

In **Options > Preferences**, you can enable:

```text
Add "Find locking processes" to Explorer context menu
```

This adds a right-click option for files, and optionally folders.

The integration is per-user only. It writes under:

```text
HKCU\Software\Classes
```

So it does not need administrator rights and does not install anything system-wide.

On Windows 11, the entry may appear under **Show more options**, which is the classic context menu. A native top-level Windows 11 context-menu entry would need a packaged `IExplorerCommand` shell extension, which is deliberately out of scope for this small utility.

## How detection works

The main scan starts in:

```text
Core/FileLockAnalyzer.cpp
```

`FileLockAnalyzer::Scan` runs on a background thread and combines a few different signals. It tries to return useful partial results even if one method fails.

### 1. Non-destructive file probe

Implemented in:

```text
WindowsApi/NativeFileApi.cpp
```

The tool tries to open the file or folder with `CreateFile`, requesting `DELETE` access and full sharing:

```cpp
FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE
```

It then immediately closes the handle.

Nothing is deleted, renamed, or modified.

The result is used as a signal:

* `ERROR_SHARING_VIOLATION` usually means something has the file open in a way that blocks the operation.
* `ERROR_ACCESS_DENIED` is treated separately, because that is often a permissions, attribute, or file-system issue rather than a normal lock.

### 2. Restart Manager

Implemented in:

```text
WindowsApi/RestartManager.cpp
```

This is the primary detection method.

It uses the documented Windows Restart Manager API, which is also used by things like Windows Update and MSI installers to work out which processes are using a file.

The flow is:

```text
RmStartSession
RmRegisterResources
RmGetList
RmEndSession
```

Cleanup is guarded with RAII so that the Restart Manager session is ended properly.

Restart Manager is preferred because it is a documented and supported Windows API, unlike raw system handle enumeration.

### 3. Advanced Handle Scan

Implemented in:

```text
WindowsApi/AdvancedHandleScanner.cpp
```

This is optional and opt-in.

It can be run manually from the UI, enabled from the menu, or enabled by default in Preferences.

This method enumerates open handles on the system using the undocumented native API:

```text
NtQuerySystemInformation(SystemExtendedHandleInformation, ...)
```

For each candidate process it opens the process with minimum useful rights only:

```text
PROCESS_DUP_HANDLE | PROCESS_QUERY_LIMITED_INFORMATION
```

It never requests `PROCESS_ALL_ACCESS`.

Candidate handles are duplicated into this process using `DuplicateHandle`, but the source handle is never closed. The tool does not force-unlock anything.

File handles are resolved using:

```text
GetFileType
GetFinalPathNameByHandleW
```

There is one awkward Windows detail here: probing some handle types can block indefinitely, especially synchronous named pipes. To avoid the whole scan hanging, each risky handle probe runs on its own short-lived thread with a timeout.

If a probe does not return in time, that handle is abandoned. The thread is not forcibly killed, because terminating a thread in the middle of a system call can corrupt process state.

This is a rare and bounded tradeoff, but it is still a tradeoff. The code comments call this out clearly.

Because this method depends on internal Windows behaviour, it is treated as best-effort throughout the app.

## Results

Results from Restart Manager and Advanced Handle Scan are de-duplicated by:

```text
PID + executable path
```

The UI also shows a **Detection Source** column, so it is clear whether a process was found by Restart Manager, Advanced Scan, or both.

The app also tries to provide useful advice when the result is not clear-cut. For example:

* the file may be on a network share and the lock may not be visible locally;
* elevation may be needed to inspect protected processes;
* Advanced Scan may be worth trying if the basic scan detects a lock but cannot identify the owner;
* access denied may be a permissions or file attribute issue, not a lock.

## Safety

This tool is deliberately conservative.

It does not try to be a "force unlocker".

* It never kills processes.
* It never force-closes handles in another process.
* It never uses `DUPLICATE_CLOSE_SOURCE`.
* It never requests `PROCESS_ALL_ACCESS`.
* Normal use does not require administrator rights.

The **Schedule Delete/Rename on Reboot** feature only calls:

```text
MoveFileExW(..., MOVEFILE_DELAY_UNTIL_REBOOT)
```

That means nothing happens immediately. The operation is only scheduled for the next restart.

Scheduling always requires an explicit confirmation dialog. There is also an optional stronger confirmation setting in Preferences.

The Explorer context-menu registration only touches these per-user keys:

```text
HKCU\Software\Classes\*\shell\FileLockFinder
HKCU\Software\Classes\Directory\shell\FileLockFinder
```

and their `command` subkeys.

The registration is idempotent, can be removed again from Preferences, and does not touch any other registry location.

**Run as Administrator** is also explicit. It relaunches the app with `ShellExecuteEx` and the `runas` verb. The app does not silently elevate itself.

## Known limitations

This kind of tool can help a lot, but Windows file locking is not always simple.

Known limitations:

* Some protected or system processes cannot be inspected without elevation.
* A file may be locked because it is memory-mapped or loaded as a DLL, not because there is a normal open file handle.
* Network share locks may not be visible from the local machine.
* Permissions, read-only attributes, very long paths, and file-system corruption can look similar to a lock from the user's point of view.
* The Advanced Handle Scan relies on undocumented Windows internals, so it is best-effort by design.
* Restart Manager remains the primary and preferred detection method.

## Design notes

The main design goal was to keep the utility small, understandable, and safe.

I wanted something that feels like a normal native Windows tool, but without hiding all the interesting parts behind frameworks or generated UI code.

The code is split so that:

* `src/` contains the Win32 UI;
* `Core/` contains the application logic;
* `WindowsApi/` contains the native Windows API boundary.

That makes it easier to reason about the risky parts of the program, especially anything involving native handles, undocumented APIs, registry writes, or reboot scheduling.

The end result is not meant to replace tools like Process Explorer. It is more of a focused utility for one common annoyance: finding out what is stopping you from deleting, renaming, or moving a file.

## License

MIT License. See [LICENSE](LICENSE) for details.

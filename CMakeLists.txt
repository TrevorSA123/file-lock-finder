cmake_minimum_required(VERSION 3.20)
project(FileLockFinder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin)

if(NOT MSVC)
    message(FATAL_ERROR "FileLockFinder targets MSVC (cl.exe). Configure with -G \"Visual Studio 17 2022\" or use the MSVC toolset.")
endif()

add_compile_options(/W4 /permissive- /utf-8)
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

add_executable(FileLockFinder WIN32
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/AboutDialog.cpp
    src/AboutDialog.h
    src/PreferencesDialog.cpp
    src/PreferencesDialog.h
    src/Dialogs.cpp
    src/Dialogs.h
    src/UiHelpers.cpp
    src/UiHelpers.h
    src/QuickScanDialog.cpp
    src/QuickScanDialog.h
    src/ResultsListView.cpp
    src/ResultsListView.h
    src/ResultsFormatting.cpp
    src/ResultsFormatting.h
    src/Resource.h

    Core/PathNormalizer.cpp
    Core/PathNormalizer.h
    Core/SettingsService.cpp
    Core/SettingsService.h
    Core/FileLockAnalyzer.cpp
    Core/FileLockAnalyzer.h
    Core/AppSettings.h
    Core/LockResult.h
    Core/LockingProcessInfo.h
    Core/LockStatus.h

    WindowsApi/NativeFileApi.cpp
    WindowsApi/NativeFileApi.h
    WindowsApi/ProcessApi.cpp
    WindowsApi/ProcessApi.h
    WindowsApi/RestartManager.cpp
    WindowsApi/RestartManager.h
    WindowsApi/AdvancedHandleScanner.cpp
    WindowsApi/AdvancedHandleScanner.h
    WindowsApi/MoveFileExApi.cpp
    WindowsApi/MoveFileExApi.h
    WindowsApi/ContextMenuIntegrationService.cpp
    WindowsApi/ContextMenuIntegrationService.h
    WindowsApi/Win32Handle.h

    FileLockFinder.manifest
)

target_include_directories(FileLockFinder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(FileLockFinder PRIVATE
    Rstrtmgr.lib
    Shell32.lib
    Shlwapi.lib
    Ole32.lib
    Comctl32.lib
    User32.lib
    Gdi32.lib
    Advapi32.lib
)

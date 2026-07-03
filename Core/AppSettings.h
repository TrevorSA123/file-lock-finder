#pragma once

// Persisted user preferences. Kept as a flat set of booleans so it can be
// serialized to a tiny hand-written JSON document without pulling in a
// third-party JSON library for seven fields.
struct AppSettings
{
    bool addToContextMenu = false;
    bool includeFoldersInContextMenu = false;
    bool autoScanOnOpen = true;
    bool enableAdvancedScanByDefault = false;
    bool confirmBeforeRebootActions = true;
    bool checkContextMenuIntegrationAtStartup = true;

    // When true, launching the app with a path on the command line (as the
    // context-menu verb always does) shows a small standalone results popup
    // instead of the full main window - for users who just want to see who
    // is locking a file without the rest of the UI. The main window is still
    // used for every other entry point (drag/drop, Open File/Folder, etc.),
    // and the popup itself offers an "Open Full Window" escape hatch.
    bool useCompactPopupFromContextMenu = false;
};

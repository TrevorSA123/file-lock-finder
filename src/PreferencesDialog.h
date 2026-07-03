#pragma once

#include <windows.h>
#include "../Core/AppSettings.h"

namespace PreferencesDialog
{
    // Modal-style preferences window, built entirely in code (no resource
    // template, no designer). Returns true if the user saved changes via
    // OK or Apply at least once; `settings` is updated in place to reflect
    // the last saved state so the caller can react immediately.
    bool Show(HWND owner, AppSettings& settings);
}

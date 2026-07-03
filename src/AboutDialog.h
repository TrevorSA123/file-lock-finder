#pragma once

#include <windows.h>

namespace AboutDialog
{
    // Blocking (modal-style) show: disables owner, runs a nested message
    // loop, re-enables owner on close. No resource template - built in code.
    void Show(HWND owner);
}

// wim - a Windows-native terminal workspace for coding agents.
//
// Entry point. The executable is a GUI-subsystem binary: double-clicking it
// starts the app without a console, while running it from a terminal with
// arguments attaches to that terminal for CLI output (see src/cli).
#include <windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR cmdline, int showCmd)
{
    // Placeholder boot: the window, renderer and ConPTY layers arrive in the
    // next commits. Keep a message loop so the process behaves predictably.
    UNREFERENCED_PARAMETER(instance);
    UNREFERENCED_PARAMETER(cmdline);
    UNREFERENCED_PARAMETER(showCmd);
    return 0;
}

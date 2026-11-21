// wim - a Windows-native terminal workspace for coding agents.
//
// Entry point. The executable is a GUI-subsystem binary: double-clicking it
// starts the app without a console, while running it from a terminal with
// arguments attaches to that terminal for CLI output (see src/cli).
#include "core/util/fs.h"
#include "core/util/log.h"
#include "core/util/str.h"
#include "core/version.h"

#include <windows.h>

namespace {

// Prepares %APPDATA%\wim and %LOCALAPPDATA%\wim and opens the log file.
// Every step is best-effort: a read-only profile must not keep the app
// from starting.
void bootDiagnostics()
{
    wim::fs::ensureDir(wim::fs::appDataDir());
    wim::fs::ensureDir(wim::fs::localDataDir());
    wim::log::openFile(wim::fs::join(wim::fs::localDataDir(), L"wim.log"));
    WIM_LOG_INFO("%s starting (exe: %s)", wim::versionString(),
                 wim::str::toUtf8(wim::fs::exeDir()).c_str());
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR cmdline, int showCmd)
{
    UNREFERENCED_PARAMETER(instance);
    UNREFERENCED_PARAMETER(cmdline);
    UNREFERENCED_PARAMETER(showCmd);

    bootDiagnostics();

    // The window, renderer and ConPTY layers arrive in the next commits.
    WIM_LOG_INFO("shutdown");
    return 0;
}

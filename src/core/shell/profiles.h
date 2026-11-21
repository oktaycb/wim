// wim - a Windows-native terminal workspace for coding agents.
// Core/shell: the shell profiles a pane can run. Detection runs once at
// startup; everything here is pure enough to unit test.
#pragma once

#include <string>
#include <vector>

namespace wim::shell {

struct Profile {
    std::wstring id;         // "pwsh", "cmd", "gitbash", "wsl-Ubuntu", ...
    std::wstring displayName; // sidebar / settings text
    std::wstring commandLine; // full command line to spawn
    bool isDefault = false;
};

// All profiles installed on this machine, in a stable order: pwsh,
// powershell, cmd, gitbash, then WSL distros alphabetically.
std::vector<Profile> detectProfiles();

// The profile to open when nothing else is specified.
Profile defaultProfile();

// Finds a profile by id (case-insensitive); null when unknown.
const Profile* findProfile(const std::vector<Profile>& profiles, const std::wstring& id);

// Parses `wsl --list --quiet` output into distro names. The output can be
// UTF-16LE with a BOM or plain UTF-8/ANSI depending on Windows build; both
// are handled. Pure function over raw bytes for testability.
std::vector<std::wstring> parseWslList(const std::string& raw);

// Command line quoting helper: wraps in quotes when the path contains
// spaces, always producing a single argument for CreateProcessW.
std::wstring quoteIfNeeded(const std::wstring& argument);

// Builds the command that launches 'profile' in 'cwd'. Profiles wrap
// shells with a login-ish cd so the pane starts in the right folder.
std::wstring buildCommand(const Profile& profile, const std::wstring& cwd);

} // namespace wim::shell

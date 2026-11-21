// wim - a Windows-native terminal workspace for coding agents.
// Core/conpty: one pseudoconsole plus the shell process attached to it.
// The kernel32 ConPTY API (Windows 10 1809+) is dynamic-loaded so the
// binary also starts on machines where the API is missing.
#pragma once

#include <windows.h>

#include <optional>
#include <string>
#include <vector>

namespace wim::conpty {

class Host {
public:
    Host() = default;
    ~Host();

    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    // Starts 'commandLine' inside a new pseudoconsole of the given size.
    // 'environment' (optional, double-null-terminated wide block, may be
    // null to inherit) and 'cwd' apply to the child. Returns false and
    // fills 'error' when something cannot be created.
    bool start(const std::wstring& commandLine, const std::wstring& cwd, int cols, int rows,
               std::optional<std::wstring> environment, std::wstring& error);

    // Resizes the pseudoconsole grid (columns x rows of cells).
    bool resize(int cols, int rows);

    // Sends bytes to the child as terminal input (already VT-encoded).
    bool writeInput(const char* data, size_t len);

    // Reads available output. Returns nullopt when the read side is closed
    // (child gone). Otherwise returns 0..len bytes; 0 means nothing
    // pending only when nonblocking is set and the pipe is empty.
    std::optional<size_t> readOutput(char* buffer, size_t len);

    // Process state.
    bool running();
    DWORD exitCode();
    DWORD pid() const { return pid_; }

    // Terminates the child process tree and tears down the console.
    void terminate();

    bool valid() const { return hpc_ != nullptr; }

private:
    void closeHandles();

    HPCON hpc_ = nullptr;
    HANDLE outRead_ = nullptr;  // app reads child output here
    HANDLE inWrite_ = nullptr;  // app writes child input here
    HANDLE process_ = nullptr;
    HANDLE thread_ = nullptr;
    DWORD pid_ = 0;
};

// Builds a double-null-terminated environment block from the current
// process environment with additions/replacements ("KEY=value" entries).
// An empty block (no variables) returns L"\0" so CreateProcess inherits.
std::wstring buildEnvironment(const std::vector<std::wstring>& setVars);

// Kills every process descended from pid (Taskkill fallback when the
// toolhelp walk is not possible).
void terminateTree(DWORD pid);

} // namespace wim::conpty

#include "host.h"
#include "core/util/log.h"
#include "core/util/str.h"

#include <tlhelp32.h>

#include <cstdio>
#include <vector>

namespace wim::conpty {

// ---- dynamic loading of the ConPTY API ------------------------------------------

namespace {

using CreatePseudoConsoleFn = HRESULT(WINAPI*)(COORD, HANDLE, HANDLE, DWORD, void**);
using ResizePseudoConsoleFn = HRESULT(WINAPI*)(void*, COORD);
using ClosePseudoConsoleFn = void(WINAPI*)(void*);

struct ConPtyApi {
    CreatePseudoConsoleFn create = nullptr;
    ResizePseudoConsoleFn resize = nullptr;
    ClosePseudoConsoleFn close = nullptr;
    bool loaded = false;

    static ConPtyApi& get()
    {
        static ConPtyApi api;
        if (!api.loaded) {
            api.loaded = true;
            HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
            if (k32) {
                api.create = (CreatePseudoConsoleFn)GetProcAddress(k32, "CreatePseudoConsole");
                api.resize = (ResizePseudoConsoleFn)GetProcAddress(k32, "ResizePseudoConsole");
                api.close = (ClosePseudoConsoleFn)GetProcAddress(k32, "ClosePseudoConsole");
            }
        }
        return api;
    }
};

} // namespace

// ---- lifecycle -------------------------------------------------------------------

Host::~Host()
{
    terminate();
}

bool Host::start(const std::wstring& commandLine, const std::wstring& cwd, int cols, int rows,
                 std::optional<std::wstring> environment, std::wstring& error)
{
    ConPtyApi& api = ConPtyApi::get();
    if (!api.create || !api.resize || !api.close) {
        error = L"ConPTY is not available on this Windows build (1809+ required)";
        return false;
    }
    if (cols < 1)
        cols = 1;
    if (rows < 1)
        rows = 1;

    // Two pipes: input (app -> ConPTY) and output (ConPTY -> app). Created
    // non-inheritable, exactly like the documented sample: inheritable pipe
    // ends combined with the PSEUDOCONSOLE attribute misroute some children
    // on current Windows builds.
    HANDLE inRead = nullptr, inWrite = nullptr, outRead = nullptr, outWrite = nullptr;
    if (!CreatePipe(&inRead, &inWrite, nullptr, 0) ||
        !CreatePipe(&outRead, &outWrite, nullptr, 0)) {
        error = L"CreatePipe failed";
        closeHandles();
        return false;
    }

    const COORD size{(SHORT)cols, (SHORT)rows};
    HRESULT hr = api.create(size, inRead, outWrite, 0, &hpc_);
    if (FAILED(hr)) {
        error = str::wformat(L"CreatePseudoConsole failed (hr=0x%08lX)", (unsigned long)hr);
        CloseHandle(inRead);
        CloseHandle(outWrite);
        CloseHandle(inWrite);
        CloseHandle(outRead);
        return false;
    }
    inWrite_ = inWrite;
    outRead_ = outRead;

    // Spawn the process attached to the pseudoconsole.
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<BYTE> attrBuffer(attrSize);
    si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)attrBuffer.data();
    if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attrSize) ||
        !UpdateProcThreadAttribute(si.lpAttributeList, 0,
                                   PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hpc_, sizeof(hpc_),
                                   nullptr, nullptr)) {
        error = L"attribute list setup failed";
        closeHandles();
        return false;
    }

    PROCESS_INFORMATION pi{};
    const wchar_t* cwdPtr = cwd.empty() ? nullptr : cwd.c_str();
    wchar_t* mutableCmd = _wcsdup(commandLine.c_str());
    const BOOL ok = CreateProcessW(nullptr, mutableCmd, nullptr, nullptr, FALSE,
                                   EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
                                   environment ? (void*)environment->data() : nullptr,
                                   cwdPtr, &si.StartupInfo, &pi);
    free(mutableCmd);
    DeleteProcThreadAttributeList(si.lpAttributeList);
    // The pseudoconsole has its own references now; dropping ours lets I/O
    // detect a broken channel when the session ends (documented lifetime).
    CloseHandle(inRead);
    CloseHandle(outWrite);
    if (!ok) {
        error = str::wformat(L"CreateProcess failed (error %lu) for '%s'", GetLastError(),
                             commandLine.c_str());
        CloseHandle(inRead);
        CloseHandle(outWrite);
        closeHandles();
        return false;
    }
    process_ = pi.hProcess;
    thread_ = pi.hThread;
    pid_ = pi.dwProcessId;
    WIM_LOG_INFO("conpty: started pid %lu (%ls)", pid_, commandLine.c_str());
    return true;
}

bool Host::resize(int cols, int rows)
{
    if (!hpc_ || cols < 1 || rows < 1)
        return false;
    const COORD size{(SHORT)cols, (SHORT)rows};
    return SUCCEEDED(ConPtyApi::get().resize(hpc_, size));
}

bool Host::writeInput(const char* data, size_t len)
{
    if (!inWrite_ || len == 0)
        return false;
    DWORD written = 0;
    return WriteFile(inWrite_, data, (DWORD)len, &written, nullptr) && written == len;
}

std::optional<size_t> Host::readOutput(char* buffer, size_t len)
{
    if (!outRead_)
        return std::nullopt;
    DWORD avail = 0;
    if (!PeekNamedPipe(outRead_, nullptr, 0, nullptr, &avail, nullptr)) {
        // Pipe broken: the child is gone.
        return std::nullopt;
    }
    if (avail == 0) {
        // Distinguish "nothing now" from "closed": PeekNamedPipe succeeds
        // on a live pipe with an empty buffer.
        if (running())
            return size_t(0);
        return std::nullopt;
    }
    DWORD got = 0;
    const DWORD want = (DWORD)(len < avail ? len : avail);
    if (!ReadFile(outRead_, buffer, want, &got, nullptr) || got == 0)
        return std::nullopt;
    return size_t(got);
}

bool Host::running()
{
    if (!process_)
        return false;
    DWORD code = 0;
    if (!GetExitCodeProcess(process_, &code))
        return false;
    return code == STILL_ACTIVE;
}

DWORD Host::exitCode()
{
    DWORD code = 0;
    if (process_ && GetExitCodeProcess(process_, &code))
        return code;
    return 0;
}

void Host::terminate()
{
    if (process_) {
        if (running())
            terminateTree(pid_);
        CloseHandle(process_);
        process_ = nullptr;
    }
    if (thread_) {
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    closeHandles();
    pid_ = 0;
}

void Host::closeHandles()
{
    if (hpc_) {
        ConPtyApi::get().close(hpc_);
        hpc_ = nullptr;
    }
    if (inWrite_) {
        CloseHandle(inWrite_);
        inWrite_ = nullptr;
    }
    if (outRead_) {
        CloseHandle(outRead_);
        outRead_ = nullptr;
    }
}

// ---- environment ----------------------------------------------------------------

std::wstring buildEnvironment(const std::vector<std::wstring>& setVars)
{
    std::vector<std::wstring> entries;

    LPWCH block = GetEnvironmentStringsW();
    if (block) {
        LPWCH p = block;
        while (*p) {
            const std::wstring var = p;
            // Skip pseudo-entries like "=C:" (per-drive working dirs).
            if (!var.empty() && var[0] != L'=')
                entries.push_back(var);
            p += var.size() + 1;
        }
        FreeEnvironmentStringsW(block);
    }

    for (const auto& v : setVars) {
        const size_t eq = v.find(L'=');
        if (eq == std::wstring::npos)
            continue;
        const std::wstring key = v.substr(0, eq) + L"=";
        bool replaced = false;
        for (auto& existing : entries) {
            if (_wcsnicmp(existing.c_str(), key.c_str(), key.size()) == 0) {
                existing = v;
                replaced = true;
                break;
            }
        }
        if (!replaced)
            entries.push_back(v);
    }

    std::wstring out;
    for (const auto& e : entries) {
        out += e;
        out += L'\0';
    }
    out += L'\0';
    return out;
}

// ---- process tree termination ----------------------------------------------------

void terminateTree(DWORD pid)
{
    // Build pid -> parent map with one snapshot, then walk up from every
    // process collecting our descendants.
    std::vector<PROCESSENTRY32W> procs;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snap, &entry)) {
            do {
                procs.push_back(entry);
            } while (Process32NextW(snap, &entry));
        }
        CloseHandle(snap);
    }

    std::vector<DWORD> toKill;
    bool changed = true;
    toKill.push_back(pid);
    while (changed) {
        changed = false;
        for (const auto& p : procs) {
            for (DWORD root : toKill) {
                if (p.th32ParentProcessID == root) {
                    bool known = false;
                    for (DWORD k : toKill)
                        if (k == p.th32ProcessID)
                            known = true;
                    if (!known) {
                        toKill.push_back(p.th32ProcessID);
                        changed = true;
                    }
                    break;
                }
            }
        }
    }

    // Children first so they cannot respawn or block on the parent.
    for (auto it = toKill.rbegin(); it != toKill.rend(); ++it) {
        if (*it == pid)
            continue;
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, *it);
        if (h) {
            TerminateProcess(h, 1);
            CloseHandle(h);
        }
    }
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (h) {
        TerminateProcess(h, 1);
        CloseHandle(h);
    }
}

} // namespace wim::conpty

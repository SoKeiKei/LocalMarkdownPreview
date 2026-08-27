#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <tlhelp32.h>

#include <cwchar>
#include <string>

namespace {

class Snapshot final {
public:
    explicit Snapshot(HANDLE value) noexcept : _value(value) {}
    Snapshot(const Snapshot&) = delete;
    Snapshot& operator=(const Snapshot&) = delete;
    ~Snapshot()
    {
        if (_value != INVALID_HANDLE_VALUE) {
            CloseHandle(_value);
        }
    }
    HANDLE Get() const noexcept { return _value; }
    bool IsValid() const noexcept { return _value != INVALID_HANDLE_VALUE; }

private:
    HANDLE _value;
};

bool SamePath(const wchar_t* left, const std::wstring& right) noexcept
{
    return left != nullptr && _wcsicmp(left, right.c_str()) == 0;
}

bool ProcessLoadsModule(DWORD processId, const std::wstring& expectedPath)
{
    Snapshot modules(CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
        processId));
    if (!modules.IsValid()) {
        return false;
    }
    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);
    if (!Module32FirstW(modules.Get(), &module)) {
        return false;
    }
    do {
        if (SamePath(module.szExePath, expectedPath)) {
            return true;
        }
    } while (Module32NextW(modules.Get(), &module));
    return false;
}

std::wstring FullPath(const wchar_t* path)
{
    const DWORD required = GetFullPathNameW(path, 0, nullptr, nullptr);
    if (required == 0 || required > 32768) {
        return {};
    }
    std::wstring result(required, L'\0');
    const DWORD written = GetFullPathNameW(path, required, result.data(), nullptr);
    if (written == 0 || written >= required) {
        return {};
    }
    result.resize(written);
    return result;
}

} // namespace

int wmain(int argc, wchar_t* argv[])
{
    if (argc != 2) {
        return 2;
    }
    const std::wstring expectedPath = FullPath(argv[1]);
    if (expectedPath.empty()) {
        return 2;
    }

    Snapshot processes(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!processes.IsValid()) {
        return 1;
    }
    PROCESSENTRY32W processEntry{};
    processEntry.dwSize = sizeof(processEntry);
    if (!Process32FirstW(processes.Get(), &processEntry)) {
        return 1;
    }

    bool failed = false;
    do {
        if (_wcsicmp(processEntry.szExeFile, L"prevhost.exe") != 0 ||
            !ProcessLoadsModule(processEntry.th32ProcessID, expectedPath)) {
            continue;
        }
        HANDLE process = OpenProcess(
            PROCESS_TERMINATE | SYNCHRONIZE,
            FALSE,
            processEntry.th32ProcessID);
        if (process == nullptr) {
            failed = true;
            continue;
        }
        if (!TerminateProcess(process, 0) || WaitForSingleObject(process, 5000) != WAIT_OBJECT_0) {
            failed = true;
        }
        CloseHandle(process);
    } while (Process32NextW(processes.Get(), &processEntry));
    return failed ? 1 : 0;
}

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <richedit.h>
#include <shlwapi.h>
#include <shobjidl.h>

#include <cstdio>
#include <stdexcept>
#include <string>

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void RequireHr(HRESULT actual, HRESULT expected, const char* message)
{
    if (actual != expected) {
        char buffer[256]{};
        sprintf_s(buffer, "%s (expected=0x%08lX actual=0x%08lX)", message,
                  static_cast<unsigned long>(expected),
                  static_cast<unsigned long>(actual));
        throw std::runtime_error(buffer);
    }
}

template <typename T>
void ReleaseInterface(T*& value)
{
    if (value != nullptr) {
        value->Release();
        value = nullptr;
    }
}

LRESULT CALLBACK ParentWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProcW(window, message, wParam, lParam);
}

HWND CreateParentWindow()
{
    constexpr wchar_t className[] = L"LocalMarkdownPreview.RegisteredTestParent";
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = ParentWindowProc;
    windowClass.lpszClassName = className;
    if (RegisterClassExW(&windowClass) == 0 &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw std::runtime_error("RegisterClassExW failed");
    }
    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, className, L"Registered test",
                                  WS_OVERLAPPEDWINDOW, 0, 0, 640, 480, nullptr,
                                  nullptr, instance, nullptr);
    Require(window != nullptr, "CreateWindowExW failed");
    ShowWindow(window, SW_SHOWNA);
    return window;
}

std::wstring ReadWindowText(HWND window)
{
    const LRESULT lengthResult = SendMessageW(window, WM_GETTEXTLENGTH, 0, 0);
    const int length = lengthResult > 0 ? static_cast<int>(lengthResult) : 0;
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    const LRESULT writtenResult = SendMessageW(window,
                                               WM_GETTEXT,
                                               static_cast<WPARAM>(text.size()),
                                               reinterpret_cast<LPARAM>(text.data()));
    const int written = writtenResult > 0 ? static_cast<int>(writtenResult) : 0;
    text.resize(written > 0 ? static_cast<std::size_t>(written) : 0);
    return text;
}

std::wstring ProcessImagePath(DWORD processId)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    Require(process != nullptr, "OpenProcess failed for preview host");
    wchar_t path[32768]{};
    DWORD length = static_cast<DWORD>(_countof(path));
    const BOOL ok = QueryFullProcessImageNameW(process, 0, path, &length);
    CloseHandle(process);
    Require(ok != FALSE, "QueryFullProcessImageNameW failed");
    return std::wstring(path, length);
}

DWORD ProcessIntegrityRid(DWORD processId)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    Require(process != nullptr, "OpenProcess failed for integrity check");
    HANDLE token = nullptr;
    const BOOL tokenOpened = OpenProcessToken(process, TOKEN_QUERY, &token);
    CloseHandle(process);
    Require(tokenOpened != FALSE, "OpenProcessToken failed");

    DWORD required = 0;
    GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &required);
    Require(required > 0, "TokenIntegrityLevel size query failed");
    auto* buffer = new BYTE[required];
    const BOOL read = GetTokenInformation(token, TokenIntegrityLevel, buffer,
                                          required, &required);
    CloseHandle(token);
    Require(read != FALSE, "TokenIntegrityLevel query failed");
    auto* label = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(buffer);
    PSID sid = label->Label.Sid;
    const UCHAR count = *GetSidSubAuthorityCount(sid);
    Require(count > 0, "Integrity SID has no sub-authority");
    const DWORD rid = *GetSidSubAuthority(sid, count - 1);
    delete[] buffer;
    return rid;
}

void RunRegisteredPreview(const std::wstring& markdownPath)
{
    CLSID clsid{};
    RequireHr(CLSIDFromString(L"{5893C3F8-C5FB-41E4-90C8-02389BF3EA4C}",
                              &clsid), S_OK, "CLSIDFromString failed");

    IUnknown* unknown = nullptr;
    RequireHr(CoCreateInstance(clsid, nullptr, CLSCTX_LOCAL_SERVER, IID_IUnknown,
                               reinterpret_cast<void**>(&unknown)),
              S_OK, "registered out-of-process activation failed");

    IInitializeWithFile* fileInitializer = nullptr;
    IPreviewHandler* preview = nullptr;
    IOleWindow* oleWindow = nullptr;
    IPreviewHandlerVisuals* visuals = nullptr;
    RequireHr(unknown->QueryInterface(IID_PPV_ARGS(&fileInitializer)), S_OK,
              "IInitializeWithFile unavailable through proxy");
    RequireHr(unknown->QueryInterface(IID_PPV_ARGS(&preview)), S_OK,
              "IPreviewHandler unavailable through proxy");
    RequireHr(unknown->QueryInterface(IID_PPV_ARGS(&oleWindow)), S_OK,
              "IOleWindow unavailable through proxy");
    RequireHr(unknown->QueryInterface(IID_PPV_ARGS(&visuals)), S_OK,
              "IPreviewHandlerVisuals unavailable through proxy");

    RequireHr(fileInitializer->Initialize(markdownPath.c_str(), STGM_READ), S_OK,
              "registered handler file Initialize failed");

    LOGFONTW font{};
    font.lfHeight = -16;
    wcscpy_s(font.lfFaceName, _countof(font.lfFaceName), L"Segoe UI");
    RequireHr(visuals->SetBackgroundColor(RGB(250, 250, 250)), S_OK,
              "SetBackgroundColor failed");
    RequireHr(visuals->SetTextColor(RGB(24, 24, 24)), S_OK,
              "SetTextColor failed");
    RequireHr(visuals->SetFont(&font), S_OK, "SetFont failed");

    HWND parent = CreateParentWindow();
    RECT rect{0, 0, 600, 420};
    RequireHr(preview->SetWindow(parent, &rect), S_OK, "SetWindow failed");
    RequireHr(preview->DoPreview(), S_OK, "DoPreview failed");

    HWND child = nullptr;
    RequireHr(oleWindow->GetWindow(&child), S_OK, "GetWindow failed");
    Require(child != nullptr && IsWindow(child), "preview child window is invalid");
    const LONG_PTR childStyle = GetWindowLongPtrW(child, GWL_STYLE);
    Require((childStyle & WS_VSCROLL) != 0, "registered preview has no vertical scrolling");
    Require((childStyle & ES_AUTOHSCROLL) == 0,
            "registered preview still uses automatic horizontal scrolling");
    RECT narrowRect{0, 0, 280, 420};
    RequireHr(preview->SetRect(&narrowRect), S_OK, "narrow SetRect failed");
    const LRESULT narrowLineCount = SendMessageW(child, EM_GETLINECOUNT, 0, 0);
    RECT wideRect{0, 0, 600, 420};
    RequireHr(preview->SetRect(&wideRect), S_OK, "wide SetRect failed");
    const LRESULT wideLineCount = SendMessageW(child, EM_GETLINECOUNT, 0, 0);
    Require(wideLineCount < narrowLineCount,
            "registered preview text did not reflow with pane width");
    const std::wstring text = ReadWindowText(child);
    Require(text.find(L"一级标题：中文 Markdown 预览") != std::wstring::npos,
            "Chinese Markdown content was not rendered");
    Require(text.find(L"# 一级标题") == std::wstring::npos,
            "Markdown heading marker remained visible");
    Require(text.find(L"代码块只显示，不执行。") != std::wstring::npos,
            "code block text was not displayed safely");
    Require(text.find(L"```text") == std::wstring::npos,
            "code fence marker remained visible");

    DWORD hostProcessId = 0;
    GetWindowThreadProcessId(child, &hostProcessId);
    Require(hostProcessId != 0 && hostProcessId != GetCurrentProcessId(),
            "handler did not run in a separate host process");
    const std::wstring imagePath = ProcessImagePath(hostProcessId);
    const wchar_t* fileName = PathFindFileNameW(imagePath.c_str());
    Require(fileName != nullptr && _wcsicmp(fileName, L"prevhost.exe") == 0,
            "handler host is not prevhost.exe");
    const DWORD integrityRid = ProcessIntegrityRid(hostProcessId);
    Require(integrityRid <= SECURITY_MANDATORY_MEDIUM_RID,
            "direct activation host is above medium integrity");

    RequireHr(preview->Unload(), S_OK, "Unload failed");
    DestroyWindow(parent);
    ReleaseInterface(visuals);
    ReleaseInterface(oleWindow);
    ReleaseInterface(preview);
    ReleaseInterface(fileInitializer);
    ReleaseInterface(unknown);

    std::wprintf(L"PASS: registered preview host pid=%lu integrityRid=0x%lX image=%ls\n",
                 static_cast<unsigned long>(hostProcessId),
                 static_cast<unsigned long>(integrityRid), imagePath.c_str());
}

} // namespace

int wmain(int argc, wchar_t* argv[])
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: RegisteredPreviewTests.exe <markdown-path>\n");
        return 2;
    }
    const HRESULT initializeHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initializeHr)) {
        std::fprintf(stderr, "CoInitializeEx failed: 0x%08lX\n",
                     static_cast<unsigned long>(initializeHr));
        return 1;
    }
    int exitCode = 0;
    try {
        RunRegisteredPreview(argv[1]);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        exitCode = 1;
    }
    CoUninitialize();
    return exitCode;
}

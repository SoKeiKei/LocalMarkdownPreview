#include "LocalMarkdownPreview.h"
#include "ThemePalette.h"

#include <windows.h>
#include <richole.h>

#include <cstdio>
#include <cstdint>
#include <cwchar>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using DllCanUnloadNowFunction = HRESULT(STDAPICALLTYPE*)();
using DllGetClassObjectFunction = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, void**);

constexpr size_t kMaxPreviewBytes = 5u * 1024u * 1024u;

void Require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void RequireHr(HRESULT actual, HRESULT expected, const char* message) {
    if (actual != expected) {
        char detail[160]{};
        std::snprintf(detail, sizeof(detail), "%s (actual=0x%08lX expected=0x%08lX)", message,
                      static_cast<unsigned long>(actual), static_cast<unsigned long>(expected));
        throw std::runtime_error(detail);
    }
}

void VerifyThemePalettes() {
    using LocalMarkdownPreview::BuildMarkdownThemePalette;
    using LocalMarkdownPreview::HasReadableThemeContrast;
    using LocalMarkdownPreview::IsDarkThemeColor;

    const auto light = BuildMarkdownThemePalette(
        RGB(250, 250, 250), true, RGB(30, 30, 30), true,
        false, false, RGB(0, 95, 184));
    Require(!light.dark && !IsDarkThemeColor(light.background),
            "light palette selected a dark background");
    Require(HasReadableThemeContrast(light.text, light.background),
            "light palette body text has insufficient contrast");
    Require(HasReadableThemeContrast(light.heading, light.background),
            "light palette heading has insufficient contrast");
    Require(light.code_background != light.background,
            "light palette code surface is invisible");

    const auto dark = BuildMarkdownThemePalette(
        RGB(250, 250, 250), true, RGB(30, 30, 30), true,
        true, false, RGB(102, 179, 255));
    Require(dark.dark && IsDarkThemeColor(dark.background),
            "dark palette accepted a stale light background");
    Require(HasReadableThemeContrast(dark.text, dark.background),
            "dark palette body text has insufficient contrast");
    Require(HasReadableThemeContrast(dark.link, dark.background),
            "dark palette link has insufficient contrast");
    Require(dark.code_background != dark.background,
            "dark palette code surface is invisible");

    const COLORREF contrastBackground = RGB(0, 0, 0);
    const COLORREF contrastText = RGB(255, 255, 0);
    const COLORREF contrastLink = RGB(0, 255, 255);
    const auto highContrast = BuildMarkdownThemePalette(
        contrastBackground, true, contrastText, true,
        true, true, contrastLink);
    Require(highContrast.high_contrast &&
                highContrast.background == contrastBackground &&
                highContrast.text == contrastText &&
                highContrast.link == contrastLink,
            "high-contrast system colors were not preserved");
}

class MemoryStream final : public IStream {
public:
    MemoryStream(std::vector<BYTE> bytes, bool supportStat, const wchar_t* sourceName)
        : _referenceCount(1),
          _bytes(std::move(bytes)),
          _position(0),
          _supportStat(supportStat),
          _sourceName(sourceName == nullptr ? L"" : sourceName),
          _readCount(0),
          _statCount(0) {}

    MemoryStream(const MemoryStream&) = delete;
    MemoryStream& operator=(const MemoryStream&) = delete;

    ULONG ReadCount() const noexcept { return _readCount; }
    ULONG StatCount() const noexcept { return _statCount; }

    STDMETHODIMP QueryInterface(REFIID riid, void** result) override {
        if (result == nullptr) {
            return E_POINTER;
        }
        *result = nullptr;
        if (riid == IID_IUnknown || riid == IID_ISequentialStream || riid == IID_IStream) {
            *result = static_cast<IStream*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&_referenceCount));
    }

    STDMETHODIMP_(ULONG) Release() override {
        const LONG remaining = InterlockedDecrement(&_referenceCount);
        if (remaining == 0) {
            delete this;
            return 0;
        }
        return remaining > 0 ? static_cast<ULONG>(remaining) : 0;
    }

    STDMETHODIMP Read(void* buffer, ULONG count, ULONG* read) override {
        if (read == nullptr) {
            return E_POINTER;
        }
        *read = 0;
        if (count == 0) {
            return S_OK;
        }
        if (buffer == nullptr) {
            return E_POINTER;
        }

        ++_readCount;
        if (_position >= _bytes.size()) {
            return S_FALSE;
        }
        const size_t available = _bytes.size() - static_cast<size_t>(_position);
        const size_t amount = available < count ? available : count;
        std::memcpy(buffer, _bytes.data() + static_cast<size_t>(_position), amount);
        _position += static_cast<ULONGLONG>(amount);
        *read = static_cast<ULONG>(amount);
        return amount < count ? S_FALSE : S_OK;
    }

    STDMETHODIMP Write(const void*, ULONG, ULONG* written) override {
        if (written != nullptr) {
            *written = 0;
        }
        return STG_E_ACCESSDENIED;
    }

    STDMETHODIMP Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER* newPosition) override {
        LONGLONG base = 0;
        switch (origin) {
        case STREAM_SEEK_SET:
            base = 0;
            break;
        case STREAM_SEEK_CUR:
            base = static_cast<LONGLONG>(_position);
            break;
        case STREAM_SEEK_END:
            base = static_cast<LONGLONG>(_bytes.size());
            break;
        default:
            return STG_E_INVALIDFUNCTION;
        }

        const LONGLONG target = base + move.QuadPart;
        if (target < 0) {
            return STG_E_INVALIDFUNCTION;
        }
        _position = static_cast<ULONGLONG>(target);
        if (newPosition != nullptr) {
            newPosition->QuadPart = _position;
        }
        return S_OK;
    }

    STDMETHODIMP SetSize(ULARGE_INTEGER) override { return STG_E_ACCESSDENIED; }

    STDMETHODIMP CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override {
        return E_NOTIMPL;
    }

    STDMETHODIMP Commit(DWORD) override { return E_NOTIMPL; }
    STDMETHODIMP Revert() override { return E_NOTIMPL; }

    STDMETHODIMP LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override {
        return E_NOTIMPL;
    }

    STDMETHODIMP UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override {
        return E_NOTIMPL;
    }

    STDMETHODIMP Stat(STATSTG* statistics, DWORD flags) override {
        ++_statCount;
        if (statistics == nullptr) {
            return E_POINTER;
        }
        ZeroMemory(statistics, sizeof(*statistics));
        if (!_supportStat) {
            return E_NOTIMPL;
        }

        statistics->type = STGTY_STREAM;
        statistics->cbSize.QuadPart = static_cast<ULONGLONG>(_bytes.size());
        if (flags != STATFLAG_NONAME && !_sourceName.empty()) {
            const size_t bytes = (_sourceName.size() + 1) * sizeof(wchar_t);
            PWSTR name = static_cast<PWSTR>(CoTaskMemAlloc(bytes));
            if (name == nullptr) {
                return E_OUTOFMEMORY;
            }
            std::memcpy(name, _sourceName.c_str(), bytes);
            statistics->pwcsName = name;
        }
        return S_OK;
    }

    STDMETHODIMP Clone(IStream**) override { return E_NOTIMPL; }

private:
    ~MemoryStream() = default;

    LONG _referenceCount;
    std::vector<BYTE> _bytes;
    ULONGLONG _position;
    bool _supportStat;
    std::wstring _sourceName;
    ULONG _readCount;
    ULONG _statCount;
};

class FrameSite final : public IPreviewHandlerFrame {
public:
    FrameSite() : _referenceCount(1), _called(false) {}

    bool Called() const noexcept { return _called; }

    STDMETHODIMP QueryInterface(REFIID riid, void** result) override {
        if (result == nullptr) {
            return E_POINTER;
        }
        *result = nullptr;
        if (riid == IID_IUnknown || riid == __uuidof(IPreviewHandlerFrame)) {
            *result = static_cast<IPreviewHandlerFrame*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&_referenceCount));
    }

    STDMETHODIMP_(ULONG) Release() override {
        const LONG remaining = InterlockedDecrement(&_referenceCount);
        if (remaining == 0) {
            delete this;
            return 0;
        }
        return remaining > 0 ? static_cast<ULONG>(remaining) : 0;
    }

    STDMETHODIMP TranslateAccelerator(MSG* message) override {
        _called = message != nullptr;
        return _called ? S_OK : E_INVALIDARG;
    }

    STDMETHODIMP GetWindowContext(PREVIEWHANDLERFRAMEINFO* frameInfo) override {
        if (frameInfo == nullptr) {
            return E_POINTER;
        }
        ZeroMemory(frameInfo, sizeof(*frameInfo));
        return E_NOTIMPL;
    }

private:
    ~FrameSite() = default;

    LONG _referenceCount;
    bool _called;
};

LRESULT CALLBACK ParentWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(window, message, wParam, lParam);
}

HWND CreateParentWindow() {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    constexpr wchar_t kClassName[] = L"LocalMarkdownPreview.NativeSmokeParent";

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = ParentWindowProc;
    windowClass.lpszClassName = kClassName;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw std::runtime_error("RegisterClassExW failed");
    }

    HWND parent = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kClassName,
        L"Native smoke parent",
        WS_OVERLAPPEDWINDOW,
        0,
        0,
        320,
        220,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (parent == nullptr) {
        throw std::runtime_error("CreateWindowExW failed");
    }

    ShowWindow(parent, SW_SHOWNA);
    SetActiveWindow(parent);
    return parent;
}

std::wstring ReadWindowText(HWND window) {
    const int length = GetWindowTextLengthW(window);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    const int written = GetWindowTextW(window, text.data(), static_cast<int>(text.size()));
    text.resize(written < 0 ? 0 : static_cast<size_t>(written));
    return text;
}

std::vector<BYTE> Utf8Bytes(const char* text) {
    const size_t length = std::strlen(text);
    return std::vector<BYTE>(reinterpret_cast<const BYTE*>(text),
                             reinterpret_cast<const BYTE*>(text) + length);
}

std::vector<BYTE> FilledBytes(size_t size, BYTE value) {
    return std::vector<BYTE>(size, value);
}

class LocalImageFixture final {
public:
    LocalImageFixture() {
        wchar_t temporaryRoot[MAX_PATH]{};
        Require(GetTempPathW(_countof(temporaryRoot), temporaryRoot) > 0,
                "GetTempPathW failed");
        wchar_t candidate[MAX_PATH]{};
        Require(GetTempFileNameW(temporaryRoot, L"LMP", 0, candidate) != 0,
                "GetTempFileNameW failed");
        Require(DeleteFileW(candidate) != FALSE, "temporary marker deletion failed");
        Require(CreateDirectoryW(candidate, nullptr) != FALSE,
                "temporary image directory creation failed");
        _directory = candidate;
        _documentPath = _directory + L"\\preview.md";
        _imagePath = _directory + L"\\preview.bmp";

        const std::vector<BYTE> bitmap = CreateBitmapFixture(320, 120);
        WriteFile(_imagePath, bitmap.data(), bitmap.size());
        static constexpr char markdown[] =
            "# \xe6\xa0\x87\xe9\xa2\x98\n\n\xe4\xbd\xa0\xe5\xa5\xbd\n\n"
            "> quote\n\n`inline` and [link](https://example.com)\n\n"
            "```txt\ncode block\n```\n\n"
            "| A | B |\n|---|---|\n| 1 | 2 |\n\n"
            "![local image](preview.bmp)\n";
        WriteFile(_documentPath,
                  reinterpret_cast<const BYTE*>(markdown),
                  sizeof(markdown) - 1);
    }

    ~LocalImageFixture() {
        if (!_imagePath.empty()) {
            DeleteFileW(_imagePath.c_str());
        }
        if (!_documentPath.empty()) {
            DeleteFileW(_documentPath.c_str());
        }
        for (const std::wstring& path : _additionalPaths) {
            DeleteFileW(path.c_str());
        }
        if (!_directory.empty()) {
            RemoveDirectoryW(_directory.c_str());
        }
    }

    const std::wstring& DocumentPath() const noexcept { return _documentPath; }

    std::wstring CreateNamedFile(const wchar_t* name, const std::vector<BYTE>& bytes) {
        std::wstring path = _directory + L"\\" + name;
        WriteFile(path, bytes.data(), bytes.size());
        _additionalPaths.push_back(path);
        return path;
    }

private:
    static void WriteLittleEndian16(std::vector<BYTE>* bytes,
                                    std::size_t offset,
                                    std::uint16_t value) {
        (*bytes)[offset] = static_cast<BYTE>(value & 0xFFu);
        (*bytes)[offset + 1] = static_cast<BYTE>((value >> 8) & 0xFFu);
    }

    static void WriteLittleEndian32(std::vector<BYTE>* bytes,
                                    std::size_t offset,
                                    std::uint32_t value) {
        for (unsigned index = 0; index < 4; ++index) {
            (*bytes)[offset + index] =
                static_cast<BYTE>((value >> (index * 8u)) & 0xFFu);
        }
    }

    static std::vector<BYTE> CreateBitmapFixture(std::uint32_t width,
                                                  std::uint32_t height) {
        const std::uint32_t stride = (width * 3u + 3u) & ~3u;
        const std::uint32_t pixelBytes = stride * height;
        std::vector<BYTE> bytes(54u + pixelBytes, 0);
        bytes[0] = 'B';
        bytes[1] = 'M';
        WriteLittleEndian32(&bytes, 2, static_cast<std::uint32_t>(bytes.size()));
        WriteLittleEndian32(&bytes, 10, 54);
        WriteLittleEndian32(&bytes, 14, 40);
        WriteLittleEndian32(&bytes, 18, width);
        WriteLittleEndian32(&bytes, 22, height);
        WriteLittleEndian16(&bytes, 26, 1);
        WriteLittleEndian16(&bytes, 28, 24);
        WriteLittleEndian32(&bytes, 34, pixelBytes);
        for (std::uint32_t row = 0; row < height; ++row) {
            BYTE* pixels = bytes.data() + 54u + row * stride;
            for (std::uint32_t column = 0; column < width; ++column) {
                pixels[column * 3u] = 224;
                pixels[column * 3u + 1] = 132;
                pixels[column * 3u + 2] = 36;
            }
        }
        return bytes;
    }

    static void WriteFile(const std::wstring& path, const BYTE* bytes, std::size_t size) {
        HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
        Require(file != INVALID_HANDLE_VALUE, "temporary fixture file creation failed");
        DWORD written = 0;
        const BOOL success = ::WriteFile(file, bytes, static_cast<DWORD>(size), &written, nullptr);
        CloseHandle(file);
        Require(success != FALSE && written == size, "temporary fixture write failed");
    }

    std::wstring _directory;
    std::wstring _documentPath;
    std::wstring _imagePath;
    std::vector<std::wstring> _additionalPaths;
};

void QueryHandlerInterfaces(IUnknown* unknown,
                            IObjectWithSite** site,
                            IPreviewHandler** preview,
                            IOleWindow** oleWindow,
                            IInitializeWithFile** fileInitializer,
                            IPreviewHandlerVisuals** visuals) {
    RequireHr(unknown->QueryInterface(__uuidof(IObjectWithSite), reinterpret_cast<void**>(site)), S_OK,
              "IObjectWithSite is unavailable");
    RequireHr(unknown->QueryInterface(__uuidof(IPreviewHandler), reinterpret_cast<void**>(preview)), S_OK,
              "IPreviewHandler is unavailable");
    RequireHr(unknown->QueryInterface(__uuidof(IOleWindow), reinterpret_cast<void**>(oleWindow)), S_OK,
              "IOleWindow is unavailable");
    RequireHr(unknown->QueryInterface(__uuidof(IInitializeWithFile), reinterpret_cast<void**>(fileInitializer)),
              S_OK, "IInitializeWithFile is unavailable");
    RequireHr(unknown->QueryInterface(__uuidof(IPreviewHandlerVisuals), reinterpret_cast<void**>(visuals)), S_OK,
              "IPreviewHandlerVisuals is unavailable");
}

void RunSmoke(const std::wstring& dllPath) {
    VerifyThemePalettes();
    HMODULE module = LoadLibraryW(dllPath.c_str());
    Require(module != nullptr, "LoadLibraryW failed");

    auto canUnload = reinterpret_cast<DllCanUnloadNowFunction>(
        GetProcAddress(module, "DllCanUnloadNow"));
    auto getClassObject = reinterpret_cast<DllGetClassObjectFunction>(
        GetProcAddress(module, "DllGetClassObject"));
    Require(canUnload != nullptr && getClassObject != nullptr, "COM exports are missing");
    RequireHr(canUnload(), S_OK, "DLL is not initially unloadable");

    CLSID clsid{};
    RequireHr(CLSIDFromString(
                  L"{5893C3F8-C5FB-41E4-90C8-02389BF3EA4C}",
                  &clsid),
              S_OK, "CLSIDFromString failed");

    IClassFactory* factory = nullptr;
    RequireHr(getClassObject(clsid, __uuidof(IClassFactory), reinterpret_cast<void**>(&factory)), S_OK,
              "DllGetClassObject failed");
    RequireHr(canUnload(), S_FALSE, "class factory did not pin the module");
    RequireHr(factory->LockServer(TRUE), S_OK, "LockServer(TRUE) failed");
    RequireHr(canUnload(), S_FALSE, "LockServer(TRUE) did not pin the module");
    RequireHr(factory->LockServer(FALSE), S_OK, "LockServer(FALSE) failed");

    HWND parent = CreateParentWindow();

    IUnknown* unknown = nullptr;
    RequireHr(factory->CreateInstance(nullptr, IID_IUnknown, reinterpret_cast<void**>(&unknown)), S_OK,
              "CreateInstance failed");

    IObjectWithSite* site = nullptr;
    IPreviewHandler* preview = nullptr;
    IOleWindow* oleWindow = nullptr;
    IInitializeWithFile* fileInitializer = nullptr;
    IPreviewHandlerVisuals* visuals = nullptr;
    QueryHandlerInterfaces(unknown, &site, &preview, &oleWindow, &fileInitializer, &visuals);
    IInitializeWithStream* unavailableStreamInitializer = nullptr;
    RequireHr(unknown->QueryInterface(__uuidof(IInitializeWithStream),
                                      reinterpret_cast<void**>(&unavailableStreamInitializer)),
              E_NOINTERFACE,
              "stream initialization unexpectedly hid the document directory");

    RequireHr(preview->DoPreview(), E_UNEXPECTED, "DoPreview accepted an uninitialized handler");

    LocalImageFixture imageFixture;
    RequireHr(fileInitializer->Initialize(imageFixture.DocumentPath().c_str(), STGM_READ),
              S_OK, "read-only file Initialize failed");
    RequireHr(fileInitializer->Initialize(imageFixture.DocumentPath().c_str(), STGM_READWRITE), STG_E_ACCESSDENIED,
              "read/write Initialize was not rejected");
    RequireHr(fileInitializer->Initialize(imageFixture.DocumentPath().c_str(), STGM_READ), HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED),
              "second Initialize was accepted");

    FrameSite* frame = new FrameSite();
    RequireHr(site->SetSite(static_cast<IUnknown*>(frame)), S_OK, "SetSite failed");
    IUnknown* returnedSite = nullptr;
    RequireHr(site->GetSite(IID_IUnknown, reinterpret_cast<void**>(&returnedSite)), S_OK,
              "GetSite failed");
    returnedSite->Release();

    MSG message{};
    message.hwnd = parent;
    message.message = WM_KEYDOWN;
    message.wParam = VK_ESCAPE;
    RequireHr(preview->TranslateAccelerator(&message), S_OK, "TranslateAccelerator did not reach the frame");
    Require(frame->Called(), "preview frame did not receive the accelerator");
    RequireHr(preview->TranslateAccelerator(nullptr), E_INVALIDARG, "null accelerator was accepted");
    RequireHr(site->SetSite(nullptr), S_OK, "clearing the site failed");
    frame->Release();

    LOGFONTW font{};
    font.lfHeight = -16;
    font.lfWeight = FW_NORMAL;
    wcscpy_s(font.lfFaceName, _countof(font.lfFaceName), L"Segoe UI");
    RequireHr(visuals->SetBackgroundColor(RGB(250, 250, 250)), S_OK, "SetBackgroundColor failed");
    RequireHr(visuals->SetTextColor(RGB(30, 30, 30)), S_OK, "SetTextColor failed");
    RequireHr(visuals->SetFont(&font), S_OK, "SetFont failed");

    RECT rect{0, 0, 280, 180};
    RequireHr(preview->SetWindow(parent, &rect), S_OK, "SetWindow failed");
    RECT movedRect{4, 5, 270, 175};
    RequireHr(preview->SetRect(&movedRect), S_OK, "SetRect before preview failed");
    RequireHr(preview->DoPreview(), S_OK, "DoPreview failed for a UTF-8 file");

    HWND previewWindow = nullptr;
    RequireHr(oleWindow->GetWindow(&previewWindow), S_OK, "GetWindow failed after DoPreview");
    Require(previewWindow != nullptr && IsWindow(previewWindow), "preview window is invalid");
    const std::wstring previewText = ReadWindowText(previewWindow);
    Require(previewText.find(L"标题") != std::wstring::npos, "UTF-8 text was not displayed");
    Require(previewText.find(L"# 标题") == std::wstring::npos,
            "Markdown heading marker remained visible");
    CHARRANGE headingRange{0, 2};
    SendMessageW(previewWindow, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&headingRange));
    CHARFORMAT2W headingFormat{};
    headingFormat.cbSize = sizeof(headingFormat);
    headingFormat.dwMask = CFM_BOLD | CFM_COLOR | CFM_SIZE;
    SendMessageW(previewWindow, EM_GETCHARFORMAT, TRUE, reinterpret_cast<LPARAM>(&headingFormat));
    Require((headingFormat.dwEffects & CFE_BOLD) != 0,
            "Markdown heading was not formatted as bold");
    PARAFORMAT2 headingParagraph{};
    headingParagraph.cbSize = sizeof(headingParagraph);
    SendMessageW(previewWindow,
                 EM_GETPARAFORMAT,
                 0,
                 reinterpret_cast<LPARAM>(&headingParagraph));
    Require(headingParagraph.dySpaceAfter > 0,
            "Markdown heading has no visual separation from body text");
    const COLORREF temporaryBackground = RGB(1, 2, 3);
    const COLORREF previewBackground = static_cast<COLORREF>(SendMessageW(
        previewWindow, EM_SETBKGNDCOLOR, FALSE, temporaryBackground));
    SendMessageW(previewWindow,
                 EM_SETBKGNDCOLOR,
                 FALSE,
                 static_cast<LPARAM>(previewBackground));
    Require(LocalMarkdownPreview::HasReadableThemeContrast(
                headingFormat.crTextColor, previewBackground),
            "Markdown heading color has insufficient theme contrast");
    const std::size_t codeStart = previewText.find(L"code block");
    Require(codeStart != std::wstring::npos, "code block text is missing");
    CHARRANGE codeRange{static_cast<LONG>(codeStart),
                        static_cast<LONG>(codeStart + 10)};
    SendMessageW(previewWindow, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&codeRange));
    CHARFORMAT2W codeFormat{};
    codeFormat.cbSize = sizeof(codeFormat);
    codeFormat.dwMask = CFM_BACKCOLOR | CFM_COLOR | CFM_FACE;
    SendMessageW(previewWindow,
                 EM_GETCHARFORMAT,
                 TRUE,
                 reinterpret_cast<LPARAM>(&codeFormat));
    Require(codeFormat.crBackColor != previewBackground,
            "code block background does not separate from the preview surface");
    Require(LocalMarkdownPreview::HasReadableThemeContrast(
                codeFormat.crTextColor, codeFormat.crBackColor),
            "code block text has insufficient theme contrast");
    PARAFORMAT2 codeParagraph{};
    codeParagraph.cbSize = sizeof(codeParagraph);
    SendMessageW(previewWindow,
                 EM_GETPARAFORMAT,
                 0,
                 reinterpret_cast<LPARAM>(&codeParagraph));
    Require(codeParagraph.dxStartIndent > 0,
            "code block paragraph has no left inset");
    Require(codeParagraph.dxRightIndent > 0,
            "code block paragraph has no right inset");
    SendMessageW(previewWindow, WM_THEMECHANGED, 0, 0);
    SendMessageW(previewWindow,
                 WM_SETTINGCHANGE,
                 0,
                 reinterpret_cast<LPARAM>(L"ImmersiveColorSet"));
    Require(IsWindow(previewWindow) != FALSE,
            "preview window did not survive a live theme refresh");
    const LRESULT options = SendMessageW(previewWindow, EM_GETOPTIONS, 0, 0);
    Require((options & ECO_READONLY) != 0, "RichEdit is not read-only");
    Require(previewText.find(L"[图片: preview.png]") == std::wstring::npos,
            "safe local image remained a placeholder");
    IRichEditOle* richEditOle = nullptr;
    Require(SendMessageW(previewWindow, EM_GETOLEINTERFACE, 0,
                         reinterpret_cast<LPARAM>(&richEditOle)) != FALSE &&
                richEditOle != nullptr,
            "local image object collection is unavailable");
    Require(richEditOle->GetObjectCount() == 1,
            "safe local image was not inserted as a visible object");
    REOBJECT imageObject{};
    imageObject.cbStruct = sizeof(imageObject);
    RequireHr(richEditOle->GetObject(0, &imageObject, REO_GETOBJ_NO_INTERFACES),
              S_OK,
              "safe local image object metadata is unavailable");
    Require(imageObject.sizel.cx >= 8400 && imageObject.sizel.cy >= 3100,
            "safe local image was permanently shrunk by transient pane layout");
    richEditOle->Release();

    RequireHr(preview->SetFocus(), S_OK, "SetFocus failed");
    HWND focusedWindow = nullptr;
    RequireHr(preview->QueryFocus(&focusedWindow), S_OK, "QueryFocus failed");
    Require(focusedWindow == previewWindow, "preview did not receive focus");
    RequireHr(preview->TranslateAccelerator(&message), S_FALSE,
              "accelerator without a site was incorrectly consumed");

    RequireHr(preview->SetRect(&rect), S_OK, "SetRect after preview failed");
    RequireHr(preview->Unload(), S_OK, "Unload failed");
    RequireHr(oleWindow->GetWindow(&previewWindow), E_FAIL, "preview window survived Unload");

    const std::wstring missingPath = imageFixture.DocumentPath() + L".missing";
    RequireHr(fileInitializer->Initialize(missingPath.c_str(), STGM_READ), S_OK,
              "Initialize rejected a temporarily missing file");
    RequireHr(preview->SetWindow(parent, &rect), S_OK, "SetWindow for missing file failed");
    RequireHr(preview->DoPreview(), S_OK, "missing file did not produce an in-pane error");
    Require(ReadWindowText([](IOleWindow* windowInterface) { HWND window = nullptr; windowInterface->GetWindow(&window); return window; }(oleWindow)).find(L"无法读取") != std::wstring::npos,
            "missing-file error was not displayed");
    RequireHr(preview->Unload(), S_OK, "missing-file Unload failed");

    const std::wstring invalidUtf16Path = imageFixture.CreateNamedFile(
        L"invalid-utf16.md", std::vector<BYTE>{0xFF, 0xFE, 0x00, 0xD8});
    RequireHr(fileInitializer->Initialize(invalidUtf16Path.c_str(), STGM_READ), S_OK,
              "Initialize failed for invalid UTF-16 test");
    RequireHr(preview->SetWindow(parent, &rect), S_OK,
              "SetWindow failed for invalid UTF-16 test");
    RequireHr(preview->DoPreview(), S_OK,
              "invalid UTF-16 did not produce an in-pane error state");
    HWND invalidUtf16Window = nullptr;
    RequireHr(oleWindow->GetWindow(&invalidUtf16Window), S_OK,
              "invalid UTF-16 error did not create a preview area");
    Require(ReadWindowText(invalidUtf16Window).find(L"编码") != std::wstring::npos,
            "invalid UTF-16 error was not displayed in the preview area");
    RequireHr(preview->Unload(), S_OK, "invalid UTF-16 Unload failed");

    IUnknown* deniedUnknown = nullptr;
    RequireHr(factory->CreateInstance(nullptr, IID_IUnknown, reinterpret_cast<void**>(&deniedUnknown)), S_OK,
              "CreateInstance for access-mode test failed");
    IInitializeWithFile* deniedInitializer = nullptr;
    RequireHr(deniedUnknown->QueryInterface(
                  __uuidof(IInitializeWithFile), reinterpret_cast<void**>(&deniedInitializer)),
              S_OK, "access-mode initializer QI failed");
    RequireHr(deniedInitializer->Initialize(imageFixture.DocumentPath().c_str(), STGM_WRITE), STG_E_ACCESSDENIED,
              "STGM_WRITE was not rejected");
    deniedInitializer->Release();
    deniedUnknown->Release();

    IUnknown* oversizedUnknown = nullptr;
    RequireHr(factory->CreateInstance(nullptr, IID_IUnknown, reinterpret_cast<void**>(&oversizedUnknown)), S_OK,
              "CreateInstance for size test failed");
    IPreviewHandler* oversizedPreview = nullptr;
    IInitializeWithFile* oversizedInitializer = nullptr;
    IOleWindow* oversizedOleWindow = nullptr;
    RequireHr(oversizedUnknown->QueryInterface(__uuidof(IPreviewHandler),
                                                reinterpret_cast<void**>(&oversizedPreview)),
              S_OK, "size test preview QI failed");
    RequireHr(oversizedUnknown->QueryInterface(__uuidof(IInitializeWithFile),
                                                reinterpret_cast<void**>(&oversizedInitializer)),
              S_OK, "size test initializer QI failed");
    RequireHr(oversizedUnknown->QueryInterface(__uuidof(IOleWindow),
                                                reinterpret_cast<void**>(&oversizedOleWindow)),
              S_OK, "size test window QI failed");
    const std::wstring oversizedPath = imageFixture.CreateNamedFile(
        L"oversized.md", FilledBytes(kMaxPreviewBytes + 1, 'x'));
    RequireHr(oversizedInitializer->Initialize(oversizedPath.c_str(), STGM_READ), S_OK,
              "oversized file Initialize failed");
    RequireHr(oversizedPreview->SetWindow(parent, &rect), S_OK, "size test SetWindow failed");
    RequireHr(oversizedPreview->DoPreview(), S_OK,
              "oversized stream did not produce an in-pane error state");
    HWND oversizedWindow = nullptr;
    RequireHr(oversizedOleWindow->GetWindow(&oversizedWindow), S_OK,
              "size error did not create a preview area");
    Require(ReadWindowText(oversizedWindow).find(L"5 MiB") != std::wstring::npos,
            "size error was not displayed in the preview area");
    RequireHr(oversizedPreview->Unload(), S_OK, "size test Unload failed");
    oversizedOleWindow->Release();
    oversizedInitializer->Release();
    oversizedPreview->Release();
    oversizedUnknown->Release();

    RequireHr(preview->Unload(), S_OK, "final handler Unload failed");
    visuals->Release();
    fileInitializer->Release();
    oleWindow->Release();
    preview->Release();
    site->Release();
    unknown->Release();
    RequireHr(canUnload(), S_FALSE, "factory was unexpectedly released");
    factory->Release();
    RequireHr(canUnload(), S_OK, "DLL remained pinned after all COM references were released");
    DestroyWindow(parent);
    FreeLibrary(module);
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: NativeSmokeTests.exe <LocalMarkdownPreview.dll>\n");
        return 2;
    }

    HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comHr)) {
        std::fprintf(stderr, "CoInitializeEx failed: 0x%08lX\n", static_cast<unsigned long>(comHr));
        return 1;
    }

    int exitCode = 0;
    try {
        RunSmoke(argv[1]);
        std::printf("PASS: native COM factory, file gating, local images, visuals, focus, and lifecycle\n");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        exitCode = 1;
    }

    CoUninitialize();
    return exitCode;
}

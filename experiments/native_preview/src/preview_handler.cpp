#include "preview_handler.h"

#include <algorithm>
#include <limits>
#include <new>
#include <utility>

namespace {

constexpr ULONG kReadChunkBytes = 64u * 1024u;

HRESULT HResultFromErrorCode(DWORD error) noexcept {
    return HRESULT_FROM_WIN32(error == ERROR_SUCCESS ? ERROR_FUNCTION_FAILED : error);
}

template <typename T>
void ReleaseInterface(T*& value) noexcept {
    if (value != nullptr) {
        value->Release();
        value = nullptr;
    }
}

} // namespace

NativePreviewHandler::NativePreviewHandler() noexcept
    : _referenceCount(1),
      _parentWindow(nullptr),
      _previewRect{},
      _previewWindow(nullptr),
      _richEditModule(nullptr),
      _site(nullptr),
      _stream(nullptr),
      _streamMode(0),
      _initialized(false) {
    ModuleAddRef();
}

NativePreviewHandler::~NativePreviewHandler() {
    if (_previewWindow != nullptr) {
        DestroyWindow(_previewWindow);
        _previewWindow = nullptr;
    }

    if (_richEditModule != nullptr) {
        FreeLibrary(_richEditModule);
        _richEditModule = nullptr;
    }

    ReleaseInterface(_site);
    ReleaseInterface(_stream);
    ModuleRelease();
}

STDMETHODIMP NativePreviewHandler::QueryInterface(REFIID riid, void** result) {
    if (result == nullptr) {
        return E_POINTER;
    }
    *result = nullptr;

    if (riid == IID_IUnknown) {
        *result = static_cast<IUnknown*>(static_cast<IObjectWithSite*>(this));
    } else if (riid == __uuidof(IObjectWithSite)) {
        *result = static_cast<IObjectWithSite*>(this);
    } else if (riid == __uuidof(IPreviewHandler)) {
        *result = static_cast<IPreviewHandler*>(this);
    } else if (riid == __uuidof(IOleWindow)) {
        *result = static_cast<IOleWindow*>(this);
    } else if (riid == __uuidof(IInitializeWithStream)) {
        *result = static_cast<IInitializeWithStream*>(this);
    } else {
        return E_NOINTERFACE;
    }

    static_cast<IUnknown*>(static_cast<IObjectWithSite*>(this))->AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) NativePreviewHandler::AddRef() {
    return static_cast<ULONG>(InterlockedIncrement(&_referenceCount));
}

STDMETHODIMP_(ULONG) NativePreviewHandler::Release() {
    const LONG remaining = InterlockedDecrement(&_referenceCount);
    if (remaining == 0) {
        delete this;
        return 0;
    }
    return remaining > 0 ? static_cast<ULONG>(remaining) : 0;
}

STDMETHODIMP NativePreviewHandler::SetSite(IUnknown* site) {
    IUnknown* replacement = nullptr;
    if (site != nullptr) {
        HRESULT hr = site->QueryInterface(IID_PPV_ARGS(&replacement));
        if (FAILED(hr)) {
            return hr;
        }
    }

    ReleaseInterface(_site);
    _site = replacement;
    return S_OK;
}

STDMETHODIMP NativePreviewHandler::GetSite(REFIID riid, void** result) {
    if (result == nullptr) {
        return E_POINTER;
    }
    *result = nullptr;
    if (_site == nullptr) {
        return E_NOINTERFACE;
    }
    return _site->QueryInterface(riid, result);
}

STDMETHODIMP NativePreviewHandler::GetWindow(HWND* window) {
    if (window == nullptr) {
        return E_POINTER;
    }
    *window = nullptr;
    if (_previewWindow == nullptr || !IsWindow(_previewWindow)) {
        return E_FAIL;
    }
    *window = _previewWindow;
    return S_OK;
}

STDMETHODIMP NativePreviewHandler::ContextSensitiveHelp(BOOL) {
    return E_NOTIMPL;
}

STDMETHODIMP NativePreviewHandler::Initialize(IStream* stream, DWORD mode) {
    if (stream == nullptr) {
        return E_INVALIDARG;
    }
    if (_previewWindow != nullptr) {
        return E_UNEXPECTED;
    }

    stream->AddRef();
    ReleaseInterface(_stream);
    _stream = stream;
    _streamMode = mode;
    _initialized = true;
    return S_OK;
}

STDMETHODIMP NativePreviewHandler::SetWindow(HWND parent, const RECT* rect) {
    if (parent == nullptr || rect == nullptr || !IsWindow(parent) || !IsRectUsable(*rect)) {
        return E_INVALIDARG;
    }

    _parentWindow = parent;
    _previewRect = *rect;
    if (_previewWindow == nullptr) {
        return S_OK;
    }

    SetLastError(ERROR_SUCCESS);
    const HWND previousParent = SetParent(_previewWindow, _parentWindow);
    if (previousParent == nullptr && GetLastError() != ERROR_SUCCESS) {
        return LastErrorAsHResult();
    }
    return MovePreviewWindow();
}

STDMETHODIMP NativePreviewHandler::SetRect(const RECT* rect) {
    if (rect == nullptr || !IsRectUsable(*rect)) {
        return E_INVALIDARG;
    }

    const RECT previousRect = _previewRect;
    _previewRect = *rect;
    const HRESULT hr = MovePreviewWindow();
    if (FAILED(hr)) {
        _previewRect = previousRect;
    }
    return hr;
}

STDMETHODIMP NativePreviewHandler::DoPreview() {
    if (!_initialized || _stream == nullptr) {
        return E_UNEXPECTED;
    }
    if (_previewWindow != nullptr) {
        return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
    }
    if (_parentWindow == nullptr || !IsWindow(_parentWindow)) {
        return E_UNEXPECTED;
    }

    std::wstring text;
    HRESULT hr = ReadPreviewText(&text);
    if (FAILED(hr)) {
        return hr;
    }
    return CreatePreviewWindow(text);
}

STDMETHODIMP NativePreviewHandler::SetFocus() {
    if (_previewWindow == nullptr || !IsWindow(_previewWindow)) {
        return E_FAIL;
    }

    ::SetFocus(_previewWindow);
    return GetFocus() == _previewWindow ? S_OK : E_FAIL;
}

STDMETHODIMP NativePreviewHandler::QueryFocus(HWND* window) {
    if (window == nullptr) {
        return E_POINTER;
    }
    *window = GetFocus();
    return *window == nullptr ? S_FALSE : S_OK;
}

STDMETHODIMP NativePreviewHandler::TranslateAccelerator(MSG* message) {
    if (message == nullptr) {
        return E_INVALIDARG;
    }
    if (_site == nullptr) {
        return S_FALSE;
    }

    IPreviewHandlerFrame* frame = nullptr;
    HRESULT hr = _site->QueryInterface(IID_PPV_ARGS(&frame));
    if (hr == E_NOINTERFACE) {
        return S_FALSE;
    }
    if (FAILED(hr)) {
        return hr;
    }

    hr = frame->TranslateAccelerator(message);
    frame->Release();
    return hr;
}

STDMETHODIMP NativePreviewHandler::Unload() {
    HRESULT hr = DestroyPreviewWindow();

    ReleaseInterface(_stream);
    _streamMode = 0;
    _initialized = false;

    if (_previewWindow == nullptr && _richEditModule != nullptr) {
        HMODULE module = _richEditModule;
        _richEditModule = nullptr;
        if (!FreeLibrary(module)) {
            _richEditModule = module;
            if (SUCCEEDED(hr)) {
                hr = LastErrorAsHResult();
            }
        }
    }
    return hr;
}

HRESULT NativePreviewHandler::CreatePreviewWindow(const std::wstring& text) noexcept {
    if (_parentWindow == nullptr || !IsWindow(_parentWindow) || !IsRectUsable(_previewRect)) {
        return E_UNEXPECTED;
    }

    if (_richEditModule == nullptr) {
        _richEditModule = LoadLibraryW(L"Msftedit.dll");
        if (_richEditModule == nullptr) {
            return LastErrorAsHResult();
        }
    }

    const int width = _previewRect.right - _previewRect.left;
    const int height = _previewRect.bottom - _previewRect.top;
    _previewWindow = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        MSFTEDIT_CLASS,
        L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY |
            ES_AUTOVSCROLL | ES_AUTOHSCROLL,
        _previewRect.left,
        _previewRect.top,
        width,
        height,
        _parentWindow,
        nullptr,
        g_module_instance,
        nullptr);
    if (_previewWindow == nullptr) {
        const HRESULT hr = LastErrorAsHResult();
        return hr;
    }

    SendMessageW(_previewWindow, EM_EXLIMITTEXT, 0, static_cast<LPARAM>(kMaxPreviewBytes));
    if (SendMessageW(_previewWindow, EM_SETREADONLY, TRUE, 0) == FALSE) {
        const HRESULT hr = LastErrorAsHResult();
        DestroyWindow(_previewWindow);
        _previewWindow = nullptr;
        return hr;
    }

    const HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    if (font != nullptr) {
        SendMessageW(_previewWindow, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }

    if (!SetWindowTextW(_previewWindow, text.c_str())) {
        const HRESULT hr = LastErrorAsHResult();
        DestroyWindow(_previewWindow);
        _previewWindow = nullptr;
        return hr;
    }

    const HRESULT hr = MovePreviewWindow();
    if (FAILED(hr)) {
        DestroyWindow(_previewWindow);
        _previewWindow = nullptr;
        return hr;
    }
    return S_OK;
}

HRESULT NativePreviewHandler::DestroyPreviewWindow() noexcept {
    if (_previewWindow == nullptr) {
        return S_OK;
    }

    const HWND window = _previewWindow;
    SetLastError(ERROR_SUCCESS);
    if (!DestroyWindow(window) && IsWindow(window)) {
        return LastErrorAsHResult();
    }
    _previewWindow = nullptr;
    return S_OK;
}

HRESULT NativePreviewHandler::MovePreviewWindow() noexcept {
    if (_previewWindow == nullptr) {
        return S_OK;
    }
    if (!IsWindow(_previewWindow) || _parentWindow == nullptr || !IsWindow(_parentWindow) ||
        !IsRectUsable(_previewRect)) {
        return E_UNEXPECTED;
    }

    const int width = _previewRect.right - _previewRect.left;
    const int height = _previewRect.bottom - _previewRect.top;
    if (!SetWindowPos(
            _previewWindow,
            nullptr,
            _previewRect.left,
            _previewRect.top,
            width,
            height,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW)) {
        return LastErrorAsHResult();
    }
    return S_OK;
}

HRESULT NativePreviewHandler::ReadPreviewText(std::wstring* text) const noexcept {
    if (!_initialized || _stream == nullptr || text == nullptr) {
        return E_INVALIDARG;
    }
    return ReadStreamText(_stream, text);
}

HRESULT NativePreviewHandler::ReadStreamText(IStream* stream, std::wstring* text) noexcept {
    if (stream == nullptr || text == nullptr) {
        return E_INVALIDARG;
    }

    STATSTG statistics{};
    HRESULT hr = stream->Stat(&statistics, STATFLAG_NONAME);
    if (FAILED(hr)) {
        return hr;
    }
    if (statistics.cbSize.QuadPart > static_cast<ULONGLONG>(kMaxPreviewBytes)) {
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    }

    const size_t byteCount = static_cast<size_t>(statistics.cbSize.QuadPart);
    std::vector<BYTE> bytes;
    try {
        bytes.resize(byteCount);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }

    LARGE_INTEGER origin{};
    ULARGE_INTEGER newPosition{};
    hr = stream->Seek(origin, STREAM_SEEK_SET, &newPosition);
    if (FAILED(hr)) {
        return hr;
    }

    size_t offset = 0;
    while (offset < byteCount) {
        const size_t remaining = byteCount - offset;
        const ULONG requested = static_cast<ULONG>(std::min(remaining, static_cast<size_t>(kReadChunkBytes)));
        ULONG read = 0;
        hr = stream->Read(bytes.data() + offset, requested, &read);
        if (FAILED(hr)) {
            return hr;
        }
        if (read == 0 || read > requested) {
            return STG_E_READFAULT;
        }
        offset += read;
    }

    return ConvertBytesToText(bytes, text);
}

HRESULT NativePreviewHandler::ConvertBytesToText(const std::vector<BYTE>& bytes,
                                                  std::wstring* text) noexcept {
    if (text == nullptr) {
        return E_INVALIDARG;
    }

    try {
        size_t offset = 0;
        if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
            offset = 3;
        }

        if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
            offset = 2;
            const size_t payloadBytes = bytes.size() - offset;
            if ((payloadBytes % 2) != 0) {
                return HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION);
            }
            std::wstring converted(payloadBytes / 2, L'\0');
            for (size_t index = 0; index < converted.size(); ++index) {
                const size_t byteIndex = offset + (index * 2);
                converted[index] = static_cast<wchar_t>(
                    static_cast<unsigned int>(bytes[byteIndex]) |
                    (static_cast<unsigned int>(bytes[byteIndex + 1]) << 8));
            }
            std::replace(converted.begin(), converted.end(), L'\0', L'\xFFFD');
            *text = std::move(converted);
            return S_OK;
        }

        if (bytes.size() >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) {
            offset = 2;
            const size_t payloadBytes = bytes.size() - offset;
            if ((payloadBytes % 2) != 0) {
                return HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION);
            }
            std::wstring converted(payloadBytes / 2, L'\0');
            for (size_t index = 0; index < converted.size(); ++index) {
                const size_t byteIndex = offset + (index * 2);
                converted[index] = static_cast<wchar_t>(
                    (static_cast<unsigned int>(bytes[byteIndex]) << 8) |
                    static_cast<unsigned int>(bytes[byteIndex + 1]));
            }
            std::replace(converted.begin(), converted.end(), L'\0', L'\xFFFD');
            *text = std::move(converted);
            return S_OK;
        }

        const int sourceLength = static_cast<int>(bytes.size() - offset);
        if (sourceLength == 0) {
            text->clear();
            return S_OK;
        }

        int required = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            reinterpret_cast<LPCCH>(bytes.data() + offset),
            sourceLength,
            nullptr,
            0);
        UINT codePage = CP_UTF8;
        DWORD flags = MB_ERR_INVALID_CHARS;
        if (required == 0) {
            const DWORD error = GetLastError();
            if (error != ERROR_NO_UNICODE_TRANSLATION) {
                return HResultFromErrorCode(error);
            }
            codePage = CP_ACP;
            flags = 0;
            required = MultiByteToWideChar(
                codePage,
                flags,
                reinterpret_cast<LPCCH>(bytes.data() + offset),
                sourceLength,
                nullptr,
                0);
            if (required == 0) {
                return LastErrorAsHResult();
            }
        }

        std::wstring converted(static_cast<size_t>(required), L'\0');
        const int written = MultiByteToWideChar(
            codePage,
            flags,
            reinterpret_cast<LPCCH>(bytes.data() + offset),
            sourceLength,
            converted.data(),
            required);
        if (written != required) {
            return LastErrorAsHResult();
        }
        std::replace(converted.begin(), converted.end(), L'\0', L'\xFFFD');
        *text = std::move(converted);
        return S_OK;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
}

HRESULT NativePreviewHandler::LastErrorAsHResult() noexcept {
    return HResultFromErrorCode(GetLastError());
}

bool NativePreviewHandler::IsRectUsable(const RECT& rect) noexcept {
    const LONGLONG width = static_cast<LONGLONG>(rect.right) - static_cast<LONGLONG>(rect.left);
    const LONGLONG height = static_cast<LONGLONG>(rect.bottom) - static_cast<LONGLONG>(rect.top);
    return width >= 0 && height >= 0 && width <= static_cast<LONGLONG>(std::numeric_limits<int>::max()) &&
           height <= static_cast<LONGLONG>(std::numeric_limits<int>::max());
}

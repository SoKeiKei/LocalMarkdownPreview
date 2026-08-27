#include "LocalMarkdownPreview.h"
#include "SafeImageLoader.h"

#include <shlwapi.h>
#include <shlobj_core.h>
#include <richole.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string_view>
#include <utility>

namespace {

constexpr ULONG kReadChunkBytes = 64u * 1024u;
constexpr UINT kCodePageGb18030 = 54936u;
constexpr size_t kMaxSourceNameChars = 512u;
constexpr wchar_t kGenericPreviewError[] = L"无法读取此文件的预览。";
constexpr wchar_t kTooLargePreviewError[] = L"文件超过 5 MiB，无法预览。";
constexpr wchar_t kEncodingPreviewError[] = L"文件编码无法识别。";
constexpr wchar_t kMarkdownLimitPreviewError[] = L"文档结构过于复杂，已停止预览。";
constexpr wchar_t kMarkdownTimeoutPreviewError[] = L"文档解析超时，已停止预览。";
constexpr wchar_t kMarkdownCancelledPreviewError[] = L"预览已取消。";

bool IsPreviewCancelled(void* context) noexcept {
    if (context == nullptr) {
        return false;
    }
    auto* flag = static_cast<LONG*>(context);
    return InterlockedCompareExchange(flag, 0, 0) != 0;
}

const wchar_t* MarkdownErrorPreviewText(
    LocalMarkdownPreview::MarkdownParseStatus status) noexcept {
    using LocalMarkdownPreview::MarkdownParseStatus;
    switch (status) {
    case MarkdownParseStatus::NodeLimitExceeded:
    case MarkdownParseStatus::OutputLimitExceeded:
    case MarkdownParseStatus::InputTooLarge:
        return kMarkdownLimitPreviewError;
    case MarkdownParseStatus::TimeLimitExceeded:
        return kMarkdownTimeoutPreviewError;
    case MarkdownParseStatus::Cancelled:
        return kMarkdownCancelledPreviewError;
    default:
        return kGenericPreviewError;
    }
}

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

bool IsReadWriteMode(DWORD mode) noexcept {
    return (mode & (STGM_WRITE | STGM_READWRITE)) != 0;
}

bool IsValidUtf16(std::wstring_view text) noexcept {
    for (std::size_t index = 0; index < text.size(); ++index) {
        const wchar_t value = text[index];
        if (value >= 0xD800 && value <= 0xDBFF) {
            if (index + 1 >= text.size() || text[index + 1] < 0xDC00 ||
                text[index + 1] > 0xDFFF) {
                return false;
            }
            ++index;
        } else if (value >= 0xDC00 && value <= 0xDFFF) {
            return false;
        }
    }
    return true;
}

const wchar_t* ErrorPreviewText(HRESULT hr) noexcept {
    if (hr == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE)) {
        return kTooLargePreviewError;
    }
    if (hr == HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION)) {
        return kEncodingPreviewError;
    }
    return kGenericPreviewError;
}

class BitmapDataObject final : public IDataObject {
public:
    explicit BitmapDataObject(HBITMAP bitmap) noexcept
        : _referenceCount(1), _bitmap(bitmap) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** result) override {
        if (result == nullptr) return E_POINTER;
        *result = nullptr;
        if (riid == IID_IUnknown || riid == IID_IDataObject) {
            *result = static_cast<IDataObject*>(this);
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
        if (remaining == 0) { delete this; return 0; }
        return static_cast<ULONG>(remaining);
    }
    STDMETHODIMP GetData(FORMATETC* format, STGMEDIUM* medium) override {
        if (format == nullptr || medium == nullptr) return E_POINTER;
        if (format->cfFormat != CF_BITMAP || (format->tymed & TYMED_GDI) == 0 ||
            (format->dwAspect & DVASPECT_CONTENT) == 0) return DV_E_FORMATETC;
        HBITMAP duplicate = static_cast<HBITMAP>(CopyImage(
            _bitmap, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));
        if (duplicate == nullptr) return E_OUTOFMEMORY;
        medium->tymed = TYMED_GDI;
        medium->hBitmap = duplicate;
        medium->pUnkForRelease = nullptr;
        return S_OK;
    }
    STDMETHODIMP GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    STDMETHODIMP QueryGetData(FORMATETC* format) override {
        if (format == nullptr) return E_POINTER;
        return format->cfFormat == CF_BITMAP && (format->tymed & TYMED_GDI) != 0
            ? S_OK : DV_E_FORMATETC;
    }
    STDMETHODIMP GetCanonicalFormatEtc(FORMATETC*, FORMATETC* output) override {
        if (output == nullptr) return E_POINTER;
        output->ptd = nullptr;
        return E_NOTIMPL;
    }
    STDMETHODIMP SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    STDMETHODIMP EnumFormatEtc(DWORD direction, IEnumFORMATETC** result) override {
        if (result == nullptr) return E_POINTER;
        *result = nullptr;
        if (direction != DATADIR_GET) return E_NOTIMPL;
        FORMATETC format{CF_BITMAP, nullptr, DVASPECT_CONTENT, -1, TYMED_GDI};
        return SHCreateStdEnumFmtEtc(1, &format, result);
    }
    STDMETHODIMP DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    STDMETHODIMP DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    STDMETHODIMP EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }

private:
    ~BitmapDataObject() {
        if (_bitmap != nullptr) DeleteObject(_bitmap);
    }
    LONG _referenceCount;
    HBITMAP _bitmap;
};

HBITMAP CreateImageBitmap(const LocalMarkdownPreview::SafeImageLoadResult& image) noexcept {
    if (!image.Succeeded() || image.width == 0 || image.height == 0 ||
        image.stride != image.width * 4u ||
        image.pixels.size() != static_cast<std::size_t>(image.stride) * image.height) {
        return nullptr;
    }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(image.width);
    info.bmiHeader.biHeight = -static_cast<LONG>(image.height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (screen != nullptr) ReleaseDC(nullptr, screen);
    if (bitmap != nullptr && bits != nullptr) {
        std::memcpy(bits, image.pixels.data(), image.pixels.size());
    }
    return bitmap;
}

HRESULT InsertBitmapObject(HWND richEdit,
                           const LocalMarkdownPreview::SafeImageLoadResult& image,
                           LONG widthHimetric,
                           LONG heightHimetric) noexcept {
    HBITMAP bitmap = CreateImageBitmap(image);
    if (bitmap == nullptr) return E_OUTOFMEMORY;
    auto* data = new (std::nothrow) BitmapDataObject(bitmap);
    if (data == nullptr) { DeleteObject(bitmap); return E_OUTOFMEMORY; }

    IRichEditOle* richOle = nullptr;
    if (SendMessageW(richEdit, EM_GETOLEINTERFACE, 0,
                     reinterpret_cast<LPARAM>(&richOle)) == FALSE || richOle == nullptr) {
        data->Release();
        return E_NOINTERFACE;
    }
    IOleClientSite* clientSite = nullptr;
    ILockBytes* lockBytes = nullptr;
    IStorage* storage = nullptr;
    IOleObject* object = nullptr;
    HRESULT hr = richOle->GetClientSite(&clientSite);
    if (SUCCEEDED(hr)) hr = CreateILockBytesOnHGlobal(nullptr, TRUE, &lockBytes);
    if (SUCCEEDED(hr)) hr = StgCreateDocfileOnILockBytes(
        lockBytes, STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, &storage);
    FORMATETC format{CF_BITMAP, nullptr, DVASPECT_CONTENT, -1, TYMED_GDI};
    if (SUCCEEDED(hr)) hr = OleCreateStaticFromData(
        data, IID_IOleObject, OLERENDER_FORMAT, &format, clientSite, storage,
        reinterpret_cast<void**>(&object));
    if (SUCCEEDED(hr)) hr = OleSetContainedObject(object, TRUE);
    REOBJECT reobject{};
    if (SUCCEEDED(hr)) {
        reobject.cbStruct = sizeof(reobject);
        reobject.cp = REO_CP_SELECTION;
        object->GetUserClassID(&reobject.clsid);
        reobject.poleobj = object;
        reobject.pstg = storage;
        reobject.polesite = clientSite;
        reobject.sizel.cx = widthHimetric;
        reobject.sizel.cy = heightHimetric;
        reobject.dvaspect = DVASPECT_CONTENT;
        reobject.dwFlags = REO_BELOWBASELINE;
        hr = richOle->InsertObject(&reobject);
    }
    if (object != nullptr) object->Release();
    if (storage != nullptr) storage->Release();
    if (lockBytes != nullptr) lockBytes->Release();
    if (clientSite != nullptr) clientSite->Release();
    richOle->Release();
    data->Release();
    return hr;
}

std::size_t MapPositionAfterImage(std::size_t position,
                                  std::size_t imageStart,
                                  std::size_t imageLength) noexcept {
    const std::size_t imageEnd = imageStart + imageLength;
    if (position <= imageStart) {
        return position;
    }
    if (position >= imageEnd) {
        return position - (imageLength - 1);
    }
    return imageStart + 1;
}

template <typename Range>
void AdjustRangeAfterImage(Range& range,
                           std::size_t imageStart,
                           std::size_t imageLength) noexcept {
    const std::size_t oldStart = range.start;
    const std::size_t oldEnd = oldStart + range.length;
    range.start = MapPositionAfterImage(oldStart, imageStart, imageLength);
    const std::size_t newEnd = MapPositionAfterImage(oldEnd, imageStart, imageLength);
    range.length = newEnd >= range.start ? newEnd - range.start : 0;
}

void AdjustDocumentAfterImage(LocalMarkdownPreview::MarkdownDocumentResult& document,
                              std::size_t imageStart,
                              std::size_t imageLength) {
    if (imageLength == 0 || imageStart > document.visible_text.size() ||
        imageLength > document.visible_text.size() - imageStart) {
        return;
    }
    document.visible_text.replace(imageStart, imageLength, 1, L'\uFFFC');
    document.output_char_count = document.visible_text.size();
    for (auto& range : document.formats) {
        AdjustRangeAfterImage(range, imageStart, imageLength);
    }
    for (auto& link : document.links) {
        AdjustRangeAfterImage(link, imageStart, imageLength);
    }
    for (auto& image : document.images) {
        AdjustRangeAfterImage(image, imageStart, imageLength);
    }
}

} // namespace

LocalMarkdownPreviewHandler::LocalMarkdownPreviewHandler() noexcept
    : _referenceCount(1),
      _parentWindow(nullptr),
      _previewRect{},
      _previewWindow(nullptr),
      _richEditModule(nullptr),
      _site(nullptr),
      _streamMode(0),
      _initialized(false),
      _backgroundColor(GetSysColor(COLOR_WINDOW)),
      _textColor(GetSysColor(COLOR_WINDOWTEXT)),
      _font{},
      _fontSet(false),
      _fontHandle(nullptr),
      _cancelRequested(0),
      _sourceName() {
    ModuleAddRef();
}

LocalMarkdownPreviewHandler::~LocalMarkdownPreviewHandler() {
    if (_previewWindow != nullptr) {
        DestroyWindow(_previewWindow);
        _previewWindow = nullptr;
    }

    if (_fontHandle != nullptr) {
        DeleteObject(_fontHandle);
        _fontHandle = nullptr;
    }

    if (_richEditModule != nullptr) {
        FreeLibrary(_richEditModule);
        _richEditModule = nullptr;
    }

    ReleaseInterface(_site);
    ModuleRelease();
}

STDMETHODIMP LocalMarkdownPreviewHandler::QueryInterface(REFIID riid, void** result) {
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
    } else if (riid == __uuidof(IInitializeWithFile)) {
        *result = static_cast<IInitializeWithFile*>(this);
    } else if (riid == __uuidof(IPreviewHandlerVisuals)) {
        *result = static_cast<IPreviewHandlerVisuals*>(this);
    } else {
        return E_NOINTERFACE;
    }

    static_cast<IUnknown*>(static_cast<IObjectWithSite*>(this))->AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) LocalMarkdownPreviewHandler::AddRef() {
    return static_cast<ULONG>(InterlockedIncrement(&_referenceCount));
}

STDMETHODIMP_(ULONG) LocalMarkdownPreviewHandler::Release() {
    const LONG remaining = InterlockedDecrement(&_referenceCount);
    if (remaining == 0) {
        delete this;
        return 0;
    }
    return remaining > 0 ? static_cast<ULONG>(remaining) : 0;
}

STDMETHODIMP LocalMarkdownPreviewHandler::SetSite(IUnknown* site) {
    IUnknown* replacement = nullptr;
    if (site != nullptr) {
        const HRESULT hr = site->QueryInterface(IID_PPV_ARGS(&replacement));
        if (FAILED(hr)) {
            return hr;
        }
    }

    ReleaseInterface(_site);
    _site = replacement;
    return S_OK;
}

STDMETHODIMP LocalMarkdownPreviewHandler::GetSite(REFIID riid, void** result) {
    if (result == nullptr) {
        return E_POINTER;
    }
    *result = nullptr;
    if (_site == nullptr) {
        return E_NOINTERFACE;
    }
    return _site->QueryInterface(riid, result);
}

STDMETHODIMP LocalMarkdownPreviewHandler::GetWindow(HWND* window) {
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

STDMETHODIMP LocalMarkdownPreviewHandler::ContextSensitiveHelp(BOOL) {
    return E_NOTIMPL;
}

STDMETHODIMP LocalMarkdownPreviewHandler::Initialize(LPCWSTR filePath, DWORD mode) {
    if (filePath == nullptr || filePath[0] == L'\0') {
        return E_INVALIDARG;
    }
    if (IsReadWriteMode(mode)) {
        return STG_E_ACCESSDENIED;
    }
    if (_initialized) {
        return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
    }
    const std::size_t length = wcsnlen_s(filePath, 32768);
    if (length == 0 || length >= 32768) {
        return E_INVALIDARG;
    }
    try {
        _sourceName.assign(filePath, length);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    _streamMode = mode;
    _initialized = true;
    InterlockedExchange(&_cancelRequested, 0);
    return S_OK;
}

STDMETHODIMP LocalMarkdownPreviewHandler::SetWindow(HWND parent, const RECT* rect) {
    if (parent == nullptr || rect == nullptr || !IsWindow(parent) || !IsRectUsable(*rect)) {
        return E_INVALIDARG;
    }

    if (_previewWindow != nullptr && !IsWindow(_previewWindow)) {
        _previewWindow = nullptr;
    }

    if (_previewWindow == nullptr) {
        _parentWindow = parent;
        _previewRect = *rect;
        return S_OK;
    }

    const HWND oldParent = GetParent(_previewWindow);
    const HWND oldMemberParent = _parentWindow;
    const RECT oldRect = _previewRect;

    SetLastError(ERROR_SUCCESS);
    if (SetParent(_previewWindow, parent) == nullptr && GetLastError() != ERROR_SUCCESS) {
        return LastErrorAsHResult();
    }

    _parentWindow = parent;
    _previewRect = *rect;
    const HRESULT hr = MovePreviewWindow();
    if (FAILED(hr)) {
        SetLastError(ERROR_SUCCESS);
        SetParent(_previewWindow, oldParent);
        _parentWindow = oldMemberParent;
        _previewRect = oldRect;
        return hr;
    }
    return S_OK;
}

STDMETHODIMP LocalMarkdownPreviewHandler::SetRect(const RECT* rect) {
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

STDMETHODIMP LocalMarkdownPreviewHandler::DoPreview() {
    if (!_initialized) {
        return E_UNEXPECTED;
    }
    if (_previewWindow != nullptr && !IsWindow(_previewWindow)) {
        _previewWindow = nullptr;
    }
    if (_previewWindow != nullptr) {
        return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
    }
    if (_parentWindow == nullptr || !IsWindow(_parentWindow) || !IsRectUsable(_previewRect)) {
        return E_UNEXPECTED;
    }

    std::wstring text;
    const HRESULT readHr = ReadPreviewText(&text);
    if (FAILED(readHr)) {
        const HRESULT displayHr = CreatePreviewWindow(ErrorPreviewText(readHr));
        return displayHr;
    }
    LocalMarkdownPreview::MarkdownDocumentLimits limits;
    limits.max_nodes = 500000;
    limits.max_output_chars = kMaxPreviewBytes;
    limits.max_parse_milliseconds = 1200;
    limits.should_cancel = &IsPreviewCancelled;
    limits.cancellation_context = &_cancelRequested;
    LocalMarkdownPreview::MarkdownDocumentResult document =
        LocalMarkdownPreview::ParseMarkdownDocument(text, limits);
    if (!document.Succeeded()) {
        _document = {};
        return CreatePreviewWindow(MarkdownErrorPreviewText(document.status));
    }
    return CreateMarkdownPreviewWindow(std::move(document));
}

STDMETHODIMP LocalMarkdownPreviewHandler::SetFocus() {
    if (_previewWindow == nullptr || !IsWindow(_previewWindow)) {
        return E_FAIL;
    }

    ::SetFocus(_previewWindow);
    return GetFocus() == _previewWindow ? S_OK : E_FAIL;
}

STDMETHODIMP LocalMarkdownPreviewHandler::QueryFocus(HWND* window) {
    if (window == nullptr) {
        return E_POINTER;
    }
    *window = GetFocus();
    return *window == nullptr ? S_FALSE : S_OK;
}

STDMETHODIMP LocalMarkdownPreviewHandler::TranslateAccelerator(MSG* message) {
    if (message == nullptr) {
        return E_INVALIDARG;
    }

    if (_site != nullptr) {
        IPreviewHandlerFrame* frame = nullptr;
        const HRESULT queryHr = _site->QueryInterface(IID_PPV_ARGS(&frame));
        if (SUCCEEDED(queryHr)) {
            const HRESULT frameHr = frame->TranslateAccelerator(message);
            frame->Release();
            if (frameHr != S_FALSE) {
                return frameHr;
            }
        } else if (queryHr != E_NOINTERFACE) {
            return queryHr;
        }
    }

    if (_previewWindow != nullptr && IsWindow(_previewWindow) && message->hwnd != nullptr &&
        (message->hwnd == _previewWindow || IsChild(_previewWindow, message->hwnd)) &&
        (message->message == WM_KEYDOWN || message->message == WM_SYSKEYDOWN)) {
        const bool controlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (controlDown && (message->wParam == 'A' || message->wParam == 'a')) {
            SendMessageW(_previewWindow, EM_SETSEL, 0, -1);
            return S_OK;
        }
        if (controlDown && (message->wParam == 'C' || message->wParam == 'c')) {
            SendMessageW(_previewWindow, WM_COPY, 0, 0);
            return S_OK;
        }
    }
    return S_FALSE;
}

STDMETHODIMP LocalMarkdownPreviewHandler::Unload() {
    InterlockedExchange(&_cancelRequested, 1);
    HRESULT hr = DestroyPreviewWindow();

    _streamMode = 0;
    _initialized = false;
    _sourceName.clear();
    _document = {};
    _parentWindow = nullptr;
    _previewRect = RECT{};

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

STDMETHODIMP LocalMarkdownPreviewHandler::SetBackgroundColor(COLORREF color) {
    _backgroundColor = color;
    if (_previewWindow != nullptr && IsWindow(_previewWindow)) {
        SendMessageW(_previewWindow, EM_SETBKGNDCOLOR, FALSE, static_cast<LPARAM>(color));
        return ApplyMarkdownFormatting();
    }
    return S_OK;
}

STDMETHODIMP LocalMarkdownPreviewHandler::SetFont(const LOGFONTW* font) {
    if (font == nullptr) {
        return E_INVALIDARG;
    }

    HFONT replacement = CreateFontIndirectW(font);
    if (replacement == nullptr) {
        return LastErrorAsHResult();
    }

    HFONT previous = _fontHandle;
    _font = *font;
    _fontSet = true;
    _fontHandle = replacement;
    HRESULT formattingHr = S_OK;
    if (_previewWindow != nullptr && IsWindow(_previewWindow)) {
        SendMessageW(_previewWindow, WM_SETFONT, reinterpret_cast<WPARAM>(_fontHandle), TRUE);
        formattingHr = ApplyMarkdownFormatting();
    }
    if (previous != nullptr) {
        DeleteObject(previous);
    }
    return formattingHr;
}

STDMETHODIMP LocalMarkdownPreviewHandler::SetTextColor(COLORREF color) {
    _textColor = color;
    if (_previewWindow == nullptr || !IsWindow(_previewWindow)) {
        return S_OK;
    }
    const HRESULT colorHr = ApplyTextColor();
    return FAILED(colorHr) ? colorHr : ApplyMarkdownFormatting();
}

HRESULT LocalMarkdownPreviewHandler::CreateMarkdownPreviewWindow(
    LocalMarkdownPreview::MarkdownDocumentResult document) noexcept {
    _document = std::move(document);
    HRESULT hr = CreatePreviewWindow(_document.visible_text);
    if (SUCCEEDED(hr)) {
        hr = InsertLocalImages();
    }
    if (FAILED(hr)) {
        DestroyPreviewWindow();
        _document = {};
    }
    return hr;
}

HRESULT LocalMarkdownPreviewHandler::CreatePreviewWindow(const std::wstring& text) noexcept {
    if (_parentWindow == nullptr || !IsWindow(_parentWindow) || !IsRectUsable(_previewRect)) {
        return E_UNEXPECTED;
    }

    if (_richEditModule == nullptr) {
        _richEditModule = LoadLibraryExW(L"Msftedit.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
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
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY |
            ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_NOOLEDRAGDROP,
        _previewRect.left,
        _previewRect.top,
        width,
        height,
        _parentWindow,
        nullptr,
        g_module_instance,
        nullptr);
    if (_previewWindow == nullptr) {
        return LastErrorAsHResult();
    }

    SendMessageW(_previewWindow, EM_EXLIMITTEXT, 0, static_cast<LPARAM>(kMaxPreviewBytes));
    if (SendMessageW(_previewWindow, EM_SETREADONLY, TRUE, 0) == FALSE) {
        const HRESULT hr = LastErrorAsHResult();
        DestroyWindow(_previewWindow);
        _previewWindow = nullptr;
        return hr;
    }

    SendMessageW(_previewWindow, EM_SETBKGNDCOLOR, FALSE, static_cast<LPARAM>(_backgroundColor));
    if (_fontSet && _fontHandle != nullptr) {
        SendMessageW(_previewWindow, WM_SETFONT, reinterpret_cast<WPARAM>(_fontHandle), TRUE);
    }

    if (!SetWindowTextW(_previewWindow, text.c_str())) {
        const HRESULT hr = LastErrorAsHResult();
        DestroyWindow(_previewWindow);
        _previewWindow = nullptr;
        return hr;
    }

    const HRESULT colorHr = ApplyTextColor();
    if (FAILED(colorHr)) {
        DestroyWindow(_previewWindow);
        _previewWindow = nullptr;
        return colorHr;
    }

    const HRESULT formatHr = ApplyMarkdownFormatting();
    if (FAILED(formatHr)) {
        DestroyWindow(_previewWindow);
        _previewWindow = nullptr;
        return formatHr;
    }

    const HRESULT moveHr = MovePreviewWindow();
    if (FAILED(moveHr)) {
        DestroyWindow(_previewWindow);
        _previewWindow = nullptr;
        return moveHr;
    }
    return S_OK;
}

HRESULT LocalMarkdownPreviewHandler::DestroyPreviewWindow() noexcept {
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

HRESULT LocalMarkdownPreviewHandler::MovePreviewWindow() noexcept {
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

HRESULT LocalMarkdownPreviewHandler::ApplyTextColor() noexcept {
    if (_previewWindow == nullptr || !IsWindow(_previewWindow)) {
        return E_FAIL;
    }

    CHARFORMAT2W format{};
    format.cbSize = sizeof(format);
    format.dwMask = CFM_COLOR;
    format.crTextColor = _textColor;
    return SendMessageW(
               _previewWindow,
               EM_SETCHARFORMAT,
               static_cast<WPARAM>(SCF_ALL),
               reinterpret_cast<LPARAM>(&format)) != FALSE
               ? S_OK
               : E_FAIL;
}

HRESULT LocalMarkdownPreviewHandler::ApplyMarkdownFormatting() noexcept {
    if (_previewWindow == nullptr || !IsWindow(_previewWindow)) {
        return E_FAIL;
    }

    const int red = GetRValue(_backgroundColor);
    const int green = GetGValue(_backgroundColor);
    const int blue = GetBValue(_backgroundColor);
    const bool dark = (red * 299 + green * 587 + blue * 114) < 128000;
    const COLORREF linkColor = dark ? RGB(102, 179, 255) : RGB(0, 102, 204);
    const COLORREF quoteColor = dark ? RGB(176, 190, 204) : RGB(80, 96, 112);
    const COLORREF codeBackground = dark ? RGB(45, 48, 52) : RGB(242, 244, 247);

    for (const auto& range : _document.formats) {
        if (range.length == 0 || range.start > static_cast<std::size_t>(LONG_MAX) ||
            range.length > static_cast<std::size_t>(LONG_MAX) - range.start) {
            continue;
        }

        CHARRANGE selection{
            static_cast<LONG>(range.start),
            static_cast<LONG>(range.start + range.length)};
        SendMessageW(_previewWindow, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&selection));

        CHARFORMAT2W format{};
        format.cbSize = sizeof(format);
        using LocalMarkdownPreview::MarkdownFormatKind;
        switch (range.kind) {
        case MarkdownFormatKind::Heading: {
            static constexpr LONG headingTwips[] = {480, 400, 340, 300, 270, 240};
            const unsigned index = range.level >= 1 && range.level <= 6
                                       ? range.level - 1
                                       : 5;
            format.dwMask = CFM_BOLD | CFM_SIZE;
            format.dwEffects = CFE_BOLD;
            format.yHeight = headingTwips[index];
            break;
        }
        case MarkdownFormatKind::Bold:
        case MarkdownFormatKind::TableHeader:
            format.dwMask = CFM_BOLD;
            format.dwEffects = CFE_BOLD;
            break;
        case MarkdownFormatKind::Italic:
            format.dwMask = CFM_ITALIC;
            format.dwEffects = CFE_ITALIC;
            break;
        case MarkdownFormatKind::Strikethrough:
            format.dwMask = CFM_STRIKEOUT;
            format.dwEffects = CFE_STRIKEOUT;
            break;
        case MarkdownFormatKind::Code:
            format.dwMask = CFM_FACE | CFM_BACKCOLOR;
            format.crBackColor = codeBackground;
            wcscpy_s(format.szFaceName, _countof(format.szFaceName), L"Cascadia Mono");
            break;
        case MarkdownFormatKind::Link:
            format.dwMask = CFM_COLOR | CFM_UNDERLINE;
            format.dwEffects = CFE_UNDERLINE;
            format.crTextColor = linkColor;
            break;
        case MarkdownFormatKind::Quote:
            format.dwMask = CFM_COLOR | CFM_ITALIC;
            format.dwEffects = CFE_ITALIC;
            format.crTextColor = quoteColor;
            break;
        }

        if (SendMessageW(_previewWindow,
                         EM_SETCHARFORMAT,
                         static_cast<WPARAM>(SCF_SELECTION),
                         reinterpret_cast<LPARAM>(&format)) == FALSE) {
            return E_FAIL;
        }
    }

    CHARRANGE startSelection{0, 0};
    SendMessageW(_previewWindow, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&startSelection));
    return S_OK;
}

HRESULT LocalMarkdownPreviewHandler::InsertLocalImages() noexcept try {
    if (_previewWindow == nullptr || !IsWindow(_previewWindow)) {
        return E_FAIL;
    }
    if (_sourceName.empty() || _document.images.empty()) {
        return S_OK;
    }

    constexpr std::size_t kMaxImages = 20;
    constexpr std::uint64_t kMaxTotalDecodedBytes = 32u * 1024u * 1024u;
    LocalMarkdownPreview::SafeImageLoadLimits limits;
    limits.max_decoded_bytes = 16u * 1024u * 1024u;
    limits.max_pixels = limits.max_decoded_bytes / 4u;
    std::uint64_t totalDecodedBytes = 0;
    std::size_t loadedCount = 0;

    for (std::size_t index = _document.images.size(); index-- > 0;) {
        if (loadedCount >= kMaxImages || IsPreviewCancelled(&_cancelRequested)) {
            break;
        }
        const auto imageMetadata = _document.images[index];
        LocalMarkdownPreview::SafeImageLoadResult image =
            LocalMarkdownPreview::LoadSafeMarkdownImage(
                _sourceName,
                imageMetadata.source,
                limits);
        if (!image.Succeeded()) {
            continue;
        }
        const std::uint64_t decodedBytes = image.pixels.size();
        if (decodedBytes > kMaxTotalDecodedBytes - totalDecodedBytes) {
            continue;
        }

        RECT client{};
        GetClientRect(_previewWindow, &client);
        const UINT dpi = std::max<UINT>(GetDpiForWindow(_previewWindow), 96u);
        const std::uint32_t availablePixels = static_cast<std::uint32_t>(
            std::max<LONG>(64, client.right - client.left - 24));
        const std::uint32_t displayWidth = std::min(image.width, availablePixels);
        const std::uint32_t displayHeight = std::max<std::uint32_t>(
            1,
            static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(image.height) * displayWidth / image.width));
        const LONG widthHimetric = static_cast<LONG>(
            static_cast<std::uint64_t>(displayWidth) * 2540u / dpi);
        const LONG heightHimetric = static_cast<LONG>(
            static_cast<std::uint64_t>(displayHeight) * 2540u / dpi);

        if (imageMetadata.start > static_cast<std::size_t>(LONG_MAX) ||
            imageMetadata.length > static_cast<std::size_t>(LONG_MAX) - imageMetadata.start) {
            continue;
        }
        const std::wstring placeholder = _document.visible_text.substr(
            imageMetadata.start, imageMetadata.length);
        CHARRANGE selection{
            static_cast<LONG>(imageMetadata.start),
            static_cast<LONG>(imageMetadata.start + imageMetadata.length)};
        SendMessageW(_previewWindow, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&selection));
        SendMessageW(_previewWindow, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L""));
        const HRESULT insertHr = InsertBitmapObject(
            _previewWindow, image, widthHimetric, heightHimetric);
        if (SUCCEEDED(insertHr)) {
            AdjustDocumentAfterImage(_document,
                                     imageMetadata.start,
                                     imageMetadata.length);
            totalDecodedBytes += decodedBytes;
            ++loadedCount;
        } else {
            SendMessageW(_previewWindow,
                         EM_REPLACESEL,
                         FALSE,
                         reinterpret_cast<LPARAM>(placeholder.c_str()));
        }
    }

    CHARRANGE startSelection{0, 0};
    SendMessageW(_previewWindow, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&startSelection));
    return S_OK;
} catch (const std::bad_alloc&) {
    return E_OUTOFMEMORY;
} catch (...) {
    return E_FAIL;
}

HRESULT LocalMarkdownPreviewHandler::ReadPreviewText(std::wstring* text) noexcept {
    if (!_initialized || text == nullptr) {
        return E_INVALIDARG;
    }
    if (_sourceName.empty()) {
        return E_UNEXPECTED;
    }
    IStream* fileStream = nullptr;
    const HRESULT openHr = SHCreateStreamOnFileEx(
        _sourceName.c_str(),
        STGM_READ | STGM_SHARE_DENY_NONE,
        FILE_ATTRIBUTE_NORMAL,
        FALSE,
        nullptr,
        &fileStream);
    if (FAILED(openHr)) {
        return openHr;
    }
    const HRESULT readHr = ReadStreamText(fileStream, text, nullptr);
    fileStream->Release();
    return readHr;
}

HRESULT LocalMarkdownPreviewHandler::ReadStreamText(IStream* stream,
                                                    std::wstring* text,
                                                    std::wstring* sourceName) noexcept {
    if (stream == nullptr || text == nullptr) {
        return E_INVALIDARG;
    }

    if (sourceName != nullptr) {
        sourceName->clear();
    }

    STATSTG statistics{};
    const HRESULT statHr = stream->Stat(&statistics, STATFLAG_DEFAULT);
    ULONGLONG knownSize = 0;
    if (SUCCEEDED(statHr)) {
        PWSTR rawName = statistics.pwcsName;
        statistics.pwcsName = nullptr;
        if (rawName != nullptr) {
            if (sourceName != nullptr) {
                size_t nameLength = 0;
                while (nameLength < kMaxSourceNameChars && rawName[nameLength] != L'\0') {
                    ++nameLength;
                }
                try {
                    sourceName->assign(rawName, nameLength);
                } catch (const std::bad_alloc&) {
                    CoTaskMemFree(rawName);
                    return E_OUTOFMEMORY;
                }
            }
            CoTaskMemFree(rawName);
        }
        knownSize = statistics.cbSize.QuadPart;
        if (knownSize > static_cast<ULONGLONG>(kMaxPreviewBytes)) {
            return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        }
    }

    LARGE_INTEGER origin{};
    ULARGE_INTEGER newPosition{};
    const HRESULT seekHr = stream->Seek(origin, STREAM_SEEK_SET, &newPosition);
    if (FAILED(seekHr)) {
        return seekHr;
    }

    std::vector<BYTE> bytes;
    try {
        if (knownSize > 0) {
            bytes.reserve(static_cast<size_t>(knownSize));
        } else {
            bytes.reserve(kReadChunkBytes);
        }
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }

    BYTE chunk[kReadChunkBytes]{};
    for (;;) {
        const size_t remaining = kMaxPreviewBytes - bytes.size();
        const ULONG requested = remaining == 0
                                    ? 1u
                                    : static_cast<ULONG>(std::min(
                                          remaining, static_cast<size_t>(kReadChunkBytes)));
        ULONG read = 0;
        const HRESULT readHr = stream->Read(chunk, requested, &read);
        if (FAILED(readHr)) {
            return readHr;
        }
        if (read > requested) {
            return STG_E_READFAULT;
        }
        if (read == 0) {
            break;
        }
        if (bytes.size() > kMaxPreviewBytes - static_cast<size_t>(read)) {
            return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        }

        try {
            bytes.insert(bytes.end(), chunk, chunk + read);
        } catch (const std::bad_alloc&) {
            return E_OUTOFMEMORY;
        }
    }

    return ConvertBytesToText(bytes, text);
}

HRESULT LocalMarkdownPreviewHandler::ConvertBytesToText(const std::vector<BYTE>& bytes,
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
            if (!IsValidUtf16(converted)) {
                return HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION);
            }
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
            if (!IsValidUtf16(converted)) {
                return HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION);
            }
            *text = std::move(converted);
            return S_OK;
        }

        const int sourceLength = static_cast<int>(bytes.size() - offset);
        if (sourceLength == 0) {
            text->clear();
            return S_OK;
        }

        UINT codePage = CP_UTF8;
        DWORD flags = MB_ERR_INVALID_CHARS;
        int required = MultiByteToWideChar(
            codePage,
            flags,
            reinterpret_cast<LPCCH>(bytes.data() + offset),
            sourceLength,
            nullptr,
            0);
        if (required == 0) {
            const DWORD utf8Error = GetLastError();
            if (utf8Error != ERROR_NO_UNICODE_TRANSLATION) {
                return HResultFromErrorCode(utf8Error);
            }

            // GB18030 is the only legacy fallback. It is still strict, so arbitrary
            // corrupt bytes do not silently become the local ANSI code page.
            codePage = kCodePageGb18030;
            required = MultiByteToWideChar(
                codePage,
                MB_ERR_INVALID_CHARS,
                reinterpret_cast<LPCCH>(bytes.data() + offset),
                sourceLength,
                nullptr,
                0);
            if (required == 0) {
                return HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION);
            }
            flags = MB_ERR_INVALID_CHARS;
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
            return HResultFromErrorCode(GetLastError());
        }
        std::replace(converted.begin(), converted.end(), L'\0', L'\xFFFD');
        *text = std::move(converted);
        return S_OK;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
}

HRESULT LocalMarkdownPreviewHandler::LastErrorAsHResult() noexcept {
    return HResultFromErrorCode(GetLastError());
}

bool LocalMarkdownPreviewHandler::IsRectUsable(const RECT& rect) noexcept {
    const LONGLONG width = static_cast<LONGLONG>(rect.right) - static_cast<LONGLONG>(rect.left);
    const LONGLONG height = static_cast<LONGLONG>(rect.bottom) - static_cast<LONGLONG>(rect.top);
    return width >= 0 && height >= 0 && width <= static_cast<LONGLONG>(std::numeric_limits<int>::max()) &&
           height <= static_cast<LONGLONG>(std::numeric_limits<int>::max());
}

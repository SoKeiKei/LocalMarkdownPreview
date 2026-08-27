#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <richedit.h>
#include <shobjidl.h>

#include <string>
#include <vector>

extern HINSTANCE g_module_instance;

void ModuleAddRef() noexcept;
void ModuleRelease() noexcept;

extern const CLSID CLSID_NativePreviewHandler;

HRESULT CreateNativePreviewHandler(REFIID riid, void** result) noexcept;

class NativePreviewHandler final : public IObjectWithSite,
                                   public IPreviewHandler,
                                   public IOleWindow,
                                   public IInitializeWithStream {
public:
    NativePreviewHandler() noexcept;
    ~NativePreviewHandler();

    NativePreviewHandler(const NativePreviewHandler&) = delete;
    NativePreviewHandler& operator=(const NativePreviewHandler&) = delete;

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** result) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IObjectWithSite
    STDMETHODIMP SetSite(IUnknown* site) override;
    STDMETHODIMP GetSite(REFIID riid, void** result) override;

    // IOleWindow
    STDMETHODIMP GetWindow(HWND* window) override;
    STDMETHODIMP ContextSensitiveHelp(BOOL enterMode) override;

    // IInitializeWithStream
    STDMETHODIMP Initialize(IStream* stream, DWORD mode) override;

    // IPreviewHandler
    STDMETHODIMP SetWindow(HWND parent, const RECT* rect) override;
    STDMETHODIMP SetRect(const RECT* rect) override;
    STDMETHODIMP DoPreview() override;
    STDMETHODIMP SetFocus() override;
    STDMETHODIMP QueryFocus(HWND* window) override;
    STDMETHODIMP TranslateAccelerator(MSG* message) override;
    STDMETHODIMP Unload() override;

private:
    static constexpr size_t kMaxPreviewBytes = 5u * 1024u * 1024u;

    HRESULT CreatePreviewWindow(const std::wstring& text) noexcept;
    HRESULT DestroyPreviewWindow() noexcept;
    HRESULT MovePreviewWindow() noexcept;
    HRESULT ReadPreviewText(std::wstring* text) const noexcept;

    static HRESULT ReadStreamText(IStream* stream, std::wstring* text) noexcept;
    static HRESULT ConvertBytesToText(const std::vector<BYTE>& bytes,
                                      std::wstring* text) noexcept;
    static HRESULT LastErrorAsHResult() noexcept;
    static bool IsRectUsable(const RECT& rect) noexcept;

    LONG _referenceCount;
    HWND _parentWindow;
    RECT _previewRect;
    HWND _previewWindow;
    HMODULE _richEditModule;
    IUnknown* _site;
    IStream* _stream;
    DWORD _streamMode;
    bool _initialized;
};

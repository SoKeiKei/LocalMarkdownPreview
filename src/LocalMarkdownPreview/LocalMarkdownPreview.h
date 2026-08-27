#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef _RICHEDIT_VER
#define _RICHEDIT_VER 0x0800
#endif

#include <windows.h>
#include <richedit.h>
#include <shobjidl.h>

#include <string>
#include <vector>

#include "MarkdownDocument.h"

extern HINSTANCE g_module_instance;

void ModuleAddRef() noexcept;
void ModuleRelease() noexcept;

extern const CLSID CLSID_LocalMarkdownPreviewHandler;

HRESULT CreateLocalMarkdownPreviewHandler(REFIID riid, void** result) noexcept;

class LocalMarkdownPreviewHandler final : public IObjectWithSite,
                                           public IPreviewHandler,
                                           public IOleWindow,
                                           public IInitializeWithFile,
                                           public IPreviewHandlerVisuals {
public:
    LocalMarkdownPreviewHandler() noexcept;
    ~LocalMarkdownPreviewHandler();

    LocalMarkdownPreviewHandler(const LocalMarkdownPreviewHandler&) = delete;
    LocalMarkdownPreviewHandler& operator=(const LocalMarkdownPreviewHandler&) = delete;

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

    // IInitializeWithFile
    STDMETHODIMP Initialize(LPCWSTR filePath, DWORD mode) override;

    // IPreviewHandler
    STDMETHODIMP SetWindow(HWND parent, const RECT* rect) override;
    STDMETHODIMP SetRect(const RECT* rect) override;
    STDMETHODIMP DoPreview() override;
    STDMETHODIMP SetFocus() override;
    STDMETHODIMP QueryFocus(HWND* window) override;
    STDMETHODIMP TranslateAccelerator(MSG* message) override;
    STDMETHODIMP Unload() override;

    // IPreviewHandlerVisuals
    STDMETHODIMP SetBackgroundColor(COLORREF color) override;
    STDMETHODIMP SetFont(const LOGFONTW* font) override;
    STDMETHODIMP SetTextColor(COLORREF color) override;

private:
    static constexpr size_t kMaxPreviewBytes = 5u * 1024u * 1024u;

    HRESULT CreatePreviewWindow(const std::wstring& text) noexcept;
    HRESULT CreateMarkdownPreviewWindow(
        LocalMarkdownPreview::MarkdownDocumentResult document) noexcept;
    HRESULT DestroyPreviewWindow() noexcept;
    HRESULT MovePreviewWindow() noexcept;
    HRESULT ApplyTextColor() noexcept;
    HRESULT ApplyMarkdownFormatting() noexcept;
    HRESULT InsertLocalImages() noexcept;
    HRESULT ReadPreviewText(std::wstring* text) noexcept;

    static HRESULT ReadStreamText(IStream* stream,
                                  std::wstring* text,
                                  std::wstring* sourceName) noexcept;
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
    DWORD _streamMode;
    bool _initialized;
    COLORREF _backgroundColor;
    COLORREF _textColor;
    LOGFONTW _font;
    bool _fontSet;
    HFONT _fontHandle;
    LONG _cancelRequested;
    std::wstring _sourceName;
    LocalMarkdownPreview::MarkdownDocumentResult _document;
};

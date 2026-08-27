#include "preview_handler.h"

#include <new>

HINSTANCE g_module_instance = nullptr;
const CLSID CLSID_NativePreviewHandler = {
    0xdce6c2fe,
    0x4c11,
    0x4d00,
    {0xae, 0x9f, 0x2a, 0x1d, 0x4a, 0x6b, 0xa9, 0xa5}};

namespace {

LONG g_module_reference_count = 0;

using CreateInstanceFunction = HRESULT (*)(REFIID, void**);

HRESULT CreateHandlerInstance(REFIID riid, void** result) noexcept {
    if (result == nullptr) {
        return E_POINTER;
    }
    *result = nullptr;

    NativePreviewHandler* handler = new (std::nothrow) NativePreviewHandler();
    if (handler == nullptr) {
        return E_OUTOFMEMORY;
    }

    const HRESULT hr = handler->QueryInterface(riid, result);
    handler->Release();
    return hr;
}

class PreviewClassFactory final : public IClassFactory {
public:
    explicit PreviewClassFactory(CreateInstanceFunction createInstance) noexcept
        : _referenceCount(1), _createInstance(createInstance) {
        ModuleAddRef();
    }

    ~PreviewClassFactory() { ModuleRelease(); }

    PreviewClassFactory(const PreviewClassFactory&) = delete;
    PreviewClassFactory& operator=(const PreviewClassFactory&) = delete;

    STDMETHODIMP QueryInterface(REFIID riid, void** result) override {
        if (result == nullptr) {
            return E_POINTER;
        }
        *result = nullptr;
        if (riid != IID_IUnknown && riid != __uuidof(IClassFactory)) {
            return E_NOINTERFACE;
        }

        *result = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
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

    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** result) override {
        if (result == nullptr) {
            return E_POINTER;
        }
        *result = nullptr;
        if (outer != nullptr) {
            return CLASS_E_NOAGGREGATION;
        }
        if (_createInstance == nullptr) {
            return E_UNEXPECTED;
        }
        return _createInstance(riid, result);
    }

    STDMETHODIMP LockServer(BOOL lock) override {
        if (lock) {
            ModuleAddRef();
        } else {
            ModuleRelease();
        }
        return S_OK;
    }

private:
    LONG _referenceCount;
    CreateInstanceFunction _createInstance;
};

} // namespace

void ModuleAddRef() noexcept {
    InterlockedIncrement(&g_module_reference_count);
}

void ModuleRelease() noexcept {
    const LONG remaining = InterlockedDecrement(&g_module_reference_count);
    if (remaining < 0) {
        // Keep DllCanUnloadNow conservative if a caller sends an unmatched unlock.
        InterlockedExchange(&g_module_reference_count, 0);
    }
}

HRESULT CreateNativePreviewHandler(REFIID riid, void** result) noexcept {
    return CreateHandlerInstance(riid, result);
}

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module_instance = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

extern "C" HRESULT STDAPICALLTYPE DllCanUnloadNow() {
    return InterlockedCompareExchange(&g_module_reference_count, 0, 0) == 0 ? S_OK : S_FALSE;
}

extern "C" HRESULT STDAPICALLTYPE DllGetClassObject(
    REFCLSID clsid,
    REFIID riid,
    void** result) {
    if (result == nullptr) {
        return E_POINTER;
    }
    *result = nullptr;
    if (clsid != CLSID_NativePreviewHandler) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    PreviewClassFactory* factory = new (std::nothrow) PreviewClassFactory(&CreateHandlerInstance);
    if (factory == nullptr) {
        return E_OUTOFMEMORY;
    }

    const HRESULT hr = factory->QueryInterface(riid, result);
    factory->Release();
    return hr;
}

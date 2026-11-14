#ifdef _WIN32
#include "TextService.h"

#include <new>

HMODULE g_wowtypeModule = nullptr;
std::atomic<long> g_wowtypeObjects{0};
std::atomic<long> g_wowtypeLocks{0};

const CLSID CLSID_WowTypeTextService =
    {0x3e76c197, 0x7a5e, 0x4a4a, {0x9a, 0x31, 0x53, 0x7d, 0x74, 0x3f, 0x22, 0x51}};
const GUID GUID_WowTypeProfile =
    {0x4931e8aa, 0xd310, 0x44f2, {0x85, 0xc0, 0x83, 0x0b, 0x19, 0xe4, 0x9d, 0x68}};

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_wowtypeModule = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

namespace {
class ClassFactory final : public IClassFactory {
public:
    ClassFactory() { ++g_wowtypeObjects; }
    ~ClassFactory() { --g_wowtypeObjects; }
    STDMETHODIMP QueryInterface(REFIID riid, void **object) override {
        if (!object) return E_INVALIDARG;
        *object = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
            *object = static_cast<IClassFactory *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++references_; }
    STDMETHODIMP_(ULONG) Release() override {
        const ULONG remaining = --references_;
        if (remaining == 0) delete this;
        return remaining;
    }
    STDMETHODIMP CreateInstance(IUnknown *outer, REFIID riid, void **object) override {
        if (outer) return CLASS_E_NOAGGREGATION;
        auto *service = new (std::nothrow) TextService();
        if (!service) return E_OUTOFMEMORY;
        const HRESULT hr = service->QueryInterface(riid, object);
        service->Release();
        return hr;
    }
    STDMETHODIMP LockServer(BOOL lock) override {
        if (lock) ++g_wowtypeLocks;
        else --g_wowtypeLocks;
        return S_OK;
    }
private:
    std::atomic<ULONG> references_{1};
};
}

HRESULT CreateTextService(REFIID riid, void **object) {
    if (!object) return E_INVALIDARG;
    *object = nullptr;
    auto *factory = new (std::nothrow) ClassFactory();
    if (!factory) return E_OUTOFMEMORY;
    const HRESULT hr = factory->CreateInstance(nullptr, riid, object);
    factory->Release();
    return hr;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID *object) {
    if (!IsEqualCLSID(clsid, CLSID_WowTypeTextService)) return CLASS_E_CLASSNOTAVAILABLE;
    if (!object) return E_INVALIDARG;
    *object = nullptr;
    auto *factory = new (std::nothrow) ClassFactory();
    if (!factory) return E_OUTOFMEMORY;
    const HRESULT hr = factory->QueryInterface(riid, object);
    factory->Release();
    return hr;
}

STDAPI DllCanUnloadNow(void) {
    return g_wowtypeObjects.load() == 0 && g_wowtypeLocks.load() == 0 ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer(void) {
    return RegisterTextService();
}

STDAPI DllUnregisterServer(void) {
    return UnregisterTextService();
}
#endif

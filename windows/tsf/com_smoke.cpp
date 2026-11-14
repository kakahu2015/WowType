// Independent registration acceptance test: load TSF COM class through COM.
// Run after per-user regsvr32, before uninstall. No keyboard injection.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <msctf.h>
#include <cstdio>

static const CLSID kWowType =
    {0x9aebaf46, 0x3231, 0x4f65, {0x83, 0x6d, 0x09, 0x72, 0xc2, 0x39, 0x61, 0x81}};

int main() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        std::fprintf(stderr, "CoInitializeEx: 0x%08lx\n", static_cast<unsigned long>(hr));
        return 1;
    }
    ITfTextInputProcessorEx *processor = nullptr;
    hr = CoCreateInstance(kWowType, nullptr, CLSCTX_INPROC_SERVER,
                          IID_ITfTextInputProcessorEx, reinterpret_cast<void **>(&processor));
    if (FAILED(hr) || !processor) {
        std::fprintf(stderr, "CoCreateInstance: 0x%08lx\n", static_cast<unsigned long>(hr));
        CoUninitialize();
        return 2;
    }
    ITfKeyEventSink *keys = nullptr;
    hr = processor->QueryInterface(IID_ITfKeyEventSink, reinterpret_cast<void **>(&keys));
    if (FAILED(hr) || !keys) {
        std::fprintf(stderr, "QueryInterface key sink: 0x%08lx\n", static_cast<unsigned long>(hr));
        processor->Release();
        CoUninitialize();
        return 3;
    }
    keys->Release();
    processor->Release();
    CoUninitialize();
    std::puts("WOWTYPE TSF COM ACTIVATION OK");
    return 0;
}

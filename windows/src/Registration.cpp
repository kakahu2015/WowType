#ifdef _WIN32
#include "TextService.h"

#include <iterator>
#include <string>

namespace {
constexpr LANGID kProfileLanguage = MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED);

std::wstring GuidString(REFGUID guid) {
    wchar_t text[64]{};
    StringFromGUID2(guid, text, static_cast<int>(std::size(text)));
    return text;
}

HRESULT WriteComRegistrationUnder(HKEY root) {
    const std::wstring keyPath = L"Software\\Classes\\CLSID\\" +
                                 GuidString(CLSID_WowTypeTextService) +
                                 L"\\InprocServer32";
    HKEY key = nullptr;
    const LONG opened = RegCreateKeyExW(root, keyPath.c_str(), 0, nullptr,
                                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                                        &key, nullptr);
    if (opened != ERROR_SUCCESS) return HRESULT_FROM_WIN32(opened);

    wchar_t module[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(g_wowtypeModule, module, MAX_PATH);
    LONG result = ERROR_SUCCESS;
    if (length == 0 || length >= MAX_PATH) {
        result = ERROR_INSUFFICIENT_BUFFER;
    } else {
        result = RegSetValueExW(key, nullptr, 0, REG_SZ,
                                reinterpret_cast<const BYTE *>(module),
                                (length + 1) * sizeof(wchar_t));
        if (result == ERROR_SUCCESS) {
            constexpr wchar_t threadingModel[] = L"Apartment";
            result = RegSetValueExW(key, L"ThreadingModel", 0, REG_SZ,
                                    reinterpret_cast<const BYTE *>(threadingModel),
                                    sizeof(threadingModel));
        }
    }
    RegCloseKey(key);
    return result == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(result);
}

HRESULT WriteComRegistration() {
    // The Win+Space switcher is rendered by TextInputHost.exe, an AppContainer
    // process whose registry view excludes HKCU\Software\Classes. A per-user
    // class is therefore invisible to the switcher even though TSF itself
    // enumerates the profile, which leaves every other input method selectable
    // but not this one. Register machine-wide and mirror to the user hive for
    // legacy consumers that still read HKCU.
    const HRESULT machine = WriteComRegistrationUnder(HKEY_LOCAL_MACHINE);
    if (FAILED(machine)) return machine;
    WriteComRegistrationUnder(HKEY_CURRENT_USER);
    return S_OK;
}

void RemoveComRegistration() {
    const std::wstring keyPath = L"Software\\Classes\\CLSID\\" +
                                 GuidString(CLSID_WowTypeTextService);
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, keyPath.c_str());
    RegDeleteTreeW(HKEY_CURRENT_USER, keyPath.c_str());
}

HRESULT WithComInitialized(bool &uninitialize) {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    uninitialize = SUCCEEDED(hr);
    return hr == RPC_E_CHANGED_MODE ? S_OK : hr;
}
}

HRESULT RegisterTextService() {
    HRESULT hr = WriteComRegistration();
    if (FAILED(hr)) return hr;

    bool uninitialize = false;
    hr = WithComInitialized(uninitialize);
    if (FAILED(hr)) {
        RemoveComRegistration();
        return hr;
    }

    Microsoft::WRL::ComPtr<ITfInputProcessorProfileMgr> profiles;
    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                          CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&profiles));
    if (SUCCEEDED(hr)) {
        hr = profiles->RegisterProfile(CLSID_WowTypeTextService, kProfileLanguage,
                                       GUID_WowTypeProfile, L"WowType", 7,
                                       nullptr, 0, 0, nullptr, 0, TRUE, 0);
        if (hr == TF_E_ALREADY_EXISTS) hr = S_OK;
    }

    Microsoft::WRL::ComPtr<ITfCategoryMgr> categories;
    if (SUCCEEDED(hr)) {
        hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&categories));
    }
    if (SUCCEEDED(hr)) {
        hr = categories->RegisterCategory(CLSID_WowTypeTextService,
                                          GUID_TFCAT_TIP_KEYBOARD,
                                          CLSID_WowTypeTextService);
        if (hr == TF_E_ALREADY_EXISTS) hr = S_OK;
    }
    if (uninitialize) CoUninitialize();

    if (FAILED(hr)) RemoveComRegistration();
    return hr;
}

HRESULT UnregisterTextService() {
    bool uninitialize = false;
    HRESULT hr = WithComInitialized(uninitialize);
    if (SUCCEEDED(hr)) {
        Microsoft::WRL::ComPtr<ITfInputProcessorProfileMgr> profiles;
        if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                       CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&profiles)))) {
            profiles->UnregisterProfile(CLSID_WowTypeTextService, kProfileLanguage,
                                        GUID_WowTypeProfile, 0);
        }
        Microsoft::WRL::ComPtr<ITfCategoryMgr> categories;
        if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
                                      CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&categories)))) {
            categories->UnregisterCategory(CLSID_WowTypeTextService,
                                           GUID_TFCAT_TIP_KEYBOARD,
                                           CLSID_WowTypeTextService);
        }
        if (uninitialize) CoUninitialize();
    }
    RemoveComRegistration();
    return S_OK;
}
#endif

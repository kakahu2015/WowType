// WowType Windows TSF Phase 2 prototype. Windows 11 x64 only.
// In-process COM text service; no global hooks, keyboard simulation or networking.
// Requires wowtype_core.dll and data/demo_lexicon.tsv beside the installed module.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <msctf.h>
#include <wrl/client.h>
#include <atomic>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <new>
#include <utility>
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>
#include "wowtype_core.h"

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

static HMODULE g_module = nullptr;
static std::atomic<long> g_objects{0};
static std::atomic<long> g_locks{0};

// Stable identifiers for this independent fork, distinct from upstream and other IMEs.
static const CLSID CLSID_WowTypeTsf =
    {0x9aebaf46, 0x3231, 0x4f65, {0x83, 0x6d, 0x09, 0x72, 0xc2, 0x39, 0x61, 0x81}};
static const GUID GUID_WowTypeProfile =
    {0x7b029476, 0xa6f3, 0x40b4, {0xb1, 0x99, 0x62, 0xe0, 0xd2, 0xfa, 0x55, 0x9d}};
static constexpr LANGID kLangZhCn = 0x0804;

static std::wstring ModuleFile() {
    wchar_t name[32768] = {};
    DWORD count = GetModuleFileNameW(g_module, name, static_cast<DWORD>(std::size(name)));
    if (!count || count >= std::size(name)) return {};
    return {name, count};
}

static std::string Utf8(const std::wstring &text) {
    if (text.empty()) return {};
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                                     static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string output(static_cast<size_t>(count), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                             static_cast<int>(text.size()), output.data(), count, nullptr, nullptr)) return {};
    return output;
}

static std::wstring Wide(const char *text) {
    if (!text || !*text) return {};
    int length = static_cast<int>(strlen(text));
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, length, nullptr, 0);
    if (count <= 0) return {};
    std::wstring output(static_cast<size_t>(count), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, length, output.data(), count)) return {};
    return output;
}

struct Snapshot {
    bool handled = false;
    std::wstring preedit;
    std::wstring commit;
    std::vector<std::wstring> candidates;
};

class CoreSession {
    HMODULE library_ = nullptr;
    WowtypeEngineHandle *engine_ = nullptr;
    WowtypeSession *session_ = nullptr;
    int (*abi_)() = nullptr;
    WowtypeEngineHandle *(*new_engine_)(const char *, const char *) = nullptr;
    void (*free_engine_)(WowtypeEngineHandle *) = nullptr;
    WowtypeSession *(*new_session_)(WowtypeEngineHandle *, const WowtypeSessionConfig *) = nullptr;
    void (*free_session_)(WowtypeSession *) = nullptr;
    const WowtypeFrame *(*key_)(WowtypeSession *, const WowtypeKey *) = nullptr;
    const WowtypeFrame *(*reset_)(WowtypeSession *, int) = nullptr;
    const char *(*candidate_)(WowtypeSession *, size_t) = nullptr;
public:
    CoreSession() = default;
    ~CoreSession() { Close(); }
    CoreSession(const CoreSession &) = delete;
    CoreSession &operator=(const CoreSession &) = delete;
    bool Ready() const { return session_ != nullptr; }

    void Close() {
        if (session_ && free_session_) free_session_(session_);
        session_ = nullptr;
        if (engine_ && free_engine_) free_engine_(engine_);
        engine_ = nullptr;
        if (library_) FreeLibrary(library_);
        library_ = nullptr;
    }

    bool Open() {
        if (Ready()) return true;
        auto path = fs::path(ModuleFile()).parent_path();
        if (path.empty()) return false;
        auto dll = path / L"wowtype_core.dll";
        // Absolute-path load prevents current-directory DLL hijacking.
        library_ = LoadLibraryExW(dll.c_str(), nullptr,
                                   LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!library_) return false;
        #define LOAD_FN(field, symbol) \
            field = reinterpret_cast<decltype(field)>(GetProcAddress(library_, symbol)); \
            if (!field) { Close(); return false; }
        LOAD_FN(abi_, "wowtype_abi_version")
        LOAD_FN(new_engine_, "wowtype_engine_new")
        LOAD_FN(free_engine_, "wowtype_engine_free")
        LOAD_FN(new_session_, "wowtype_session_new")
        LOAD_FN(free_session_, "wowtype_session_free")
        LOAD_FN(key_, "wowtype_session_key")
        LOAD_FN(reset_, "wowtype_session_reset")
        LOAD_FN(candidate_, "wowtype_session_candidate")
        #undef LOAD_FN
        int version = abi_();
        if (version / 1000 != WOWTYPE_ABI_VERSION_MAJOR ||
            version % 1000 < 6) { Close(); return false; }

        auto dictionary = path / L"data" / L"lexicon.tsv";
        if (!fs::is_regular_file(dictionary)) dictionary = path / L"data" / L"demo_lexicon.tsv";
        if (!fs::is_regular_file(dictionary)) { Close(); return false; }
        auto lexicon_utf8 = Utf8(dictionary.wstring());
        if (lexicon_utf8.empty()) { Close(); return false; }
        engine_ = new_engine_(lexicon_utf8.c_str(), nullptr);
        if (!engine_) { Close(); return false; }
        session_ = new_session_(engine_, nullptr);
        if (!session_) { Close(); return false; }
        return true;
    }

    void Reset() { if (session_) reset_(session_, 0); }

    Snapshot Press(unsigned int keysym) {
        Snapshot result;
        if (!Ready()) return result;
        WowtypeKey key{};
        key.size = sizeof(key);
        key.keysym = keysym;
        const WowtypeFrame *frame = key_(session_, &key);
        if (!frame || frame->size < offsetof(WowtypeFrame, cand_count) + sizeof(size_t)) return result;
        result.handled = frame->handled != 0;
        result.preedit = Wide(frame->preedit);
        result.commit = Wide(frame->commit);
        size_t n = std::min(frame->cand_count, static_cast<size_t>(9));
        for (size_t i = 0; i < n; ++i) {
            result.candidates.push_back(Wide(candidate_(session_, i)));
        }
        return result;
    }
};

class Candidates {
    HWND window_ = nullptr;
    std::wstring text_;
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
        if (message == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCTW *>(lp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        }
        auto self = reinterpret_cast<Candidates *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_PAINT) {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT bounds{};
            GetClientRect(hwnd, &bounds);
            FillRect(dc, &bounds, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
            SetTextColor(dc, RGB(25, 25, 25));
            SetBkMode(dc, TRANSPARENT);
            if (self) {
                bounds.left += 8; bounds.top += 6;
                DrawTextW(dc, self->text_.c_str(), -1, &bounds, DT_LEFT | DT_NOPREFIX | DT_WORDBREAK);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        return DefWindowProcW(hwnd, message, wp, lp);
    }
public:
    ~Candidates() { Hide(); if (window_) DestroyWindow(window_); }
    void Hide() { if (window_) ShowWindow(window_, SW_HIDE); }
    void Show(const Snapshot &frame) {
        if (frame.preedit.empty() || frame.candidates.empty()) { Hide(); return; }
        if (!window_) {
            static constexpr wchar_t kClass[] = L"WowTypeTsfCandidatePrototype";
            WNDCLASSEXW wc{sizeof(wc)};
            wc.lpfnWndProc = WindowProc;
            wc.hInstance = g_module;
            wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            wc.lpszClassName = kClass;
            RegisterClassExW(&wc); // Already-registered is harmless.
            window_ = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                                     kClass, L"", WS_POPUP | WS_BORDER, 0, 0, 340, 160,
                                     nullptr, nullptr, g_module, this);
            if (!window_) return;
        }
        text_ = frame.preedit + L"\r\n";
        for (size_t i = 0; i < frame.candidates.size(); ++i) {
            text_ += std::to_wstring(i + 1) + L". " + frame.candidates[i] + L"   ";
            if (i == 3) text_ += L"\r\n";
        }
        GUITHREADINFO info{sizeof(info)};
        POINT pos{80, 90};
        if (GetGUIThreadInfo(0, &info) && info.hwndCaret) {
            pos.x = info.rcCaret.left;
            pos.y = info.rcCaret.bottom + 6;
            ClientToScreen(info.hwndCaret, &pos);
        } else {
            GetCursorPos(&pos);
        }
        HMONITOR monitor = MonitorFromPoint(pos, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitor_info{sizeof(monitor_info)};
        if (GetMonitorInfoW(monitor, &monitor_info)) {
            pos.x = std::clamp(pos.x, monitor_info.rcWork.left,
                               std::max(monitor_info.rcWork.left, monitor_info.rcWork.right - 340));
            pos.y = std::clamp(pos.y, monitor_info.rcWork.top,
                               std::max(monitor_info.rcWork.top, monitor_info.rcWork.bottom - 160));
        }
        SetWindowPos(window_, HWND_TOPMOST, pos.x, pos.y, 340, 160,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(window_, nullptr, TRUE);
    }
};

class TextService final : public ITfTextInputProcessorEx,
                          public ITfKeyEventSink,
                          public ITfCompositionSink,
                          public ITfThreadMgrEventSink {
    std::atomic<ULONG> refs_{1};
    ComPtr<ITfThreadMgr> thread_mgr_;
    ComPtr<ITfKeystrokeMgr> keystrokes_;
    ComPtr<ITfSource> thread_source_;
    ComPtr<ITfComposition> composition_;
    DWORD source_cookie_ = TF_INVALID_COOKIE;
    TfClientId client_id_ = TF_CLIENTID_NULL;
    CoreSession core_;
    Candidates popup_;
    bool active_ = false;

    bool CanHandle(WPARAM key) const {
        if (!active_ || !core_.Ready()) return false;
        if ((GetKeyState(VK_CONTROL) & 0x8000) ||
            (GetKeyState(VK_MENU) & 0x8000) ||
            (GetKeyState(VK_LWIN) & 0x8000) ||
            (GetKeyState(VK_RWIN) & 0x8000)) return false;
        if (key >= 'A' && key <= 'Z') {
            return !(GetKeyState(VK_SHIFT) & 0x8000);
        }
        if (!composition_) return false;
        if (key >= '0' && key <= '9') return true;
        switch (key) {
        case VK_SPACE: case VK_RETURN: case VK_BACK: case VK_ESCAPE:
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
        case VK_PRIOR: case VK_NEXT: return true;
        default: return false;
        }
    }
    static unsigned int Keysym(WPARAM key) {
        if (key >= 'A' && key <= 'Z') return static_cast<unsigned int>(key - 'A' + 'a');
        if (key >= '0' && key <= '9') return static_cast<unsigned int>(key);
        switch (key) {
        case VK_SPACE: return 0x20;
        case VK_RETURN: return 0xFF0D;
        case VK_BACK: return 0xFF08;
        case VK_ESCAPE: return 0xFF1B;
        case VK_LEFT: return 0xFF51;
        case VK_UP: return 0xFF52;
        case VK_RIGHT: return 0xFF53;
        case VK_DOWN: return 0xFF54;
        case VK_PRIOR: return 0xFF55;
        case VK_NEXT: return 0xFF56;
        default: return 0;
        }
    }

    HRESULT ApplyEdit(ITfContext *context, TfEditCookie cookie, const Snapshot &frame) {
        if (frame.preedit.empty() && frame.commit.empty() && !composition_) return S_OK;
        ComPtr<ITfInsertAtSelection> insert;
        HRESULT hr = context->QueryInterface(IID_PPV_ARGS(insert.GetAddressOf()));
        if (FAILED(hr)) return hr;

        if (!composition_ && !frame.preedit.empty()) {
            ComPtr<ITfRange> position;
            hr = insert->InsertTextAtSelection(cookie, TF_IAS_QUERYONLY, L"", 0,
                                                position.GetAddressOf());
            if (FAILED(hr)) return hr;
            ComPtr<ITfContextComposition> contexts;
            hr = context->QueryInterface(IID_PPV_ARGS(contexts.GetAddressOf()));
            if (FAILED(hr)) return hr;
            hr = contexts->StartComposition(cookie, position.Get(),
                                            static_cast<ITfCompositionSink *>(this),
                                            composition_.ReleaseAndGetAddressOf());
            if (FAILED(hr) || !composition_) return FAILED(hr) ? hr : E_FAIL;
        }

        if (composition_) {
            ComPtr<ITfRange> range;
            hr = composition_->GetRange(range.GetAddressOf());
            if (FAILED(hr)) return hr;
            const std::wstring &text = frame.commit.empty() ? frame.preedit : frame.commit;
            hr = range->SetText(cookie, 0, text.c_str(), static_cast<LONG>(text.size()));
            if (FAILED(hr)) return hr;
            if (frame.preedit.empty() || !frame.commit.empty()) {
                auto finished = composition_;
                composition_.Reset();
                return finished->EndComposition(cookie);
            }
            ComPtr<ITfRange> cursor;
            if (SUCCEEDED(range->Clone(cursor.GetAddressOf())) &&
                SUCCEEDED(cursor->Collapse(cookie, TF_ANCHOR_END))) {
                TF_SELECTION selection{};
                selection.range = cursor.Get();
                selection.style.ase = TF_AE_END;
                selection.style.fInterimChar = FALSE;
                context->SetSelection(cookie, 1, &selection);
            }
            return S_OK;
        }
        if (!frame.commit.empty()) {
            return insert->InsertTextAtSelection(cookie, TF_IAS_NOQUERY,
                                                  frame.commit.c_str(),
                                                  static_cast<LONG>(frame.commit.size()), nullptr);
        }
        return S_OK;
    }

    class KeyEdit final : public ITfEditSession {
        std::atomic<ULONG> refs_{1};
        TextService *service_;
        ComPtr<ITfContext> context_;
        Snapshot frame_;
    public:
        KeyEdit(TextService *service, ITfContext *context, Snapshot frame)
            : service_(service), context_(context), frame_(std::move(frame)) { service_->AddRef(); }
        ~KeyEdit() { service_->Release(); }
        STDMETHODIMP QueryInterface(REFIID id, void **out) override {
            if (!out) return E_POINTER;
            *out = nullptr;
            if (IsEqualIID(id, IID_IUnknown) || IsEqualIID(id, IID_ITfEditSession))
                *out = static_cast<ITfEditSession *>(this);
            if (!*out) return E_NOINTERFACE;
            AddRef();
            return S_OK;
        }
        STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
        STDMETHODIMP_(ULONG) Release() override {
            ULONG n = --refs_; if (!n) delete this; return n;
        }
        STDMETHODIMP DoEditSession(TfEditCookie cookie) override {
            return service_->ApplyEdit(context_.Get(), cookie, frame_);
        }
    };

public:
    TextService() { ++g_objects; }
    ~TextService() { Deactivate(); --g_objects; }
    STDMETHODIMP QueryInterface(REFIID id, void **out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (IsEqualIID(id, IID_IUnknown) || IsEqualIID(id, IID_ITfTextInputProcessorEx))
            *out = static_cast<ITfTextInputProcessorEx *>(this);
        else if (IsEqualIID(id, IID_ITfTextInputProcessor))
            *out = static_cast<ITfTextInputProcessor *>(this);
        else if (IsEqualIID(id, IID_ITfKeyEventSink))
            *out = static_cast<ITfKeyEventSink *>(this);
        else if (IsEqualIID(id, IID_ITfCompositionSink))
            *out = static_cast<ITfCompositionSink *>(this);
        else if (IsEqualIID(id, IID_ITfThreadMgrEventSink))
            *out = static_cast<ITfThreadMgrEventSink *>(this);
        if (!*out) return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG n = --refs_; if (!n) delete this; return n;
    }

    STDMETHODIMP Activate(ITfThreadMgr *manager, TfClientId id) override {
        if (!manager) return E_INVALIDARG;
        if (active_) return S_OK;
        if (!core_.Open()) return E_FAIL;
        thread_mgr_ = manager; client_id_ = id;
        HRESULT hr = thread_mgr_->QueryInterface(IID_PPV_ARGS(keystrokes_.GetAddressOf()));
        if (FAILED(hr)) { Deactivate(); return hr; }
        hr = keystrokes_->AdviseKeyEventSink(id, static_cast<ITfKeyEventSink *>(this), TRUE);
        if (FAILED(hr)) { Deactivate(); return hr; }
        active_ = true;
        if (SUCCEEDED(thread_mgr_->QueryInterface(IID_PPV_ARGS(thread_source_.GetAddressOf())))) {
            if (FAILED(thread_source_->AdviseSink(IID_ITfThreadMgrEventSink,
                    static_cast<ITfThreadMgrEventSink *>(this), &source_cookie_)))
                source_cookie_ = TF_INVALID_COOKIE;
        }
        return S_OK;
    }
    STDMETHODIMP ActivateEx(ITfThreadMgr *manager, TfClientId id, DWORD) override {
        return Activate(manager, id);
    }
    STDMETHODIMP Deactivate() override {
        popup_.Hide();
        if (thread_source_ && source_cookie_ != TF_INVALID_COOKIE)
            thread_source_->UnadviseSink(source_cookie_);
        source_cookie_ = TF_INVALID_COOKIE;
        thread_source_.Reset();
        if (active_ && keystrokes_) keystrokes_->UnadviseKeyEventSink(client_id_);
        active_ = false;
        keystrokes_.Reset();
        thread_mgr_.Reset();
        composition_.Reset();
        core_.Reset();
        client_id_ = TF_CLIENTID_NULL;
        return S_OK;
    }
    STDMETHODIMP OnSetFocus(BOOL foreground) override {
        if (!foreground) { core_.Reset(); popup_.Hide(); }
        return S_OK;
    }
    STDMETHODIMP OnTestKeyDown(ITfContext *, WPARAM key, LPARAM, BOOL *eaten) override {
        if (!eaten) return E_POINTER;
        *eaten = CanHandle(key) ? TRUE : FALSE;
        return S_OK;
    }
    STDMETHODIMP OnTestKeyUp(ITfContext *, WPARAM, LPARAM, BOOL *eaten) override {
        if (!eaten) return E_POINTER;
        *eaten = FALSE;
        return S_OK;
    }
    STDMETHODIMP OnKeyUp(ITfContext *, WPARAM, LPARAM, BOOL *eaten) override {
        if (!eaten) return E_POINTER;
        *eaten = FALSE;
        return S_OK;
    }
    STDMETHODIMP OnKeyDown(ITfContext *context, WPARAM key, LPARAM, BOOL *eaten) override {
        if (!eaten) return E_POINTER;
        *eaten = FALSE;
        if (!context || !CanHandle(key)) return S_OK;
        TF_STATUS status{};
        if (FAILED(context->GetStatus(&status)) || (status.dwDynamicFlags & TF_SD_READONLY)) return S_OK;
        auto frame = core_.Press(Keysym(key));
        if (!frame.handled) return S_OK;
        auto edit = new (std::nothrow) KeyEdit(this, context, frame);
        if (!edit) { core_.Reset(); return E_OUTOFMEMORY; }
        HRESULT session_hr = E_FAIL;
        HRESULT hr = context->RequestEditSession(client_id_, edit,
                                                  TF_ES_SYNC | TF_ES_READWRITE, &session_hr);
        edit->Release();
        if (FAILED(hr) || FAILED(session_hr)) {
            core_.Reset();
            popup_.Hide();
            return S_OK; // Do not swallow user keys if no writable edit session exists.
        }
        popup_.Show(frame);
        *eaten = TRUE;
        return S_OK;
    }
    STDMETHODIMP OnPreservedKey(ITfContext *, REFGUID, BOOL *eaten) override {
        if (!eaten) return E_POINTER;
        *eaten = FALSE; return S_OK;
    }
    STDMETHODIMP OnCompositionTerminated(TfEditCookie, ITfComposition *) override {
        composition_.Reset();
        core_.Reset();
        popup_.Hide();
        return S_OK;
    }
    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr *) override { return S_OK; }
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr *) override { return S_OK; }
    STDMETHODIMP OnSetFocus(ITfDocumentMgr *, ITfDocumentMgr *) override {
        core_.Reset(); popup_.Hide(); return S_OK;
    }
    STDMETHODIMP OnPushContext(ITfContext *) override { return S_OK; }
    STDMETHODIMP OnPopContext(ITfContext *) override {
        core_.Reset(); popup_.Hide(); return S_OK;
    }
};

class Factory final : public IClassFactory {
    std::atomic<ULONG> refs_{1};
public:
    Factory() { ++g_objects; }
    ~Factory() { --g_objects; }
    STDMETHODIMP QueryInterface(REFIID id, void **out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (IsEqualIID(id, IID_IUnknown) || IsEqualIID(id, IID_IClassFactory))
            *out = static_cast<IClassFactory *>(this);
        if (!*out) return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG n = --refs_; if (!n) delete this; return n;
    }
    STDMETHODIMP CreateInstance(IUnknown *outer, REFIID id, void **out) override {
        if (outer) return CLASS_E_NOAGGREGATION;
        if (!out) return E_POINTER;
        *out = nullptr;
        auto instance = new (std::nothrow) TextService();
        if (!instance) return E_OUTOFMEMORY;
        HRESULT hr = instance->QueryInterface(id, out);
        instance->Release();
        return hr;
    }
    STDMETHODIMP LockServer(BOOL lock) override {
        if (lock) ++g_locks; else --g_locks;
        return S_OK;
    }
};

static std::wstring GuidText(const GUID &guid) {
    wchar_t buffer[64] = {};
    StringFromGUID2(guid, buffer, static_cast<int>(std::size(buffer)));
    return buffer;
}

static HRESULT WriteComRegistration() {
    std::wstring base = L"Software\\Classes\\CLSID\\" + GuidText(CLSID_WowTypeTsf);
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, base.c_str(), 0, nullptr, 0, KEY_WRITE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS) return E_ACCESSDENIED;
    static const wchar_t description[] = L"WowType Windows Prototype TSF";
    RegSetValueExW(key, nullptr, 0, REG_SZ,
                   reinterpret_cast<const BYTE *>(description), sizeof(description));
    RegCloseKey(key);
    base += L"\\InprocServer32";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, base.c_str(), 0, nullptr, 0, KEY_WRITE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS) return E_ACCESSDENIED;
    auto path = ModuleFile();
    if (path.empty()) { RegCloseKey(key); return E_FAIL; }
    auto bytes = static_cast<DWORD>((path.size() + 1) * sizeof(wchar_t));
    LONG status = RegSetValueExW(key, nullptr, 0, REG_SZ,
                                reinterpret_cast<const BYTE *>(path.c_str()), bytes);
    static const wchar_t threading[] = L"Apartment";
    LONG thread_status = RegSetValueExW(key, L"ThreadingModel", 0, REG_SZ,
                                       reinterpret_cast<const BYTE *>(threading), sizeof(threading));
    RegCloseKey(key);
    return (status == ERROR_SUCCESS && thread_status == ERROR_SUCCESS) ? S_OK : E_FAIL;
}

static void DeleteComRegistration() {
    std::wstring key = L"Software\\Classes\\CLSID\\" + GuidText(CLSID_WowTypeTsf);
    RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str());
}

static HRESULT RegisterProfile() {
    ComPtr<ITfInputProcessorProfileMgr> profiles;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_PPV_ARGS(profiles.GetAddressOf()));
    if (FAILED(hr)) return hr;
    static const wchar_t description[] = L"WowType Chinese (prototype)";
    auto module = ModuleFile();
    if (module.empty()) return E_FAIL;
    hr = profiles->RegisterProfile(CLSID_WowTypeTsf, kLangZhCn, GUID_WowTypeProfile,
                                  description, static_cast<ULONG>(wcslen(description)),
                                  module.c_str(), static_cast<ULONG>(module.size()),
                                  0, nullptr, 0, TRUE, 0);
    if (FAILED(hr)) return hr;
    ComPtr<ITfCategoryMgr> categories;
    hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(categories.GetAddressOf()));
    if (FAILED(hr)) return hr;
    return categories->RegisterCategory(CLSID_WowTypeTsf, GUID_TFCAT_TIP_KEYBOARD, CLSID_WowTypeTsf);
}

static void UnregisterProfile() {
    ComPtr<ITfCategoryMgr> categories;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(categories.GetAddressOf()))))
        categories->UnregisterCategory(CLSID_WowTypeTsf, GUID_TFCAT_TIP_KEYBOARD, CLSID_WowTypeTsf);
    ComPtr<ITfInputProcessorProfileMgr> profiles;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                    CLSCTX_INPROC_SERVER, IID_PPV_ARGS(profiles.GetAddressOf()))))
        profiles->UnregisterProfile(CLSID_WowTypeTsf, kLangZhCn, GUID_WowTypeProfile, 0);
}

extern "C" BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid, REFIID id, void **out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!IsEqualCLSID(clsid, CLSID_WowTypeTsf)) return CLASS_E_CLASSNOTAVAILABLE;
    auto factory = new (std::nothrow) Factory();
    if (!factory) return E_OUTOFMEMORY;
    HRESULT hr = factory->QueryInterface(id, out);
    factory->Release();
    return hr;
}
extern "C" HRESULT __stdcall DllCanUnloadNow() {
    return (g_objects.load() == 0 && g_locks.load() == 0) ? S_OK : S_FALSE;
}
extern "C" HRESULT __stdcall DllRegisterServer() {
    HRESULT hr = WriteComRegistration();
    if (FAILED(hr)) return hr;
    HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
        DeleteComRegistration(); return initialized;
    }
    hr = RegisterProfile();
    if (initialized == S_OK || initialized == S_FALSE) CoUninitialize();
    if (FAILED(hr)) {
        // Best-effort cleanup of partial TSF registration and the HKCU COM class.
        initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (SUCCEEDED(initialized) || initialized == RPC_E_CHANGED_MODE) {
            UnregisterProfile();
            if (SUCCEEDED(initialized)) CoUninitialize();
        }
        DeleteComRegistration();
    }
    return hr;
}
extern "C" HRESULT __stdcall DllUnregisterServer() {
    HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(initialized) || initialized == RPC_E_CHANGED_MODE) {
        UnregisterProfile();
        if (SUCCEEDED(initialized)) CoUninitialize();
    }
    DeleteComRegistration();
    return S_OK;
}

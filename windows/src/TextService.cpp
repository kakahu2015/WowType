#ifdef _WIN32
#include "TextService.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <iterator>
#include <mutex>
#include <new>
#include <utility>

#include <windowsx.h>

using Microsoft::WRL::ComPtr;

namespace {
constexpr wchar_t kTimerClass[] = L"WowTypeTimerWindow";
constexpr UINT_PTR kTimer = 1;
constexpr UINT kTimerPeriodMs = 50;
constexpr UINT kToUnicodeNoStateChange = 1u << 2;
constexpr DWORD kTranslateDelayMs = 500;
constexpr size_t kSaveEvery = 20;

std::once_flag g_engineOnce;
WowtypeEngineHandle *g_sharedEngine = nullptr;
std::once_flag g_engineConfigOnce;

std::wstring ModuleDirectory() {
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(g_wowtypeModule, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return {};
    return std::filesystem::path(path).parent_path().wstring();
}

std::wstring AppDataDirectory() {
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"APPDATA", path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return {};
    return path;
}

std::string ToUtf8(const std::wstring &text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                        static_cast<int>(text.size()), result.data(), size,
                        nullptr, nullptr);
    return result;
}

std::wstring FromUtf8(const char *text) {
    if (!text || !*text) return {};
    const int length = static_cast<int>(strlen(text));
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text,
                                         length, nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, length,
                        result.data(), size);
    return result;
}

uint32_t LastCodepoint(const std::wstring &text) {
    if (text.empty()) return 0;
    const uint32_t last = static_cast<uint16_t>(text.back());
    if (last >= 0xdc00 && last <= 0xdfff && text.size() > 1) {
        const uint32_t high = static_cast<uint16_t>(text[text.size() - 2]);
        if (high >= 0xd800 && high <= 0xdbff)
            return 0x10000 + ((high - 0xd800) << 10) + (last - 0xdc00);
    }
    return last;
}

bool Enabled(const std::unordered_map<std::string, std::string> &config,
             const char *key, bool fallback) {
    auto it = config.find(key);
    if (it == config.end()) return fallback;
    return it->second == "True" || it->second == "true" || it->second == "1";
}

unsigned Number(const std::unordered_map<std::string, std::string> &config,
                const char *key, unsigned fallback) {
    auto it = config.find(key);
    if (it == config.end()) return fallback;
    try {
        return static_cast<unsigned>(std::stoul(it->second));
    } catch (...) {
        return fallback;
    }
}

std::string Value(const std::unordered_map<std::string, std::string> &config,
                  const char *key, const char *fallback = "") {
    auto it = config.find(key);
    return it == config.end() ? fallback : it->second;
}

std::unordered_map<std::string, std::string> ReadConfig(const std::wstring &path) {
    std::unordered_map<std::string, std::string> result;
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t equals = line.find('=');
        if (equals == std::string::npos || equals == 0 || line[0] == '#' || line[0] == '[')
            continue;
        std::string key = line.substr(0, equals);
        std::string value = line.substr(equals + 1);
        auto trim = [](std::string &s) {
            const size_t first = s.find_first_not_of(" \t\"");
            const size_t last = s.find_last_not_of(" \t\"");
            s = first == std::string::npos ? std::string() : s.substr(first, last - first + 1);
        };
        trim(key);
        trim(value);
        result[key] = value;
    }
    return result;
}

bool IsDown(int virtualKey) {
    return (GetKeyState(virtualKey) & 0x8000) != 0;
}

} // namespace

class FrameEditSession final : public ITfEditSession {
public:
    FrameEditSession(TextService *owner, ITfContext *context,
                     std::wstring commit, std::wstring preedit)
        : owner_(owner), context_(context), commit_(std::move(commit)),
          preedit_(std::move(preedit)) {
        owner_->AddRef();
    }
    ~FrameEditSession() { owner_->Release(); }

    STDMETHODIMP QueryInterface(REFIID riid, void **object) override {
        if (!object) return E_INVALIDARG;
        *object = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfEditSession)) {
            *object = static_cast<ITfEditSession *>(this);
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

    STDMETHODIMP DoEditSession(TfEditCookie cookie) override {
        ComPtr<ITfRange> range;
        HRESULT hr = S_OK;
        if (owner_->composition_) {
            hr = owner_->composition_->GetRange(&range);
            if (FAILED(hr)) return hr;
            const std::wstring &replacement = commit_.empty() ? preedit_ : commit_;
            hr = range->SetText(cookie, 0, replacement.c_str(),
                                static_cast<LONG>(replacement.size()));
            if (FAILED(hr)) return hr;

            if (!commit_.empty() || preedit_.empty()) {
                hr = owner_->composition_->EndComposition(cookie);
                if (FAILED(hr)) return hr;
                owner_->composition_.Reset();
                hr = SetCaret(cookie, range.Get());
                if (FAILED(hr)) return hr;
            } else {
                return SetCaret(cookie, range.Get());
            }
        } else if (!commit_.empty()) {
            ComPtr<ITfInsertAtSelection> insert;
            hr = context_->QueryInterface(IID_PPV_ARGS(&insert));
            if (FAILED(hr)) return hr;
            hr = insert->InsertTextAtSelection(cookie, TF_IAS_NOQUERY,
                                               commit_.c_str(),
                                               static_cast<LONG>(commit_.size()), nullptr);
            if (FAILED(hr)) return hr;
        }

        if (preedit_.empty()) return S_OK;

        ComPtr<ITfInsertAtSelection> insert;
        hr = context_->QueryInterface(IID_PPV_ARGS(&insert));
        if (FAILED(hr)) return hr;
        hr = insert->InsertTextAtSelection(cookie, TF_IAS_NO_DEFAULT_COMPOSITION,
                                           preedit_.c_str(),
                                           static_cast<LONG>(preedit_.size()), &range);
        if (FAILED(hr)) return hr;

        ComPtr<ITfContextComposition> compositionServices;
        hr = context_->QueryInterface(IID_PPV_ARGS(&compositionServices));
        if (FAILED(hr)) return hr;
        hr = compositionServices->StartComposition(
            cookie, range.Get(), static_cast<ITfCompositionSink *>(owner_),
            &owner_->composition_);
        if (FAILED(hr)) return hr;
        return SetCaret(cookie, range.Get());
    }

private:
    HRESULT SetCaret(TfEditCookie cookie, ITfRange *range) {
        if (!range) return E_POINTER;
        HRESULT hr = range->Collapse(cookie, TF_ANCHOR_END);
        if (FAILED(hr)) return hr;
        TF_SELECTION selection{};
        selection.range = range;
        selection.style.ase = TF_AE_END;
        selection.style.fInterimChar = FALSE;
        return context_->SetSelection(cookie, 1, &selection);
    }

    std::atomic<ULONG> references_{1};
    TextService *owner_;
    ComPtr<ITfContext> context_;
    std::wstring commit_;
    std::wstring preedit_;
};

namespace {

LRESULT CALLBACK TimerWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    TextService *service = reinterpret_cast<TextService *>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
        service = static_cast<TextService *>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(service));
    }
    if (service && message == WM_TIMER && wParam == kTimer) {
        service->OnTimer();
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

HWND CreateTimerWindow(HINSTANCE module, TextService *service) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = TimerWindowProc;
    wc.hInstance = module;
    wc.lpszClassName = kTimerClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return nullptr;
    return CreateWindowExW(0, kTimerClass, L"", 0, 0, 0, 0, 0,
                           HWND_MESSAGE, nullptr, module, service);
}

uint32_t KeysymForVirtualKey(WPARAM key) {
    switch (key) {
    case VK_BACK: return 0xff08;
    case VK_TAB: return 0xff09;
    case VK_RETURN: return 0xff0d;
    case VK_ESCAPE: return 0xff1b;
    case VK_LEFT: return 0xff51;
    case VK_UP: return 0xff52;
    case VK_RIGHT: return 0xff53;
    case VK_DOWN: return 0xff54;
    case VK_PRIOR: return 0xff55;
    case VK_NEXT: return 0xff56;
    case VK_DELETE: return 0xffff;
    case VK_MULTIPLY: return 0xffaa;
    case VK_ADD: return 0xffab;
    case VK_SUBTRACT: return 0xffad;
    case VK_DECIMAL: return 0xffae;
    case VK_DIVIDE: return 0xffaf;
    default:
        if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9)
            return 0xffb0 + static_cast<uint32_t>(key - VK_NUMPAD0);
        break;
    }

    BYTE keyboard[256]{};
    if (!GetKeyboardState(keyboard)) return 0;
    WCHAR translated[8]{};
    const UINT scan = MapVirtualKeyExW(static_cast<UINT>(key), MAPVK_VK_TO_VSC,
                                       GetKeyboardLayout(0));
    const int count = ToUnicodeEx(static_cast<UINT>(key), scan, keyboard, translated,
                                  static_cast<int>(std::size(translated)),
                                  kToUnicodeNoStateChange,
                                  GetKeyboardLayout(0));
    if (count == 1 && translated[0] >= 0x20 && translated[0] <= 0x7e)
        return static_cast<uint32_t>(translated[0]);
    if (key >= 'A' && key <= 'Z') return static_cast<uint32_t>(key);
    if (key >= '0' && key <= '9') return static_cast<uint32_t>(key);
    return 0;
}
}

TextService::TextService() {
    ++g_wowtypeObjects;
}

TextService::~TextService() {
    Deactivate();
    --g_wowtypeObjects;
}

STDMETHODIMP TextService::QueryInterface(REFIID riid, void **object) {
    if (!object) return E_INVALIDARG;
    *object = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfTextInputProcessor)) {
        *object = static_cast<ITfTextInputProcessor *>(this);
    } else if (IsEqualIID(riid, IID_ITfKeyEventSink)) {
        *object = static_cast<ITfKeyEventSink *>(this);
    } else if (IsEqualIID(riid, IID_ITfCompositionSink)) {
        *object = static_cast<ITfCompositionSink *>(this);
    } else if (IsEqualIID(riid, IID_ITfThreadMgrEventSink)) {
        *object = static_cast<ITfThreadMgrEventSink *>(this);
    } else {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) TextService::AddRef() { return ++references_; }

STDMETHODIMP_(ULONG) TextService::Release() {
    const ULONG remaining = --references_;
    if (remaining == 0) delete this;
    return remaining;
}

void TextService::LoadSettings() {
    installDirectory_ = ModuleDirectory();
    dataDirectory_ = (std::filesystem::path(installDirectory_) / L"data").wstring();
    supportDirectory_ = (std::filesystem::path(AppDataDirectory()) / L"WowType").wstring();
    std::error_code error;
    std::filesystem::create_directories(supportDirectory_, error);
    userDictionaryPath_ = (std::filesystem::path(supportDirectory_) / L"user.tsv").wstring();
    historyPath_ = (std::filesystem::path(supportDirectory_) / L"history.tsv").wstring();
    translateKeyPath_ = (std::filesystem::path(supportDirectory_) / L"translate.key").wstring();
    const std::wstring configPath = (std::filesystem::path(supportDirectory_) / L"wowtype.conf").wstring();
    config_ = ReadConfig(configPath);
}

bool TextService::InitializeCore() {
    LoadSettings();
    std::call_once(g_engineOnce, [this] {
        const auto lexicon = ToUtf8((std::filesystem::path(dataDirectory_) / L"lexicon.tsv").wstring());
        const auto bigram = ToUtf8((std::filesystem::path(dataDirectory_) / L"words.lm").wstring());
        // TSF runs in-process, so every host that switches to this input method pays the
        // initialisation cost once. Parsing the 37 MB word language model dominates that
        // cost, so it is opt-in: without it startup only parses the lexicon snapshot and
        // the switcher feels immediate, at the price of weaker long-sentence ranking.
        const bool preload = Enabled(config_, "PreloadLanguageModel", false);
        g_sharedEngine = preload ? wowtype_engine_new(lexicon.c_str(), bigram.c_str())
                                 : wowtype_engine_new(lexicon.c_str(), nullptr);
        if (!g_sharedEngine) g_sharedEngine = wowtype_engine_new(lexicon.c_str(), nullptr);
        // The engine is shared for this process and has no per-instance owner. Keep
        // this DLL loaded so its once_flag and the Rust handle stay valid together.
        if (g_sharedEngine) ++g_wowtypeLocks;
    });
    engine_ = g_sharedEngine;
    if (!engine_) return false;

    std::call_once(g_engineConfigOnce, [this] { ApplyEngineSettings(); });

    WowtypeSessionConfig config{};
    config.size = sizeof(config);
    config.page_size = std::clamp(Number(config_, "PageSize", 9), 3u, 9u);
    config.calculator = Enabled(config_, "Calculator", true);
    config.date_time = Enabled(config_, "DateTime", true);
    config.emoji_suggest = Enabled(config_, "EmojiSuggest", true);
    config.horizontal = Value(config_, "CandidateLayout", "Horizontal") != "Vertical";
    config.page_keys = WOWTYPE_PAGE_KEYS_MINUS;
    config.select_keys = WOWTYPE_SELECT_KEYS_OFF;
    config.pick_char = Enabled(config_, "PickChar", true);
    session_ = wowtype_session_new(engine_, &config);
    if (!session_) return false;

    translator_ = wowtype_translate_new();
    if (translator_) {
        const std::string base = Value(config_, "TranslateBaseUrl");
        const std::string model = Value(config_, "TranslateModel");
        const std::string target = Value(config_, "TranslateTarget", "en");
        const auto keyFile = ToUtf8(translateKeyPath_);
        wowtype_translate_configure(translator_, base.c_str(), model.c_str(), target.c_str(),
                                    Enabled(config_, "TranslateFormal", false) ? 1 : 0,
                                    keyFile.c_str());
        if (Enabled(config_, "TranslateLocal", false)) {
            const auto localModel = ToUtf8((std::filesystem::path(supportDirectory_) /
                                           L"models" / L"HY-MT1.5-1.8B-Q4_K_M.gguf").wstring());
            const std::string server = Value(config_, "TranslateLlamaServer");
            wowtype_translate_configure_local(translator_, localModel.c_str(), server.c_str(),
                                              Number(config_, "TranslateIdleMinutes", 5) * 60);
        }
    }
    return true;
}

void TextService::ApplyEngineSettings() {
    wowtype_engine_set_max_candidates(engine_, 1000);
    wowtype_engine_set_typo_correction(engine_, Enabled(config_, "TypoCorrection", true));
    wowtype_engine_set_rare_ext_chars(engine_, Enabled(config_, "RareExtChars", false));
    wowtype_engine_set_fuzzy(engine_, Enabled(config_, "Fuzzy", false));
    unsigned groups = 0;
    const char *groupKeys[] = {"FuzzyZh", "FuzzyCh", "FuzzySh", "FuzzyNL", "FuzzyFH",
                               "FuzzyRL", "FuzzyAn", "FuzzyEn", "FuzzyIn", "FuzzyIan",
                               "FuzzyUan", "FuzzyUn"};
    for (unsigned i = 0; i < std::size(groupKeys); ++i)
        if (Enabled(config_, groupKeys[i], i < 3 || (i >= 6 && i <= 10))) groups |= 1u << i;
    wowtype_engine_set_fuzzy_groups(engine_, groups);
    wowtype_engine_set_shuangpin(engine_, Enabled(config_, "Shuangpin", false));
    const std::string scheme = Value(config_, "ShuangpinScheme", "Ziranma");
    wowtype_engine_set_shuangpin_scheme(engine_, scheme.c_str());
    wowtype_engine_set_traditional(engine_, Enabled(config_, "Traditional", false));

    const auto data = std::filesystem::path(dataDirectory_);
    const auto englishPath = ToUtf8((data / L"english.txt").wstring());
    const auto conversionPath = ToUtf8((data / L"s2t.tsv").wstring());
    const auto userPath = ToUtf8(userDictionaryPath_);
    const auto historyPath = ToUtf8(historyPath_);
    wowtype_engine_load_english(engine_, englishPath.c_str());
    wowtype_engine_load_conversion(engine_, conversionPath.c_str());
    wowtype_engine_load_user_dict(engine_, userPath.c_str());
    wowtype_engine_load_history(engine_, historyPath.c_str());
    const auto customPath = ToUtf8((std::filesystem::path(supportDirectory_) / L"custom.tsv").wstring());
    wowtype_engine_load_custom_words(engine_, customPath.c_str());
}

STDMETHODIMP TextService::Activate(ITfThreadMgr *threadManager, TfClientId clientId) {
    if (!threadManager) return E_INVALIDARG;
    threadManager_ = threadManager;
    clientId_ = clientId;
    if (!InitializeCore()) return E_FAIL;

    HRESULT hr = threadManager_->QueryInterface(IID_PPV_ARGS(&keystrokeManager_));
    if (FAILED(hr)) return hr;
    hr = keystrokeManager_->AdviseKeyEventSink(clientId_, this, TRUE);
    if (FAILED(hr)) return hr;

    ComPtr<ITfSource> source;
    hr = threadManager_->QueryInterface(IID_PPV_ARGS(&source));
    if (SUCCEEDED(hr)) {
        hr = source->AdviseSink(IID_ITfThreadMgrEventSink,
                                static_cast<ITfThreadMgrEventSink *>(this),
                                &threadEventCookie_);
        if (FAILED(hr)) threadEventCookie_ = TF_INVALID_COOKIE;
    }

    if (!candidateWindow_.Create(g_wowtypeModule)) {
        Deactivate();
        return E_FAIL;
    }
    candidateWindow_.SetSelectHandler([this](size_t index) { SelectCandidate(index); });
    candidateWindow_.SetPageHandler([this](int direction) { ChangeCandidatePage(direction); });
    timerWindow_ = CreateTimerWindow(g_wowtypeModule, this);
    return S_OK;
}

STDMETHODIMP TextService::Deactivate() {
    if (timerWindow_) {
        if (timerId_) KillTimer(timerWindow_, timerId_);
        DestroyWindow(timerWindow_);
        timerWindow_ = nullptr;
        timerId_ = 0;
    }
    candidateWindow_.Hide();
    if (translator_) {
        wowtype_translate_cancel(translator_);
        wowtype_translate_free(translator_);
        translator_ = nullptr;
    }
    if (composition_) composition_.Reset();
    activeContext_.Reset();
    if (keystrokeManager_) {
        keystrokeManager_->UnadviseKeyEventSink(clientId_);
        keystrokeManager_.Reset();
    }
    if (threadManager_ && threadEventCookie_ != TF_INVALID_COOKIE) {
        ComPtr<ITfSource> source;
        if (SUCCEEDED(threadManager_->QueryInterface(IID_PPV_ARGS(&source))))
            source->UnadviseSink(threadEventCookie_);
        threadEventCookie_ = TF_INVALID_COOKIE;
    }
    SaveLearning();
    if (session_) {
        wowtype_session_free(session_);
        session_ = nullptr;
    }
    threadManager_.Reset();
    clientId_ = TF_CLIENTID_NULL;
    mode_ = Mode::Chinese;
    return S_OK;
}

void TextService::SetActiveContext(ITfContext *context) {
    if (activeContext_.Get() == context) return;
    if (translator_) wowtype_translate_cancel(translator_);
    if (composition_) composition_.Reset();
    candidateWindow_.Hide();
    if (session_) wowtype_session_reset(session_, 0);
    translationCommitted_.clear();
    translationShown_.clear();
    translationSourceShown_.clear();
    translationStarted_ = false;
    translationDone_ = false;
    translationPending_ = false;
    translationDueAt_ = 0;
    activeContext_ = context;
}

STDMETHODIMP TextService::OnSetFocus(BOOL) { return S_OK; }

STDMETHODIMP TextService::OnInitDocumentMgr(ITfDocumentMgr *) { return S_OK; }
STDMETHODIMP TextService::OnUninitDocumentMgr(ITfDocumentMgr *) { return S_OK; }
STDMETHODIMP TextService::OnPushContext(ITfContext *) { return S_OK; }
STDMETHODIMP TextService::OnPopContext(ITfContext *) { return S_OK; }

STDMETHODIMP TextService::OnSetFocus(ITfDocumentMgr *focus, ITfDocumentMgr *) {
    ComPtr<ITfContext> context;
    if (focus && SUCCEEDED(focus->GetTop(&context))) SetActiveContext(context.Get());
    else SetActiveContext(nullptr);
    return S_OK;
}

bool TextService::IsShiftKey(WPARAM key) const {
    return key == VK_SHIFT || key == VK_LSHIFT || key == VK_RSHIFT;
}

bool TextService::IsPrintableKey(WPARAM key) const {
    const uint32_t keysym = KeysymForVirtualKey(key);
    return keysym >= 0x20 && keysym <= 0x7e;
}

bool TextService::ShouldEatKey(ITfContext *, WPARAM key, LPARAM) const {
    if (IsShiftKey(key)) {
        const std::string setting = Value(config_, "ShiftToggle", "Both");
        if (setting == "Off") return false;
        if (setting == "Left" && key == VK_RSHIFT) return false;
        if (setting == "Right" && key == VK_LSHIFT) return false;
        return true;
    }
    if (mode_ == Mode::English) return false;
    const bool control = IsDown(VK_CONTROL);
    const bool alt = IsDown(VK_MENU);
    const bool super = IsDown(VK_LWIN) || IsDown(VK_RWIN);
    if (control || alt || super) {
        if (mode_ != Mode::Translate && !alt && !super &&
            ((control && (key == VK_BACK || key == VK_DELETE)) ||
             (IsDown(VK_SHIFT) && key == VK_DELETE))) return true;
        return false;
    }
    if (mode_ == Mode::Translate &&
        (key == VK_RETURN || key == VK_SPACE || key == VK_ESCAPE)) return true;
    if (mode_ == Mode::Translate && key == VK_BACK && !translationCommitted_.empty()) return true;
    if (IsPrintableKey(key)) return true;
    const char *input = session_ ? wowtype_session_input(session_) : nullptr;
    if (!composition_ && (!input || !*input)) return false;
    return key == VK_BACK || key == VK_RETURN || key == VK_SPACE || key == VK_ESCAPE ||
           key == VK_LEFT || key == VK_RIGHT || key == VK_UP || key == VK_DOWN ||
           key == VK_PRIOR || key == VK_NEXT || key == VK_TAB || key == VK_DELETE;
}

STDMETHODIMP TextService::OnTestKeyDown(ITfContext *context, WPARAM key,
                                        LPARAM flags, BOOL *eaten) {
    if (!eaten) return E_INVALIDARG;
    *eaten = ShouldEatKey(context, key, flags) ? TRUE : FALSE;
    return S_OK;
}

STDMETHODIMP TextService::OnTestKeyUp(ITfContext *, WPARAM key, LPARAM, BOOL *eaten) {
    if (!eaten) return E_INVALIDARG;
    *eaten = IsShiftKey(key) && shiftKeyEaten_ ? TRUE : FALSE;
    return S_OK;
}

void TextService::MarkShiftInterrupted() {
    if (shiftDown_) shiftInterrupted_ = true;
}

STDMETHODIMP TextService::OnKeyDown(ITfContext *context, WPARAM key,
                                    LPARAM flags, BOOL *eaten) {
    if (!eaten) return E_INVALIDARG;
    *eaten = FALSE;
    if (!context) return S_OK;
    SetActiveContext(context);
    if (IsShiftKey(key)) {
        shiftDown_ = true;
        shiftInterrupted_ = IsDown(VK_CONTROL) || IsDown(VK_MENU);
        shiftDownAt_ = GetTickCount();
        shiftKeyEaten_ = ShouldEatKey(context, key, flags);
        *eaten = shiftKeyEaten_ ? TRUE : FALSE;
        return S_OK;
    }
    MarkShiftInterrupted();
    if (!ShouldEatKey(context, key, flags)) return S_OK;
    if (mode_ == Mode::Translate) HandleTranslationKey(context, key, flags, eaten);
    else HandleChineseKey(context, key, flags, eaten);
    return S_OK;
}

STDMETHODIMP TextService::OnKeyUp(ITfContext *context, WPARAM key,
                                  LPARAM, BOOL *eaten) {
    if (!eaten) return E_INVALIDARG;
    *eaten = FALSE;
    if (!IsShiftKey(key) || !shiftDown_) return S_OK;
    const DWORD elapsed = GetTickCount() - shiftDownAt_;
    const bool toggle = !shiftInterrupted_ && elapsed <= 500;
    const bool consumed = shiftKeyEaten_;
    shiftDown_ = false;
    shiftInterrupted_ = false;
    shiftKeyEaten_ = false;
    if (toggle) SwitchMode();
    *eaten = consumed ? TRUE : FALSE;
    if (context) SetActiveContext(context);
    return S_OK;
}

STDMETHODIMP TextService::OnPreservedKey(ITfContext *, REFGUID, BOOL *eaten) {
    if (!eaten) return E_INVALIDARG;
    *eaten = FALSE;
    return S_OK;
}

bool TextService::MakeCoreKey(WPARAM key, LPARAM flags, WowtypeKey *out) const {
    if (!out) return false;
    const uint32_t keysym = KeysymForVirtualKey(key);
    if (!keysym) return false;
    *out = {};
    out->size = sizeof(*out);
    out->keysym = keysym;
    out->modifiers = 0;
    if (IsDown(VK_SHIFT)) out->modifiers |= WOWTYPE_MOD_SHIFT;
    if (GetKeyState(VK_CAPITAL) & 1) out->modifiers |= WOWTYPE_MOD_CAPS;
    if (IsDown(VK_CONTROL)) out->modifiers |= WOWTYPE_MOD_CTRL;
    if (IsDown(VK_MENU)) out->modifiers |= WOWTYPE_MOD_ALT;
    if (GetKeyState(VK_NUMLOCK) & 1) out->modifiers |= WOWTYPE_MOD_NUMLOCK;
    if (IsDown(VK_LWIN) || IsDown(VK_RWIN)) out->modifiers |= WOWTYPE_MOD_SUPER;
    out->release = 0;
    out->prev_char = 0;
    out->action = WOWTYPE_KEY_ACTION_NONE;
    (void)flags;
    return true;
}

void TextService::HandleChineseKey(ITfContext *context, WPARAM key,
                                  LPARAM flags, BOOL *eaten) {
    WowtypeKey input{};
    if (!session_ || !MakeCoreKey(key, flags, &input)) return;
    const WowtypeFrame *frame = wowtype_session_key(session_, &input);
    if (!frame || !frame->handled) {
        if (!IsPrintableKey(key)) return;

        std::wstring commit;
        for (size_t i = 0; i < 16; ++i) {
            const char *pending = wowtype_session_input(session_);
            if (!pending || !*pending) break;
            WowtypeKey space{};
            space.size = sizeof(space);
            space.keysym = 0x20;
            const WowtypeFrame *flush = wowtype_session_key(session_, &space);
            if (!flush || !flush->handled) break;
            if (flush->commit && *flush->commit) commit += FromUtf8(flush->commit);
        }

        const int style = Value(config_, "PunctuationStyle", "Fullwidth") == "Halfwidth" ? 1 : 0;
        const char *punctuation = wowtype_punct(static_cast<int>(input.keysym), style,
                                                static_cast<int>(LastCodepoint(commit)));
        commit += punctuation ? FromUtf8(punctuation)
                              : std::wstring(1, static_cast<wchar_t>(input.keysym));
        *eaten = SUCCEEDED(ApplyText(commit, {})) ? TRUE : FALSE;
        UpdateCandidateWindow(wowtype_session_frame(session_));
        return;
    }
    if (frame->forgot) SaveLearning();
    if (frame->commit && *frame->commit) {
        if (++commitsSinceSave_ >= kSaveEvery) SaveLearning();
    }
    *eaten = SUCCEEDED(ApplyFrame(frame)) ? TRUE : FALSE;
}

HRESULT TextService::ApplyFrame(const WowtypeFrame *frame) {
    if (!frame) return E_INVALIDARG;
    const std::wstring commit = FromUtf8(frame->commit);
    const std::wstring preedit = FromUtf8(frame->preedit);
    HRESULT hr = ApplyText(commit, preedit);
    if (FAILED(hr)) return hr;
    UpdateCandidateWindow(frame);
    return S_OK;
}

HRESULT TextService::ApplyText(const std::wstring &commit, const std::wstring &preedit) {
    if (!activeContext_) return E_UNEXPECTED;
    auto *edit = new (std::nothrow) FrameEditSession(this, activeContext_.Get(), commit, preedit);
    if (!edit) return E_OUTOFMEMORY;
    HRESULT sessionResult = E_FAIL;
    const HRESULT hr = activeContext_->RequestEditSession(clientId_, edit,
                                                          TF_ES_SYNC | TF_ES_READWRITE,
                                                          &sessionResult);
    edit->Release();
    if (FAILED(hr)) return hr;
    return sessionResult;
}

void TextService::UpdateCandidateWindow(const WowtypeFrame *frame, const RECT *anchor,
                                       const std::wstring &aux,
                                       const std::wstring &status) {
    if (!frame || !session_) return;
    std::vector<std::wstring> items;
    items.reserve(frame->cand_count);
    for (size_t i = 0; i < frame->cand_count; ++i)
        items.push_back(FromUtf8(wowtype_session_candidate(session_, i)));
    candidateWindow_.Show(items, frame->cand_cursor, anchor, aux, status,
                          frame->has_prev != 0, frame->has_next != 0);
}

HRESULT TextService::SelectCandidate(size_t index) {
    if (!session_ || !activeContext_) return E_UNEXPECTED;
    const WowtypeFrame *frame = wowtype_session_select(session_, index);
    return frame ? ApplyFrame(frame) : E_FAIL;
}

void TextService::ChangeCandidatePage(int direction) {
    if (!session_ || !activeContext_ || direction == 0) return;
    WowtypeKey key{};
    key.size = sizeof(key);
    key.keysym = direction < 0 ? 0xff55 : 0xff56;
    const WowtypeFrame *frame = wowtype_session_key(session_, &key);
    if (frame && frame->handled) ApplyFrame(frame);
}

void TextService::SaveLearning() {
    if (!engine_) return;
    const auto userPath = ToUtf8(userDictionaryPath_);
    const auto historyPath = ToUtf8(historyPath_);
    wowtype_engine_save_user_dict(engine_, userPath.c_str());
    wowtype_engine_save_history(engine_, historyPath.c_str());
    commitsSinceSave_ = 0;
}

STDMETHODIMP TextService::OnCompositionTerminated(TfEditCookie,
                                                  ITfComposition *composition) {
    if (composition_.Get() == composition) composition_.Reset();
    if (session_) wowtype_session_reset(session_, 0);
    candidateWindow_.Hide();
    return S_OK;
}

void TextService::SwitchMode() {
    const bool translateEnabled = Enabled(config_, "Translate", false) && translator_ &&
                                 wowtype_translate_ready(translator_);
    if (mode_ == Mode::Chinese) mode_ = Mode::English;
    else if (mode_ == Mode::English) mode_ = translateEnabled ? Mode::Translate : Mode::Chinese;
    else {
        if (!translationCommitted_.empty()) CommitTranslation(true);
        mode_ = Mode::Chinese;
    }
    if (mode_ != Mode::Translate) {
        if (session_) {
            const WowtypeFrame *frame = wowtype_session_reset(session_, 1);
            if (frame && frame->commit && *frame->commit) ApplyFrame(frame);
            else ApplyText({}, {});
        }
        candidateWindow_.Hide();
    } else {
        translationCommitted_.clear();
        translationShown_.clear();
        translationSourceShown_.clear();
    }
}

std::wstring TextService::TranslationSource() const {
    std::wstring result = translationCommitted_;
    if (!session_) return result;
    WowtypeCandidateInfo candidate{};
    candidate.size = sizeof(candidate);
    const size_t count = wowtype_session_candidate_total(session_);
    if (count && wowtype_session_candidate_info(session_, 0, &candidate) == 0 && candidate.commit)
        result += FromUtf8(candidate.commit);
    else {
        const char *remaining = wowtype_session_remaining(session_);
        if (remaining) result += FromUtf8(remaining);
    }
    return result;
}

void TextService::ClearTranslation(bool resetSession) {
    if (translator_) wowtype_translate_cancel(translator_);
    if (timerWindow_ && timerId_) {
        KillTimer(timerWindow_, timerId_);
        timerId_ = 0;
    }
    translationCommitted_.clear();
    translationShown_.clear();
    translationSourceShown_.clear();
    translationStarted_ = false;
    translationDone_ = false;
    translationPending_ = false;
    translationDueAt_ = 0;
    if (resetSession && session_) wowtype_session_reset(session_, 0);
    if (activeContext_) ApplyText({}, {});
    candidateWindow_.Hide();
}

void TextService::HandleTranslationKey(ITfContext *context, WPARAM key,
                                       LPARAM flags, BOOL *eaten) {
    if (!session_ || !translator_) return;
    if (key == VK_ESCAPE) {
        ClearTranslation(true);
        *eaten = TRUE;
        return;
    }
    if ((key == VK_RETURN || key == VK_SPACE) && !IsDown(VK_CONTROL) && !IsDown(VK_MENU)) {
        if (key == VK_RETURN && IsDown(VK_SHIFT)) {
            CommitTranslation(true);
        } else {
            const auto source = TranslationSource();
            if (source.empty()) {
                *eaten = FALSE;
                return;
            }
            if (translationDone_) {
                CommitTranslation(false);
            } else if (!translationStarted_) {
                translationPending_ = true;
                translationDueAt_ = GetTickCount();
                StartTranslationIfDue();
            } else {
                translationPending_ = true;
            }
        }
        *eaten = TRUE;
        return;
    }

    bool removedCommittedCharacter = false;
    if (key == VK_BACK && !translationCommitted_.empty()) {
        const char *remaining = wowtype_session_remaining(session_);
        if (!remaining || !*remaining) {
            if (translationCommitted_.back() >= 0xdc00 && translationCommitted_.back() <= 0xdfff &&
                translationCommitted_.size() > 1 &&
                translationCommitted_[translationCommitted_.size() - 2] >= 0xd800 &&
                translationCommitted_[translationCommitted_.size() - 2] <= 0xdbff) {
                translationCommitted_.resize(translationCommitted_.size() - 2);
            } else {
                translationCommitted_.pop_back();
            }
            removedCommittedCharacter = true;
        }
    }

    WowtypeKey input{};
    if (!MakeCoreKey(key, flags, &input)) return;
    const WowtypeFrame *frame = wowtype_session_key(session_, &input);
    if (!frame || !frame->handled) {
        if (removedCommittedCharacter) {
            if (translationStarted_) wowtype_translate_cancel(translator_);
            translationStarted_ = false;
            translationDone_ = false;
            translationPending_ = false;
            translationShown_.clear();
            translationSourceShown_.clear();
            translationDueAt_ = translationCommitted_.empty()
                ? 0 : GetTickCount() + kTranslateDelayMs;
            ApplyText({}, {});
            if (!translationCommitted_.empty() && !timerId_ && timerWindow_)
                timerId_ = SetTimer(timerWindow_, kTimer, kTimerPeriodMs, nullptr);
            candidateWindow_.Show({}, -1, nullptr,
                                  L"互译：" + TranslationSource(),
                                  translationCommitted_.empty() ? L"" : L"停顿后自动翻译");
            *eaten = TRUE;
            return;
        }
        if (!IsPrintableKey(key)) return;

        for (size_t i = 0; i < 16; ++i) {
            const char *pending = wowtype_session_input(session_);
            if (!pending || !*pending) break;
            WowtypeKey space{};
            space.size = sizeof(space);
            space.keysym = 0x20;
            const WowtypeFrame *flush = wowtype_session_key(session_, &space);
            if (!flush || !flush->handled) break;
            if (flush->commit && *flush->commit)
                translationCommitted_ += FromUtf8(flush->commit);
        }
        const int style = Value(config_, "PunctuationStyle", "Fullwidth") == "Halfwidth" ? 1 : 0;
        const int previous = static_cast<int>(LastCodepoint(TranslationSource()));
        const char *punctuation = wowtype_punct(static_cast<int>(input.keysym), style, previous);
        translationCommitted_ += punctuation ? FromUtf8(punctuation)
                                              : std::wstring(1, static_cast<wchar_t>(input.keysym));
        if (translationStarted_) wowtype_translate_cancel(translator_);
        translationStarted_ = false;
        translationDone_ = false;
        translationPending_ = false;
        translationShown_.clear();
        translationSourceShown_.clear();
        translationDueAt_ = GetTickCount() + kTranslateDelayMs;
        if (!timerId_ && timerWindow_)
            timerId_ = SetTimer(timerWindow_, kTimer, kTimerPeriodMs, nullptr);
        ApplyText({}, {});
        candidateWindow_.Show({}, -1, nullptr, L"互译：" + TranslationSource(),
                              L"停顿后自动翻译；回车上屏，Shift+回车上屏原文");
        *eaten = TRUE;
        return;
    }
    if (removedCommittedCharacter) {
        // The session handled this key, so the committed tail is gone while the
        // preedit changed as usual. Reschedule translation with the updated text.
        if (translationStarted_) wowtype_translate_cancel(translator_);
        translationStarted_ = false;
        translationDone_ = false;
        translationPending_ = false;
        translationShown_.clear();
        translationSourceShown_.clear();
        translationDueAt_ = GetTickCount() + kTranslateDelayMs;
    }
    if (frame->commit && *frame->commit) translationCommitted_ += FromUtf8(frame->commit);
    if (frame->forgot) SaveLearning();
    if (translationStarted_) wowtype_translate_cancel(translator_);
    translationStarted_ = false;
    translationDone_ = false;
    translationPending_ = false;
    translationShown_.clear();
    translationSourceShown_.clear();
    translationDueAt_ = GetTickCount() + kTranslateDelayMs;
    if (!timerId_ && timerWindow_) timerId_ = SetTimer(timerWindow_, kTimer, kTimerPeriodMs, nullptr);
    ApplyText({}, {});
    const std::wstring source = TranslationSource();
    const std::wstring label = L"互译：" + source;
    const std::wstring status = L"停顿后自动翻译；回车上屏，Shift+回车上屏原文";
    candidateWindow_.Show({}, -1, nullptr, label, status);
    *eaten = TRUE;
    SetActiveContext(context);
}

void TextService::StartTranslationIfDue() {
    if (!translator_ || translationStarted_ || !translationDueAt_) return;
    if (static_cast<LONG>(GetTickCount() - translationDueAt_) < 0) return;
    const std::wstring source = TranslationSource();
    if (source.empty()) {
        translationDueAt_ = 0;
        return;
    }
    const std::string utf8 = ToUtf8(source);
    wowtype_translate_start(translator_, utf8.c_str());
    translationStarted_ = true;
    translationDone_ = false;
    translationSourceShown_ = source;
    translationDueAt_ = 0;
}

void TextService::PollTranslation() {
    if (!translator_ || !translationStarted_) return;
    const int state = wowtype_translate_poll(translator_);
    const char *text = wowtype_translate_text(translator_);
    const std::wstring updated = FromUtf8(text);
    if (updated != translationShown_) {
        translationShown_ = updated;
        ApplyCompositionText(translationShown_, false);
    }
    const char *source = wowtype_translate_source(translator_);
    const char *error = wowtype_translate_error(translator_);
    const std::wstring label = L"互译：" + FromUtf8(source);
    const std::wstring status = state == WOWTYPE_TRANSLATE_ERROR
        ? FromUtf8(error) : (state == WOWTYPE_TRANSLATE_DONE ? L"翻译完成" : L"正在翻译…");
    candidateWindow_.Show({}, -1, nullptr, label, status);
    if (state == WOWTYPE_TRANSLATE_DONE && translationPending_) CommitTranslation(false);
    if (state == WOWTYPE_TRANSLATE_ERROR) translationPending_ = false;
    if (state == WOWTYPE_TRANSLATE_DONE) translationDone_ = true;
    if (state == WOWTYPE_TRANSLATE_ERROR) translationDone_ = false;
    if (state == WOWTYPE_TRANSLATE_DONE || state == WOWTYPE_TRANSLATE_ERROR)
        translationStarted_ = false;
}

void TextService::OnTimer() {
    if (mode_ != Mode::Translate) return;
    StartTranslationIfDue();
    PollTranslation();
    if (!translationStarted_ && !translationDueAt_ && !translationPending_ && timerId_) {
        KillTimer(timerWindow_, timerId_);
        timerId_ = 0;
    }
}

void TextService::CommitTranslation(bool original) {
    std::wstring text = original ? TranslationSource() : translationShown_;
    if (text.empty()) text = TranslationSource();
    if (!text.empty()) ApplyText(text, {});
    ClearTranslation(true);
}

HRESULT TextService::ApplyCompositionText(const std::wstring &text, bool commit) {
    return ApplyText(commit ? text : std::wstring(), commit ? std::wstring() : text);
}

#endif

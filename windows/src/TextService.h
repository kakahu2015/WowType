#pragma once

#ifdef _WIN32
#include <windows.h>
#include <msctf.h>
#include <wrl/client.h>

#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

#include "CandidateWindow.h"
#include "wowtype_core.h"

extern const CLSID CLSID_WowTypeTextService;
extern const GUID GUID_WowTypeProfile;
extern HMODULE g_wowtypeModule;
extern std::atomic<long> g_wowtypeObjects;
extern std::atomic<long> g_wowtypeLocks;

class TextService final : public ITfTextInputProcessor,
                          public ITfKeyEventSink,
                          public ITfCompositionSink,
                          public ITfThreadMgrEventSink {
public:
    TextService();
    ~TextService();

    STDMETHODIMP QueryInterface(REFIID riid, void **object) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP Activate(ITfThreadMgr *threadManager, TfClientId clientId) override;
    STDMETHODIMP Deactivate() override;

    STDMETHODIMP OnSetFocus(BOOL foreground) override;
    STDMETHODIMP OnTestKeyDown(ITfContext *context, WPARAM key, LPARAM flags,
                              BOOL *eaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext *context, WPARAM key, LPARAM flags,
                            BOOL *eaten) override;
    STDMETHODIMP OnKeyDown(ITfContext *context, WPARAM key, LPARAM flags,
                          BOOL *eaten) override;
    STDMETHODIMP OnKeyUp(ITfContext *context, WPARAM key, LPARAM flags,
                        BOOL *eaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext *context, REFGUID key, BOOL *eaten) override;

    STDMETHODIMP OnCompositionTerminated(TfEditCookie cookie,
                                         ITfComposition *composition) override;

    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr *documentManager) override;
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr *documentManager) override;
    STDMETHODIMP OnSetFocus(ITfDocumentMgr *focus, ITfDocumentMgr *previous) override;
    STDMETHODIMP OnPushContext(ITfContext *context) override;
    STDMETHODIMP OnPopContext(ITfContext *context) override;

    HRESULT SelectCandidate(size_t index);
    void ChangeCandidatePage(int direction);
    void OnTimer();

private:
    friend class FrameEditSession;
    enum class Mode { Chinese, English, Translate };

    bool InitializeCore();
    void LoadSettings();
    void ApplyEngineSettings();
    void SetActiveContext(ITfContext *context);
    bool IsShiftKey(WPARAM key) const;
    bool IsPrintableKey(WPARAM key) const;
    bool ShouldEatKey(ITfContext *context, WPARAM key, LPARAM flags) const;
    bool MakeCoreKey(WPARAM key, LPARAM flags, WowtypeKey *out) const;
    void UpdateCandidateWindow(const WowtypeFrame *frame, const RECT *anchor = nullptr,
                               const std::wstring &aux = {},
                               const std::wstring &status = {});
    HRESULT ApplyText(const std::wstring &commit, const std::wstring &preedit);
    HRESULT ApplyFrame(const WowtypeFrame *frame);
    HRESULT ApplyCompositionText(const std::wstring &text, bool commit);
    void SaveLearning();
    void HandleChineseKey(ITfContext *context, WPARAM key, LPARAM flags, BOOL *eaten);
    void HandleTranslationKey(ITfContext *context, WPARAM key, LPARAM flags, BOOL *eaten);
    void SwitchMode();
    void ClearTranslation(bool resetSession);
    std::wstring TranslationSource() const;
    void StartTranslationIfDue();
    void PollTranslation();
    void CommitTranslation(bool original);
    void MarkShiftInterrupted();

    std::atomic<ULONG> references_{1};
    Microsoft::WRL::ComPtr<ITfThreadMgr> threadManager_;
    Microsoft::WRL::ComPtr<ITfKeystrokeMgr> keystrokeManager_;
    Microsoft::WRL::ComPtr<ITfContext> activeContext_;
    Microsoft::WRL::ComPtr<ITfComposition> composition_;
    DWORD threadEventCookie_ = TF_INVALID_COOKIE;
    TfClientId clientId_ = TF_CLIENTID_NULL;
    HWND timerWindow_ = nullptr;
    UINT_PTR timerId_ = 0;

    WowtypeEngineHandle *engine_ = nullptr;
    WowtypeSession *session_ = nullptr;
    WowtypeTranslator *translator_ = nullptr;
    CandidateWindow candidateWindow_;
    std::unordered_map<std::string, std::string> config_;
    std::wstring installDirectory_;
    std::wstring dataDirectory_;
    std::wstring supportDirectory_;
    std::wstring userDictionaryPath_;
    std::wstring historyPath_;
    std::wstring translateKeyPath_;
    uint32_t commitsSinceSave_ = 0;

    Mode mode_ = Mode::Chinese;
    bool shiftDown_ = false;
    bool shiftInterrupted_ = false;
    bool shiftKeyEaten_ = false;
    bool translationPending_ = false;
    bool translationStarted_ = false;
    bool translationDone_ = false;
    DWORD shiftDownAt_ = 0;
    DWORD translationDueAt_ = 0;
    std::wstring translationCommitted_;
    std::wstring translationShown_;
    std::wstring translationSourceShown_;
};

HRESULT CreateTextService(REFIID riid, void **object);
HRESULT RegisterTextService();
HRESULT UnregisterTextService();

#endif

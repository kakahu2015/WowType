# Windows 11 TSF prototype — Phase 2

Branch: windows/phase2-tsf
Parent: windows/phase1 at 2734d9eb4a8040710fde6c181be7e0f0d5083f10

## What this prototype implements

- Native x64 MSVC COM in-process DLL exporting DllGetClassObject, DllCanUnloadNow, DllRegisterServer and DllUnregisterServer.
- Windows TSF ITfTextInputProcessorEx and ITfKeyEventSink, using RequestEditSession to create/update/end composition text.
- Rust WowType C ABI DLL loaded by absolute path. Per-input-service opaque session and engine lifecycle.
- Prototype Win32 non-activating candidate popup with 1-9 key selection and selection/commit from Rust session.
- Per-user HKCU COM registration and TSF Chinese (Simplified) language profile + TIP keyboard category; uninstall removes registrations.
- PowerShell build, development installation and uninstall. Actions compiles and checks registration/unregistration on a Windows Server 2022 runner.

## Build / test

Run in x64 Visual Studio Developer PowerShell on Windows 11 with MSVC build tools, Windows SDK and x64 Rust MSVC installed:

    .\scripts\windows\Build-Tsf.ps1
    .\scripts\windows\Install-Tsf.ps1

Select "WowType Chinese (prototype)" in the Windows language/input methods settings. Open Notepad, switch input method via Win+Space and type nihao then Space. Confirm the inline composition, candidate window and 你好 insertion. To roll back:

    .\scripts\windows\Uninstall-Tsf.ps1

**This cannot be silently deployed to your Windows 11 device from ChatGPT.** Test on a spare device or VM first. No code signing yet. Do not install as a daily-use IME.

## Evidence matrix

| Test | CI Win Server | Windows 11 desktop |
| --- | --- | --- |
| x64 C++ TSF DLL compile/link | Windows TSF Prototype workflow | Pending |
| Required COM exports | CI dumpbin | Pending |
| HKCU registration/unregistration | CI regsvr32 | Pending |
| Actual profile present in system input method UI | Not covered | Confirmed: WowType appears in Chinese (Simplified) keyboard switcher (2026-11-14) |
| Composition, candidate window and selected Unicode insertion | Not covered | Browser inline ni'hao composition + 1. 你好 candidate confirmed; Space-to-commit still unverified (2026-11-14) |
| Word/Chrome/WeChat/VS Code focus changes | Not covered | Pending |
| CPU/per-key latency, unload, app crash isolation | Not covered | Pending |

## Known limitations (do not overstate readiness)

1. This is a minimal in-process TSF experiment. Focus changes and composition termination need broader native testing. The candidate popup does not yet support mouse selection or paging UI.
2. A demo lexicon ships with the artifact. It is not the full dictionary or trained language model. For full dictionary experiments, supply a legally distributable lexicon at data/lexicon.tsv and verify its license.
3. Microsoft TSF hosts can refuse synchronous edit sessions. The code then falls back to letting original keystrokes pass; a full error-recovery state machine is pending.
4. Initial Windows 11 manual UI evidence confirms profile visibility and live browser Pinyin composition with Chinese candidates. It does **not** yet confirm Unicode commit by Space, Notepad compatibility, offline voice recognition, local translation, MSIX, accessibility, signing, or production suitability.
5. User input and clipboard content are not logged, transmitted or persisted by the new Windows TSF code. Third-party models are not bundled.
6. Dev-mode registration is per-user. Uninstallation must occur with the same user account. Close all apps that may have loaded the text service before updating/uninstalling.
7. Per-document composition and lifetime behavior remain to be audited; do not use the prototype in password managers or sensitive applications.

## Next steps

- Confirm Space-to-commit of 你好 in the browser and Notepad; capture any focus switching and rollback errors.
- Guard password/input-scope and read-only contexts, add native COM activation and focus switching harness.
- Verify candidate popup placement and compact sizing in Chrome and Notepad after switching to ITfContextView::GetTextExt. Add TSF layout-change events and accessibility/UI-element integration.
- Only after stable testing, add full dictionary/setting UI, signing, release installer and Win11 arm64 feasibility.


## Phase 2 UI iteration: candidate layout

A real Windows 11 web input test on 2026-11-14 showed that the previous popup
was a fixed 340 x 160 rectangle detached from the insertion point. The next
prototype makes these changes in windows/tsf/text_service.cpp:

- Capture a caret range while a valid TF edit cookie is held, using
  ITfContext::GetActiveView and ITfContextView::GetTextExt. Its rectangle is
  already in screen coordinates. Prefer it over mouse or Win32 caret position.
- Query before mutating composition, since web text stores may return
  TS_E_NOLAYOUT just after SetText. If the host cannot provide a layout,
  preserve a valid earlier caret rectangle and then use a Win32 fallback.
- Size the non-activating popup from measured font and candidate rows, with
  monitor-edge clamping and flip-above-caret at the bottom of the screen.

IMPORTANT: screenshot confirms display only. It does not prove completion of
Space-to-commit. The geometry fix requires a fresh desktop test before approval.

### Upgrade test (from the previous DLL)

Switch to Microsoft Pinyin and close applications that may have loaded
WowTypeTSF.dll (notably the browser, Notepad or editors). Run the existing
Uninstall-Tsf.ps1 under the SAME Windows account; then install the new
artifact's Install-Tsf.ps1. If the old DLL remains in use, close the
applications or restart Windows before deleting/replacing it. Never
overwrite an in-use TSF DLL and treat a failed unregister as a blocker.


## Windows full-size offline lexicon (phase 2.1)

Windows artifacts now include the **official upstream WowType v0.3.12**
[rime-ice offline data release](https://github.com/Gloridust/WowType/releases/tag/v0.3.12)
rather than only the 14-entry test fixture. The pinned archive is
wowtype-data-rime-ice-2026.11.11.3.tar.gz, SHA-256
ea07c3e8cf66cbf7d0ebc23ab95cc96384e6f3808fd0cdee1f87905322e51d7c.

At BUILD time, scripts/windows/Get-OfflineLexicon.ps1 downloads and verifies the
pinned archive, extracts the full lexicon.tsv, words.lm and source/license notices,
and refuses packages with fewer than 500,000 data rows. The runtime Windows
TSF DLL NEVER accesses the network. It requires both files in
%LOCALAPPDATA%\Programs\WowTypeTSF\data and loads them through the Rust C ABI.
No fallback to demo_lexicon.tsv is allowed in the Windows frontend.

The full dictionary is based on [rime-ice](https://github.com/iDvel/rime-ice)
and is licensed **GPL-3.0**. The words.lm language model has separate
**CC BY-SA 4.0** attribution/requirements. The original upstream LICENSE and
SOURCE.txt are included alongside the data. This does not relicense the
WowType Rust/C++ source itself.

The CI runs the native C ABI smoke against the actual released TSV AND words.lm
before uploading the bundle; a 500k+ line count and ni hao / zhong guo dictionary
records are also checked independently during packaging.

### Upgrading an already installed demo-based TSF

Switch to Microsoft Pinyin, close applications with WowType loaded, and run the
OLD Uninstall-Tsf.ps1 under the same Windows account. Unzip a NEW full-lexicon
artifact and run its Install-Tsf.ps1. If an open process prevents removal,
restart Windows before trying to install the new DLL again. Installation of
a package lacking the real lexicon or words.lm now explicitly fails.

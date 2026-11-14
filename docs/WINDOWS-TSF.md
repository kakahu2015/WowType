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
| Actual profile present in system input method UI | Not covered | Pending |
| Composition, candidate window and selected Unicode insertion | Not covered | Pending |
| Word/Chrome/WeChat/VS Code focus changes | Not covered | Pending |
| CPU/per-key latency, unload, app crash isolation | Not covered | Pending |

## Known limitations (do not overstate readiness)

1. This is a minimal in-process TSF experiment. Focus changes and composition termination need broader native testing. The candidate popup does not yet support mouse selection or paging UI.
2. A demo lexicon ships with the artifact. It is not the full dictionary or trained language model. For full dictionary experiments, supply a legally distributable lexicon at data/lexicon.tsv and verify its license.
3. Microsoft TSF hosts can refuse synchronous edit sessions. The code then falls back to letting original keystrokes pass; a full error-recovery state machine is pending.
4. No Windows 11 desktop validation, offline voice recognition, local translation, app installer/MSIX, accessibility, signing, or production suitability claims are made.
5. User input and clipboard content are not logged, transmitted or persisted by the new Windows TSF code. Third-party models are not bundled.
6. Dev-mode registration is per-user. Uninstallation must occur with the same user account. Close all apps that may have loaded the text service before updating/uninstalling.
7. Per-document composition and lifetime behavior remain to be audited; do not use the prototype in password managers or sensitive applications.

## Next steps

- Obtain real Windows 11 screen/ETW observation and test Notepad composition, candidate selection, focus transitions and rollback.
- Guard password/input-scope and read-only contexts, add native COM activation and focus switching harness.
- Improve candidate geometry using ITfContextView::GetTextExt, and implement accessibility/UI-element integration.
- Only after stable testing, add full dictionary/setting UI, signing, release installer and Win11 arm64 feasibility.

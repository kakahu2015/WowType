# WowType Windows 11 Independent Fork — Phase 1

Branch: windows/phase1 in kakahu2015/WowType
Base commit: 38eb269989a4c54c96c4b5749a13294750498b5e
Target: Windows 11 x64 / x86_64-pc-windows-msvc

## Changes

- core/src/llama.rs: portable .exe lookup, USERPROFILE and LOCALAPPDATA paths, Unix-only fixture gates.
- core/src/lexicon.rs: Windows LOCALAPPDATA lexicon snapshot fallback.
- core/src/session.rs: GetLocalTime and Windows sanity test.
- core/src/translate.rs: gate Unix-only local-model test (not a Windows translation implementation).
- windows/core/abi_smoke.c: load the real WowType DLL and verify ABI, Pinyin candidates and composition commit.
- scripts/windows/Test-Core.ps1: native Rust tests, all-features check, release DLL and MSVC ABI smoke.
- .github/workflows/windows-core.yml: Windows x64 build/test with artifact hashes.

## Acceptance

From repository root in **Visual Studio Developer PowerShell (x64)**, with Windows SDK, MSVC Build Tools and x64 Rust MSVC toolchain installed, run:

    .\scripts\windows\Test-Core.ps1

Or use GitHub Actions / Windows Core Validation. The Windows Server 2022 runner does **not** replace Windows 11 desktop testing.

A green run should establish: no Rust/MSVC compile failure, core/voice tests pass, workspace check succeeds, real DLL loads, C ABI major matches, candidates include nihao -> 你好 and zhongguo -> 中国, session commits 你好, and SHA-256 is emitted. It does not verify a usable Windows input method.

## Deferred

- Windows-specific process fixtures for local llama-server and translation.
- Windows microphone and voice model provisioning, third-party licensing.
- TSF COM server, document manager, edit sessions, composition lifecycle, key mapping, focus changes and candidate UI.
- Installer, signing, uninstall/rollback and security regression.
- Real Windows 11 and non-ASCII user profile path verification.

**Warning:** wowtype_core.dll is not an installed TSF IME. Never package proprietary third-party model weights without permission; never log private user input by default.

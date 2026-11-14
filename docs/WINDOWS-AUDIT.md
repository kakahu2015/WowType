# WowType Windows Fork — Static Source Audit

**Snapshot:** 38eb269989a4c54c96c4b5749a13294750498b5e (2026-11-13 UTC; WowType 0.3.12).
**Scope:** repository file-tree inventory and focused static analysis of Windows compilation dependencies, core/session C ABI, test fixtures, platform adapters, voice/model integration and CI. This is not a line-by-line security audit or proof of native Windows build success.

## Source findings

| ID | Severity | Source location | Windows issue | Treatment |
| --- | --- | --- | --- | --- |
| WIN-001 | Blocker | core/src/llama.rs is_executable | Unconditional std::os::unix::fs::PermissionsExt | cfg-gated Windows/Unix implementation |
| WIN-002 | Blocker | core/src/llama.rs tests; core/src/translate.rs | Unix shebang, chmod, kill fixtures | Gate Unix-only fixtures; Windows-native process tests remain |
| WIN-003 | Major | core/src/session.rs system_time | Non-Unix systems returned None; date/time candidates absent | Win32 GetLocalTime |
| WIN-004 | Major | core/src/lexicon.rs cache_paths | Only XDG/HOME fallback | LOCALAPPDATA user cache |
| WIN-005 | Major | core/src/llama.rs executable discovery | Unix paths and executable bits | USERPROFILE, backslash, .exe, LOCALAPPDATA |
| WIN-006 | Major | scripts/abi-smoke.sh | POSIX shell and .so/.dylib loader | Native Windows runtime-loaded DLL smoke |
| WIN-007 | Major | .github/workflows/ci.yml | No Windows build job | Windows MSVC CI workflow |
| WIN-008 | Deferred | voice-daemon/src/other.rs | Non-Linux daemon currently errors | Windows daemon not included in phase 1 |
| WIN-009 | Deferred | voice/src/wtlocal/download.rs; pack.rs | XDG/HOME, curl and unzip | Native provisioning and licensing later |
| WIN-010 | Deferred | core/src/translate.rs; core/src/llama.rs | External curl and llama-server process | Runtime translation tests required |
| WIN-011 | High | core/include/wowtype_core.h; docs/ABI.md | Transitional session API; borrowed frame and string lifetimes | ABI major check, memory ownership contract |
| WIN-012 | Product blocker | fcitx5-addon/, macos/, settings/ | No Windows TSF COM front-end or installer | Phase 2 |

## Contract constraints

- core/Cargo.toml builds rlib, cdylib and staticlib; the Windows DLL alone is not an input method.
- The C ABI uses UTF-8; Windows TSF uses UTF-16. Test surrogate pairs and Chinese user directory paths before shipping.
- WowtypeFrame and nested strings are valid only until the next call on the same session. Check frame size before newer fields.
- Each session is single-threaded, and the engine must outlive sessions. TSF edit-session lifecycle must honor both constraints.
- Plain Pinyin must not depend on network access; input text and clipboard contents must not be logged or transmitted without explicit consent.
- Dictionary/model licenses are independent from application code. Do not redistribute Tencent model weights without verified rights.

## Independent acceptance evidence

1. Green Windows Core Validation workflow **at the exact fork commit**, full logs, and SHA-256 for the DLL.
2. Real Windows 11 x64 test, non-admin account, Chinese user profile path and non-English system locale.
3. True DLL ABI smoke (not mocked), plus deterministic candidate comparison against Linux/macOS.
4. For TSF: WinDbg/ETW traces, Notepad/Word/Chrome/WeChat/VS Code focus switching, composition/candidate/commit tests.
5. Prior to release: DLL loading hardening, installer/uninstaller registry cleanup, license and reproducible-build review.

**Status:** Changes prepared for Windows build validation. A successful CI run is not equivalent to a completed Windows TSF input method.

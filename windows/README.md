# WowType for Windows (preview)

Windows support currently targets Windows 10/11 x64 and uses the Windows Text Services Framework (TSF).

## Download and install

1. Open the [Windows build workflow](https://github.com/Gloridust/WowType/actions/workflows/windows.yml) and select the latest successful run.
2. Download the `wowtype-windows-x64` artifact and extract it.
3. Open **PowerShell as Administrator** in the extracted folder and run:

   ```powershell
   Set-ExecutionPolicy -Scope Process Bypass
   .\install.ps1
   ```

The installer registers WowType as a machine-wide TSF text service, so it needs an elevated shell; the binaries themselves still land in your user profile. A per-user registration is invisible to the Win+Space switcher, because that UI runs inside an AppContainer whose registry view excludes `HKCU\Software\Classes`. Press **Win+Space** to select WowType from the input switcher.

To remove it, switch to another input method, then run **Start > WowType > Uninstall WowType** from an elevated PowerShell. You can also run `uninstall.bat` as administrator from the extracted package. Your translation settings and API key are kept.

## Typing and translation

- Type pinyin and choose candidates with number keys, arrows, or the candidate window. When there is more than one page, click **上一页 / 下一页** or press **PgUp / PgDn**; `-` / `=` also page backward / forward.
- Press **Shift** briefly to cycle Chinese, English, and translation modes. Translation mode appears when an endpoint is configured and enabled.
- Open **WowType Settings** from the Start menu to enable translation and enter an OpenAI-compatible API URL, model, target language, and optional API key. The key is stored separately under `%APPDATA%\WowType` with a current-user-only file ACL.
- In translation mode, type the source text, wait for the automatic translation, then press **Enter** or **Space** to insert the translation. **Shift+Enter** inserts the source; **Esc** clears it.

Settings are read when the TSF service activates. Switch away from and back to WowType after changing them.

## Current scope

This preview includes the shared Rust pinyin engine, candidate selection, learned words, Chinese punctuation, and online OpenAI-compatible translation. It does not yet include voice input, the toolbar, the emoji panel, or the full Linux/macOS settings interface. Local-model translation is not exposed in the Windows settings UI.

Windows binaries are built on GitHub Actions; the repository does not require a local Windows build environment.

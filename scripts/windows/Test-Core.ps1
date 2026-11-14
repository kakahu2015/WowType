# Windows x64 native Rust core gate. Requires Rust MSVC and MSVC Developer PowerShell.
# IMPORTANT: This does NOT install a Windows TSF input method.
param([switch]$SkipWorkspaceCheck)
$ErrorActionPreference = 'Stop'
Set-Location (Resolve-Path (Join-Path $PSScriptRoot '../..'))

function Run-Checked([string]$Command, [string[]]$Arguments) {
    Write-Host ('> ' + $Command + ' ' + ($Arguments -join ' '))
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Command failed with exit code $LASTEXITCODE" }
}

if (-not (Get-Command cargo -ErrorAction SilentlyContinue)) { throw 'cargo missing; install Rust MSVC toolchain.' }
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'cl.exe missing; use x64 Visual Studio Developer PowerShell.' }
$hostLine = rustc -vV | Where-Object { $_ -like 'host:*' } | Select-Object -First 1
if ($hostLine -notmatch '^host:\s*(\S+)$') { throw 'Cannot determine Rust host triple.' }
if ($Matches[1] -ne 'x86_64-pc-windows-msvc') { throw "Need x64 Rust MSVC host, found $($Matches[1])" }

Run-Checked cargo @('test', '--locked', '--no-default-features', '-p', 'wowtype-core', '-p', 'wowtype-voice')
if (-not $SkipWorkspaceCheck) {
    Run-Checked cargo @('check', '--locked', '--workspace', '--all-features')
}
Run-Checked cargo @('build', '--locked', '--release', '--no-default-features', '-p', 'wowtype-core')
Run-Checked cl.exe @('/nologo', '/W4', '/utf-8', '/std:c11', '/Icore\include', 'windows\core\abi_smoke.c', '/Fe:target\release\wowtype-abi-smoke.exe')
Run-Checked '.\target\release\wowtype-abi-smoke.exe' @('.\target\release\wowtype_core.dll', '.\core\data\demo_lexicon.tsv')
Run-Checked cargo @('run', '--locked', '--no-default-features', '-p', 'wowtype-core', '--bin', 'wowtype-show', '--', '--lexicon', 'core/data/demo_lexicon.tsv', 'nihao', 'zhongguo')
Get-FileHash -Algorithm SHA256 '.\target\release\wowtype_core.dll' | Format-List
Write-Host 'PASS: native Windows core validation finished; TSF integration not tested.'

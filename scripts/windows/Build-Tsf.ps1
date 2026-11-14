# Compile the Windows 11 x64 TSF prototype in a Visual Studio Developer PowerShell.
# Produces a TEST-ONLY package, not a signed Windows installer.
$ErrorActionPreference = 'Stop'
Set-Location (Resolve-Path (Join-Path $PSScriptRoot '../..'))

function Checked([string]$Tool, [string[]]$Arguments) {
    Write-Host ('> ' + $Tool + ' ' + ($Arguments -join ' '))
    & $Tool @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Tool failed ($LASTEXITCODE)" }
}
if (!(Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw 'MSVC cl.exe unavailable; use Visual Studio Developer PowerShell (x64).'
}
if (!(Get-Command cargo -ErrorAction SilentlyContinue)) { throw 'Rust cargo unavailable.' }
$rustHost = rustc -vV | Select-String '^host:\s*(\S+)$'
if (!$rustHost -or $rustHost.Matches[0].Groups[1].Value -ne 'x86_64-pc-windows-msvc') {
    throw 'This prototype requires x86_64-pc-windows-msvc Rust host.'
}
Checked cargo @('build','--release','--locked','--no-default-features','-p','wowtype-core')
$destination = Join-Path (Get-Location) 'dist/windows'
New-Item -ItemType Directory -Force $destination | Out-Null
New-Item -ItemType Directory -Force (Join-Path $destination 'data') | Out-Null
$target = Join-Path $destination 'WowTypeTSF.dll'
Checked cl.exe @(
    '/nologo','/std:c++20','/EHsc','/W4','/utf-8','/MT','/LD',
    '/DUNICODE','/D_UNICODE',
    '/Icore\include','windows\tsf\text_service.cpp',
    '/link','/DEF:windows\tsf\WowTypeTSF.def',"/OUT:$target","/IMPLIB:$(Join-Path $destination 'WowTypeTSF.lib')",
    'ole32.lib','uuid.lib','advapi32.lib','user32.lib','gdi32.lib'
)
Copy-Item 'target/release/wowtype_core.dll' (Join-Path $destination 'wowtype_core.dll') -Force
Copy-Item 'core/data/demo_lexicon.tsv' (Join-Path $destination 'data/demo_lexicon.tsv') -Force
Copy-Item 'scripts/windows/Install-Tsf.ps1' (Join-Path $destination 'Install-Tsf.ps1') -Force
Copy-Item 'scripts/windows/Uninstall-Tsf.ps1' (Join-Path $destination 'Uninstall-Tsf.ps1') -Force
Copy-Item 'docs/WINDOWS-TSF.md' (Join-Path $destination 'WINDOWS-TSF.md') -Force
if (!(Test-Path $target)) { throw "Missing $target" }
Write-Host "Prototype bundle: $destination"
Get-FileHash -Algorithm SHA256 $target, (Join-Path $destination 'wowtype_core.dll')

# Per-user development installation. Only run on a Windows 11 x64 TEST MACHINE.
# Requires TSF DLL, Rust core DLL and data directory produced by Build-Tsf.ps1.
param([string]$BundleDir = '')
$ErrorActionPreference = 'Stop'
if (!$BundleDir) {
    if (Test-Path (Join-Path $PSScriptRoot 'WowTypeTSF.dll')) {
        # Runs directly from a downloaded GitHub Actions artifact.
        $BundleDir = $PSScriptRoot
    } else {
        # Runs from repository scripts/windows.
        $root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
        $BundleDir = Join-Path $root 'dist/windows'
    }
}
$BundleDir = (Resolve-Path $BundleDir).Path
$destination = Join-Path $env:LOCALAPPDATA 'Programs/WowTypeTSF'
$srcTip = Join-Path $BundleDir 'WowTypeTSF.dll'
$srcCore = Join-Path $BundleDir 'wowtype_core.dll'
$srcData = Join-Path $BundleDir 'data/demo_lexicon.tsv'
foreach ($file in @($srcTip,$srcCore,$srcData)) {
    if (!(Test-Path $file)) { throw "Missing package file: $file" }
}
if (Test-Path (Join-Path $destination 'WowTypeTSF.dll')) {
    throw "Already installed: $destination. Run Uninstall-Tsf.ps1 first; close apps using WowType."
}
New-Item -ItemType Directory -Force (Join-Path $destination 'data') | Out-Null
Copy-Item $srcTip (Join-Path $destination 'WowTypeTSF.dll') -Force
Copy-Item $srcCore (Join-Path $destination 'wowtype_core.dll') -Force
Copy-Item $srcData (Join-Path $destination 'data/demo_lexicon.tsv') -Force
$regsvr32 = Join-Path $env:WINDIR 'System32/regsvr32.exe'
& $regsvr32 /s (Join-Path $destination 'WowTypeTSF.dll')
if ($LASTEXITCODE -ne 0) { throw "TSF registration failed (exit $LASTEXITCODE). Uninstall to clean up." }
$clsid = '9aebaf46-3231-4f65-836d-0972c2396181'
$reg = "Registry::HKEY_CURRENT_USER\Software\Classes\CLSID\{$clsid}"
if (!(Test-Path $reg)) { throw "COM registration missing: $reg" }
Write-Host "Registered per-user prototype: $destination"
Write-Host 'Select WowType Chinese (prototype) in Windows Settings > Time & language > Language & region.'
Write-Warning 'This is not a production IME. Do not use for sensitive input; uninstall when testing completes.'

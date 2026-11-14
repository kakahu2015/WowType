param(
    [string]$PackageDirectory = $PSScriptRoot
)

$ErrorActionPreference = 'Stop'
$installDirectory = Join-Path $env:LOCALAPPDATA 'Programs\WowType'
$required = @('WowTypeTextService.dll', 'wowtype_core.dll', 'data\lexicon.tsv', 'data\english.txt')
foreach ($item in $required) {
    if (-not (Test-Path (Join-Path $PackageDirectory $item))) {
        throw "Package is missing $item"
    }
}

New-Item -ItemType Directory -Force -Path $installDirectory | Out-Null
Copy-Item (Join-Path $PackageDirectory 'WowTypeTextService.dll') $installDirectory -Force
Copy-Item (Join-Path $PackageDirectory 'wowtype_core.dll') $installDirectory -Force
Copy-Item (Join-Path $PackageDirectory 'data') (Join-Path $installDirectory 'data') -Recurse -Force
Copy-Item (Join-Path $PackageDirectory 'settings.ps1') $installDirectory -Force
Copy-Item (Join-Path $PackageDirectory 'uninstall.ps1') $installDirectory -Force

$dll = Join-Path $installDirectory 'WowTypeTextService.dll'
$regsvr = Join-Path $env:WINDIR 'System32\regsvr32.exe'
$registration = Start-Process -FilePath $regsvr -ArgumentList @('/s', $dll) -Wait -PassThru
if ($registration.ExitCode -ne 0) {
    throw "TSF registration failed with exit code $($registration.ExitCode)"
}

$menu = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\WowType'
New-Item -ItemType Directory -Force -Path $menu | Out-Null
$shortcutPath = Join-Path $menu 'WowType Settings.lnk'
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($shortcutPath)
$shortcut.TargetPath = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
$shortcut.Arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$installDirectory\settings.ps1`""
$shortcut.WorkingDirectory = $installDirectory
$shortcut.Description = 'Configure WowType input and AI translation'
$shortcut.Save()

Write-Host 'WowType was installed for this Windows user.'
Write-Host 'Select WowType from the Windows input switcher. Open WowType Settings to configure AI translation.'

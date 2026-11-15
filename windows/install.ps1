param(
    [string]$PackageDirectory = $PSScriptRoot
)

$ErrorActionPreference = 'Stop'
$packagePath = (Resolve-Path -LiteralPath $PackageDirectory).Path
$installDirectory = Join-Path $env:LOCALAPPDATA 'Programs\WowType'
$required = @('WowTypeTextService.dll', 'wowtype_core.dll', 'data\lexicon.tsv', 'data\english.txt')
foreach ($item in $required) {
    if (-not (Test-Path (Join-Path $packagePath $item))) {
        throw "Package is missing $item"
    }
}

$classId = '{3E76C197-7A5E-4A4A-9A31-537D743F2251}'
$machineKeyPath = "Software\Classes\CLSID\$classId\InprocServer32"
$machineKey = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($machineKeyPath)
$machineServer = if ($machineKey) { $machineKey.GetValue('') } else { $null }
if ($machineKey) { $machineKey.Close() }
$installedDll = Join-Path $installDirectory 'WowTypeTextService.dll'
$legacyMachineRegistration = $machineServer -and
    ([IO.Path]::GetFullPath($machineServer) -ieq [IO.Path]::GetFullPath($installedDll))
$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if ($legacyMachineRegistration -and
    -not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $arguments = '-NoProfile -ExecutionPolicy Bypass -File "{0}" -PackageDirectory "{1}"' -f $PSCommandPath, $packagePath
    $elevated = Start-Process -FilePath (Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe') `
        -ArgumentList $arguments -Verb RunAs -Wait -PassThru
    exit $elevated.ExitCode
}

$regsvr = Join-Path $env:WINDIR 'System32\regsvr32.exe'
if (Test-Path $installedDll) {
    $unregistration = Start-Process -FilePath $regsvr -ArgumentList @('/s', '/u', $installedDll) -Wait -PassThru
    if ($unregistration.ExitCode -ne 0) {
        throw "Existing WowType registration could not be removed (exit code $($unregistration.ExitCode))."
    }
}
if ($legacyMachineRegistration) {
    Remove-Item "Registry::HKEY_LOCAL_MACHINE\Software\Classes\CLSID\$classId" `
        -Recurse -Force -ErrorAction SilentlyContinue
}

New-Item -ItemType Directory -Force -Path $installDirectory | Out-Null
Copy-Item (Join-Path $packagePath 'WowTypeTextService.dll') $installDirectory -Force
Copy-Item (Join-Path $packagePath 'wowtype_core.dll') $installDirectory -Force
Copy-Item (Join-Path $packagePath 'data') (Join-Path $installDirectory 'data') -Recurse -Force
Copy-Item (Join-Path $packagePath 'settings.ps1') $installDirectory -Force
Copy-Item (Join-Path $packagePath 'uninstall.ps1') $installDirectory -Force

$dll = Join-Path $installDirectory 'WowTypeTextService.dll'
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

$uninstallShortcutPath = Join-Path $menu 'Uninstall WowType.lnk'
$uninstallShortcut = $shell.CreateShortcut($uninstallShortcutPath)
$uninstallShortcut.TargetPath = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
$uninstallShortcut.Arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$installDirectory\uninstall.ps1`""
$uninstallShortcut.WorkingDirectory = $installDirectory
$uninstallShortcut.Description = 'Remove WowType from this Windows user account'
$uninstallShortcut.Save()

Write-Host 'WowType was installed for this Windows user.'
Write-Host 'Select WowType from the Windows input switcher. Open WowType Settings to configure AI translation.'

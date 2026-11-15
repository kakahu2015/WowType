$ErrorActionPreference = 'Stop'
$installDirectory = Join-Path $env:LOCALAPPDATA 'Programs\WowType'
$dll = Join-Path $installDirectory 'WowTypeTextService.dll'
$regsvr = Join-Path $env:WINDIR 'System32\regsvr32.exe'
$classId = '{3E76C197-7A5E-4A4A-9A31-537D743F2251}'
$machineKeyPath = "Software\Classes\CLSID\$classId\InprocServer32"
$machineKey = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($machineKeyPath)
$machineServer = if ($machineKey) { $machineKey.GetValue('') } else { $null }
if ($machineKey) { $machineKey.Close() }
$legacyMachineRegistration = $machineServer -and
    ([IO.Path]::GetFullPath($machineServer) -ieq [IO.Path]::GetFullPath($dll))
$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if ($legacyMachineRegistration -and
    -not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $arguments = '-NoProfile -ExecutionPolicy Bypass -File "{0}"' -f $PSCommandPath
    $elevated = Start-Process -FilePath (Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe') `
        -ArgumentList $arguments -Verb RunAs -Wait -PassThru
    exit $elevated.ExitCode
}

if (Test-Path $dll) {
    $result = Start-Process -FilePath $regsvr -ArgumentList @('/s', '/u', $dll) -Wait -PassThru
    if ($result.ExitCode -ne 0) {
        throw "TSF unregistration failed with exit code $($result.ExitCode)"
    }
}
if ($legacyMachineRegistration) {
    Remove-Item "Registry::HKEY_LOCAL_MACHINE\Software\Classes\CLSID\$classId" `
        -Recurse -Force -ErrorAction SilentlyContinue
}

Remove-Item (Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\WowType') `
    -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item $installDirectory -Recurse -Force -ErrorAction SilentlyContinue
Write-Host 'WowType input method was removed. User settings and translation key were kept.'

$ErrorActionPreference = 'Stop'
$installDirectory = Join-Path $env:LOCALAPPDATA 'Programs\WowType'
$dll = Join-Path $installDirectory 'WowTypeTextService.dll'
$regsvr = Join-Path $env:WINDIR 'System32\regsvr32.exe'

if (Test-Path $dll) {
    $result = Start-Process -FilePath $regsvr -ArgumentList @('/s', '/u', $dll) -Wait -PassThru
    if ($result.ExitCode -ne 0) {
        throw "TSF unregistration failed with exit code $($result.ExitCode)"
    }
}

Remove-Item (Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\WowType') `
    -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item $installDirectory -Recurse -Force -ErrorAction SilentlyContinue
Write-Host 'WowType input method was removed. User settings and translation key were kept.'

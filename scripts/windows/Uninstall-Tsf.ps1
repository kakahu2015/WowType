# Per-user rollback of the experimental WowType TSF profile.
$ErrorActionPreference = 'Stop'
$destination = Join-Path $env:LOCALAPPDATA 'Programs/WowTypeTSF'
$dll = Join-Path $destination 'WowTypeTSF.dll'
# Prevent a scheduled update from recreating files while uninstall is removing them.
$task = Get-ScheduledTask -TaskName 'WowTypeTSF-LexiconUpdate' -ErrorAction SilentlyContinue
if ($task) {
    Unregister-ScheduledTask -TaskName 'WowTypeTSF-LexiconUpdate' -Confirm:$false -ErrorAction Stop
    Write-Host 'Removed WowType per-user weekly dictionary update task.'
}
if (Test-Path $dll) {
    $regsvr32 = Join-Path $env:WINDIR 'System32/regsvr32.exe'
    $unregistration = Start-Process -FilePath $regsvr32 -ArgumentList @('/u', '/s', ('"' + $dll + '"')) -Wait -PassThru
    if ($unregistration.ExitCode -ne 0) { throw "Unregistration failed (exit $($unregistration.ExitCode)), files retained." }
}
$clsid = '9aebaf46-3231-4f65-836d-0972c2396181'
$reg = "Registry::HKEY_CURRENT_USER\Software\Classes\CLSID\{$clsid}"
if (Test-Path $reg) { throw "COM CLSID registration still present: $reg" }
if (Test-Path $destination) {
    try { Remove-Item $destination -Force -Recurse }
    catch { Write-Warning "Could not remove DLLs (still loaded?). Restart after switching to Microsoft Pinyin, then remove $destination." }
}
Write-Host 'Unregistration completed. Restart affected applications to unload the old DLL.'

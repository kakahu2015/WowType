# Manage per-user weekly offline dictionary update checks for WowType Windows.
# PowerShell 5.1 compatible. No administrator permission required.
param(
    [switch]$Disable,
    [switch]$Status,
    [string]$InstallDir = ''
)
$ErrorActionPreference = 'Stop'
if (!$InstallDir) { $InstallDir = Join-Path $env:LOCALAPPDATA 'Programs\WowTypeTSF' }
$taskName = 'WowTypeTSF-LexiconUpdate'
$existing = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
if ($Status) {
    if ($existing) {
        $info = Get-ScheduledTaskInfo -TaskName $taskName
        Write-Host "Lexicon automatic update enabled. Next run: $($info.NextRunTime)"
    } else {
        Write-Host 'Lexicon automatic update disabled.'
    }
    return
}
if ($Disable) {
    if ($existing) {
        Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction Stop
    }
    Write-Host 'Lexicon automatic update disabled and scheduled task removed.'
    return
}
$updater = Join-Path $InstallDir 'Update-Lexicon.ps1'
if (!(Test-Path -LiteralPath $updater -PathType Leaf)) {
    throw "Updater script not installed: $updater"
}
$exe = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
$argument = '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' + $updater + '"'
$action = New-ScheduledTaskAction -Execute $exe -Argument $argument
$trigger = New-ScheduledTaskTrigger -Weekly -DaysOfWeek Sunday -At ([DateTime]::Today.AddHours(3))
$settings = New-ScheduledTaskSettingsSet -StartWhenAvailable -ExecutionTimeLimit (New-TimeSpan -Minutes 15) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
# The logged-on current user, never SYSTEM and never elevated.
$user = [Security.Principal.WindowsIdentity]::GetCurrent().Name
$principal = New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive -RunLevel Limited
Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Settings $settings -Principal $principal -Force -ErrorAction Stop | Out-Null
$task = Get-ScheduledTask -TaskName $taskName -ErrorAction Stop
if (!$task) { throw 'Task Scheduler did not retain the per-user update task' }
Write-Host "Weekly WowType offline lexicon update check scheduled for Sunday at 03:00 ($user)."
Write-Host "Manual check: & '$updater' -CheckOnly"

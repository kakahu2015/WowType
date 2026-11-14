# WowType Windows: check upstream release and atomically switch offline lexicon data.
# This process sends NO keystrokes, documents, history or user lexicon to the network.
# Requires the native full_lexicon_smoke.exe shipped with the Windows test bundle.
# PowerShell 5.1 and PowerShell 7 compatible; no elevation required.
param(
    [switch]$CheckOnly,
    [switch]$Force,
    [string]$InstallDir = '',
    [string]$MetadataPath = '',
    [string]$ArchivePath = ''
)
$ErrorActionPreference = 'Stop'
if (!$InstallDir) { $InstallDir = Join-Path $env:LOCALAPPDATA 'Programs\WowTypeTSF' }
$DataDir = Join-Path $InstallDir 'data'
$VersionsDir = Join-Path $DataDir 'versions'
$Pointer = Join-Path $DataDir 'active.txt'
$LogDir = Join-Path $InstallDir 'logs'
$TaskName = 'WowTypeTSF-LexiconUpdate'
$Api = 'https://api.github.com/repos/Gloridust/WowType/releases/latest'

function Write-Status([string]$Message) {
    Write-Host $Message
    try {
        New-Item -ItemType Directory -Path $LogDir -Force | Out-Null
        Add-Content -LiteralPath (Join-Path $LogDir 'lexicon-update.log') -Value (
            (Get-Date -Format 'yyyy-MM-ddTHH:mm:ss') + ' ' + $Message
        ) -Encoding UTF8
    } catch {
        # Telemetry is optional; a log failure must not prevent safe updating.
    }
}

function Parse-Version([string]$Text) {
    $value = $Text.Trim()
    if ($value -notmatch '^\d{4}\.\d{1,2}\.\d{1,2}\.\d{1,6}$') {
        throw "Invalid dictionary version format: $value"
    }
    return [Version]$value
}

function Validate-Data([string]$Directory) {
    foreach ($name in @('lexicon.tsv','words.lm','VERSION','LICENSE','SOURCE.txt')) {
        if (!(Test-Path -LiteralPath (Join-Path $Directory $name) -PathType Leaf)) {
            throw "Dictionary missing $name in $Directory"
        }
    }
    $r = [IO.StreamReader]::new((Join-Path $Directory 'lexicon.tsv'), [Text.Encoding]::UTF8, $true)
    $count = 0
    try {
        while ($null -ne ($line = $r.ReadLine())) {
            if ($line.Length -gt 0 -and !$line.StartsWith('#')) { $count++ }
        }
    } finally { $r.Dispose() }
    if ($count -lt 500000) { throw "Truncated dictionary: $count rows" }
    if ((Get-Item -LiteralPath (Join-Path $Directory 'words.lm')).Length -lt 1024) {
        throw 'Empty word language model'
    }
    return $count
}

if (!(Test-Path -LiteralPath (Join-Path $InstallDir 'wowtype_core.dll'))) {
    throw "Not installed: $InstallDir"
}
if (!(Test-Path -LiteralPath (Join-Path $InstallDir 'full_lexicon_smoke.exe'))) {
    throw 'Missing native lexicon/LM validation tool; reinstall the current test package.'
}
if (!(Test-Path -LiteralPath (Join-Path $DataDir 'lexicon.tsv'))) {
    throw "Original offline dictionary missing: $DataDir"
}

$mutexName = 'Local\WowTypeTSF-LexiconUpdate'
$mutex = [System.Threading.Mutex]::new($false, $mutexName)
$locked = $false
$temp = ''
try {
    $locked = $mutex.WaitOne(0)
    if (!$locked) {
        Write-Status 'Update already running; skipped.'
        return
    }

    if ($MetadataPath) {
        # Deterministic integration tests can supply a local release manifest.
        $release = Get-Content -LiteralPath $MetadataPath -Raw -Encoding UTF8 | ConvertFrom-Json
    } else {
        $headers = @{ 'Accept' = 'application/vnd.github+json'; 'User-Agent' = 'WowType-Windows-LexiconUpdater' }
        $release = Invoke-RestMethod -Uri $Api -Headers $headers -TimeoutSec 30
    }
    if (!$release -or $release.draft -eq $true -or $release.prerelease -eq $true) {
        throw 'No trusted stable release metadata returned by GitHub'
    }
    if ([string]$release.tag_name -notmatch '^v\d+\.\d+\.\d+$') {
        throw 'Unexpected upstream release tag'
    }
    $assets = @($release.assets | Where-Object {
        $_.name -match '^wowtype-data-rime-ice-\d{4}\.\d{1,2}\.\d{1,2}\.\d+\.tar\.gz$'
    })
    if ($assets.Count -ne 1) {
        Write-Status 'No unique official rime-ice data asset in latest release; not changing installed dictionary.'
        return
    }
    $asset = $assets[0]
    $versionName = [regex]::Match([string]$asset.name,
        '^wowtype-data-rime-ice-(\d{4}\.\d{1,2}\.\d{1,2}\.\d+)\.tar\.gz$').Groups[1].Value
    $upstream = Parse-Version $versionName
    $expected = [string]$asset.digest
    if ($expected -notmatch '^sha256:[a-fA-F0-9]{64}$') {
        throw 'Official release is missing a valid SHA-256 asset digest; refusing unsafe update'
    }
    $expected = $expected.Substring(7).ToLowerInvariant()
    if ([long]$asset.size -lt 1048576 -or [long]$asset.size -gt 209715200) {
        throw 'Upstream release archive size is outside accepted limits'
    }
    $assetUrl = "https://github.com/Gloridust/WowType/releases/download/$($release.tag_name)/$($asset.name)"
    if ([string]$asset.browser_download_url -ne $assetUrl) {
        throw 'Upstream download URL did not match the expected project and release'
    }

    $activeName = ''
    if (Test-Path -LiteralPath $Pointer) {
        $activeName = (Get-Content -LiteralPath $Pointer -Raw -Encoding ASCII).Trim()
    } else {
        $activeName = (Get-Content -LiteralPath (Join-Path $DataDir 'VERSION') -Raw -Encoding UTF8).Trim()
    }
    $current = Parse-Version $activeName
    if (!$Force -and $upstream -le $current) {
        Write-Status "Offline dictionary up to date: $activeName ($($release.tag_name))."
        return
    }
    Write-Status "Upstream dictionary $versionName available; installed $activeName"
    if ($CheckOnly) { return }

    $temp = Join-Path ([IO.Path]::GetTempPath()) ('WowTypeUpdate-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $temp -Force | Out-Null
    if ($ArchivePath) {
        $archive = $ArchivePath
    } else {
        $archive = Join-Path $temp 'rime-ice.tar.gz'
        Invoke-WebRequest -Uri $assetUrl -OutFile $archive -MaximumRedirection 10 -ErrorAction Stop
    }
    if (!(Test-Path -LiteralPath $archive -PathType Leaf)) { throw 'Upstream archive missing' }
    $actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $expected) { throw "Dictionary SHA-256 mismatch: $actual" }

    # Reject traversal, absolute paths and Windows alternate stream paths before tar extraction.
    $entries = @(& tar.exe -tzf $archive)
    if ($LASTEXITCODE -ne 0 -or !$entries.Count) { throw 'Cannot list upstream dictionary tar archive' }
    foreach ($entry in $entries) {
        $p = ([string]$entry).Replace('\','/')
        if ($p.StartsWith('/') -or $p -match '^[a-zA-Z]:' -or
            $p -match '(^|/)\.\.(/|$)' -or $p -match ':') {
            throw "Unsafe path inside upstream archive: $entry"
        }
    }
    $extract = Join-Path $temp 'extract'
    New-Item -ItemType Directory -Path $extract -Force | Out-Null
    & tar.exe -xzf $archive -C $extract
    if ($LASTEXITCODE -ne 0) { throw 'Cannot extract verified upstream archive' }
    $lexicons = @(Get-ChildItem -LiteralPath $extract -Recurse -File -Filter 'lexicon.tsv')
    if ($lexicons.Count -ne 1) { throw 'Expected exactly one upstream lexicon.tsv' }
    $sourceDir = $lexicons[0].DirectoryName
    # Official tarballs need not contain VERSION: the authenticated release asset
    # name is canonical after checking the GitHub-published SHA-256 digest.
    $archiveVersion = Join-Path $sourceDir 'VERSION'
    if (Test-Path -LiteralPath $archiveVersion -PathType Leaf) {
        $seenVersion = (Get-Content -LiteralPath $archiveVersion -Raw).Trim()
        if ($seenVersion -ne $versionName) { throw 'Archive VERSION disagrees with release asset name' }
    } else {
        [IO.File]::WriteAllText($archiveVersion, $versionName, (New-Object System.Text.ASCIIEncoding))
    }
    $count = Validate-Data $sourceDir

    # Rust core checks whether lexicon AND words.lm can load and perform Unicode selection.
    $validator = Join-Path $InstallDir 'full_lexicon_smoke.exe'
    & $validator (Join-Path $InstallDir 'wowtype_core.dll') (
        Join-Path $sourceDir 'lexicon.tsv') (Join-Path $sourceDir 'words.lm')
    if ($LASTEXITCODE -ne 0) { throw 'Rust core rejected the updated dictionary or language model' }

    New-Item -ItemType Directory -Force -Path $VersionsDir | Out-Null
    $destination = Join-Path $VersionsDir $versionName
    if (!(Test-Path -LiteralPath $destination)) {
        $stage = Join-Path $VersionsDir ('.staging-' + [guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Force -Path $stage | Out-Null
        try {
            foreach ($name in @('lexicon.tsv','words.lm','VERSION','LICENSE','SOURCE.txt')) {
                Copy-Item -LiteralPath (Join-Path $sourceDir $name) -Destination $stage -Force
            }
            foreach ($optional in @('english.txt')) {
                if (Test-Path -LiteralPath (Join-Path $sourceDir $optional)) {
                    Copy-Item -LiteralPath (Join-Path $sourceDir $optional) -Destination $stage -Force
                }
            }
            Move-Item -LiteralPath $stage -Destination $destination
        } finally {
            if (Test-Path -LiteralPath $stage) {
                Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
            }
        }
    } else {
        $existing = Validate-Data $destination
        if ((Get-Content -LiteralPath (Join-Path $destination 'VERSION') -Raw).Trim() -ne $versionName) {
            throw "Existing immutable version directory has inconsistent VERSION: $destination"
        }
    }

    # Crash-safe tiny pointer swap; old lexicon is retained for rollback/active sessions.
    # TSF detects the changed data on its next safe activation / non-composing focus.
    $pointerTemp = Join-Path $DataDir ('active-' + [guid]::NewGuid().ToString('N') + '.tmp')
    try {
        [IO.File]::WriteAllText($pointerTemp, $versionName,
            (New-Object System.Text.ASCIIEncoding))
        if (Test-Path -LiteralPath $Pointer) {
            [IO.File]::Replace($pointerTemp, $Pointer, (Join-Path $DataDir 'active.previous.txt'))
        } else {
            [IO.File]::Move($pointerTemp, $Pointer)
        }
    } finally {
        if (Test-Path -LiteralPath $pointerTemp) {
            Remove-Item -LiteralPath $pointerTemp -Force -ErrorAction SilentlyContinue
        }
    }
    Write-Status "Updated offline dictionary to $versionName ($count rows), validated Rust core; takes effect after TSF reactivation."
} catch {
    Write-Status ('UPDATE FAILED; previous offline data preserved: ' + $_.Exception.Message)
    throw
} finally {
    if ($temp -and (Test-Path -LiteralPath $temp)) {
        Remove-Item -LiteralPath $temp -Force -Recurse -ErrorAction SilentlyContinue
    }
    if ($locked) { $mutex.ReleaseMutex() | Out-Null }
    $mutex.Dispose()
}

# Pin and vendor the upstream WowType offline dictionary in the Windows CI artifact.
# The Windows IME never downloads input data at runtime.
# Source: Gloridust/WowType v0.3.12 -> rime-ice 2026.11.11.3
# Lexicon: GPL-3.0; words.lm: CC BY-SA 4.0 (see SOURCE.txt).
param(
    [string]$OutputDir = '',
    [string]$ArchivePath = ''
)
$ErrorActionPreference = 'Stop'
$Version = 'v0.3.12'
$Asset = 'wowtype-data-rime-ice-2026.11.11.3.tar.gz'
$Url = "https://github.com/Gloridust/WowType/releases/download/$Version/$Asset"
$ExpectedSha256 = 'ea07c3e8cf66cbf7d0ebc23ab95cc96384e6f3808fd0cdee1f87905322e51d7c'

$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if (!$OutputDir) { $OutputDir = Join-Path $root 'dist/windows/data' }
if (!$ArchivePath -and $env:WOWTYPE_DATA_ARCHIVE) { $ArchivePath = $env:WOWTYPE_DATA_ARCHIVE }

$temp = Join-Path ([IO.Path]::GetTempPath()) ('wowtype-dict-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temp -Force | Out-Null
try {
    if (!$ArchivePath) {
        $ArchivePath = Join-Path $temp $Asset
        Write-Host "Downloading pinned upstream dictionary: $Url"
        Invoke-WebRequest -Uri $Url -OutFile $ArchivePath -MaximumRedirection 10 -ErrorAction Stop
    }
    if (!(Test-Path -LiteralPath $ArchivePath -PathType Leaf)) {
        throw "Offline dictionary archive missing: $ArchivePath"
    }
    $actual = (Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $ExpectedSha256) {
        throw "Upstream dictionary SHA-256 mismatch: expected $ExpectedSha256, got $actual"
    }
    Write-Host "Pinned archive SHA-256 verified: $actual"
    $unpack = Join-Path $temp 'unpacked'
    New-Item -ItemType Directory -Force -Path $unpack | Out-Null
    & tar.exe -xzf $ArchivePath -C $unpack
    if ($LASTEXITCODE -ne 0) { throw 'tar.exe failed to extract verified dictionary archive' }

    $lexicons = @(Get-ChildItem -LiteralPath $unpack -Recurse -File -Filter 'lexicon.tsv')
    if ($lexicons.Count -ne 1) {
        throw "Expected exactly one lexicon.tsv in upstream archive, found $($lexicons.Count)"
    }
    $source = $lexicons[0].DirectoryName
    $required = @('lexicon.tsv', 'words.lm', 'LICENSE', 'SOURCE.txt')
    foreach ($name in $required) {
        if (!(Test-Path -LiteralPath (Join-Path $source $name) -PathType Leaf)) {
            throw "Verified upstream archive lacks required $name beside lexicon.tsv"
        }
    }

    # Count actual data records using streaming I/O (do NOT load ~900k records into PowerShell).
    $lexiconPath = Join-Path $source 'lexicon.tsv'
    $reader = [IO.StreamReader]::new($lexiconPath, [Text.Encoding]::UTF8, $true)
    $entries = 0
    $nihao = $false
    $zhongguo = $false
    $nKey = 'ni hao' + [char]9 + '你好' + [char]9
    $zKey = 'zhong guo' + [char]9 + '中国' + [char]9
    try {
        while ($null -ne ($line = $reader.ReadLine())) {
            if (!$line -or $line.StartsWith('#')) { continue }
            $entries++
            if ($line.StartsWith($nKey)) { $nihao = $true }
            if ($line.StartsWith($zKey)) { $zhongguo = $true }
        }
    } finally { $reader.Dispose() }
    if ($entries -lt 500000) { throw "Real lexicon is too small: $entries records (minimum 500000)" }
    if (!$nihao -or !$zhongguo) {
        throw 'Real lexicon missing required ni hao / zhong guo candidate(s)'
    }
    if ((Get-Item -LiteralPath (Join-Path $source 'words.lm')).Length -lt 1024) {
        throw 'Language model words.lm is empty or truncated'
    }

    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
    foreach ($name in $required) {
        Copy-Item -LiteralPath (Join-Path $source $name) -Destination (Join-Path $OutputDir $name) -Force
    }
    foreach ($optional in @('VERSION', 'english.txt')) {
        if (Test-Path -LiteralPath (Join-Path $source $optional) -PathType Leaf) {
            Copy-Item -LiteralPath (Join-Path $source $optional) -Destination (Join-Path $OutputDir $optional) -Force
        }
    }
    Write-Host "WowType Windows OFFLINE lexicon ready: $entries entries; model and GPL/CC BY-SA provenance included"
    Write-Host "Dictionary source: $Url"
    Get-FileHash -LiteralPath (Join-Path $OutputDir 'lexicon.tsv') -Algorithm SHA256
} finally {
    Remove-Item -LiteralPath $temp -Force -Recurse -ErrorAction SilentlyContinue
}

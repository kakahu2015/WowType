param(
    [Parameter(Mandatory = $true)][string]$DataDirectory,
    [string]$OutputDirectory = 'dist/windows'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$data = [IO.Path]::GetFullPath($DataDirectory)
$output = [IO.Path]::GetFullPath((Join-Path $root $OutputDirectory))
$target = 'x86_64-pc-windows-msvc'
$buildDirectory = Join-Path $root 'build/windows'
$packageDirectory = Join-Path $output 'WowType-windows-x64'

foreach ($file in @('lexicon.tsv', 'english.txt', 'words.lm')) {
    if (-not (Test-Path (Join-Path $data $file))) { throw "Data directory is missing $file" }
}

Push-Location $root
try {
    & cargo build --release --locked -p wowtype-core --no-default-features --target $target
    if ($LASTEXITCODE -ne 0) { throw 'Rust Windows build failed.' }

    $release = Join-Path $root "target/$target/release"
    $coreDll = Join-Path $release 'wowtype_core.dll'
    $coreImport = Join-Path $release 'wowtype_core.dll.lib'
    if (-not (Test-Path $coreDll)) { throw "Rust core DLL not found: $coreDll" }
    if (-not (Test-Path $coreImport)) { throw "Rust core import library not found: $coreImport" }

    & cmake -S windows -B $buildDirectory -A x64 `
        "-DWOWTYPE_CORE_INCLUDE=$(Join-Path $root 'core/include')" `
        "-DWOWTYPE_CORE_IMPORT_LIB=$coreImport"
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & cmake --build $buildDirectory --config Release --parallel
    if ($LASTEXITCODE -ne 0) { throw 'Windows TSF build failed.' }

    New-Item -ItemType Directory -Force -Path $packageDirectory | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $packageDirectory 'data') | Out-Null
    Copy-Item (Join-Path $buildDirectory 'Release/WowTypeTextService.dll') $packageDirectory -Force
    Copy-Item $coreDll $packageDirectory -Force
    Copy-Item (Join-Path $data '*') (Join-Path $packageDirectory 'data') -Force
    Copy-Item (Join-Path $root 'core/data/s2t.tsv') (Join-Path $packageDirectory 'data') -Force
    Copy-Item (Join-Path $PSScriptRoot 'install.ps1') $packageDirectory -Force
    Copy-Item (Join-Path $PSScriptRoot 'uninstall.ps1') $packageDirectory -Force
    Copy-Item (Join-Path $PSScriptRoot 'settings.ps1') $packageDirectory -Force
    Copy-Item (Join-Path $PSScriptRoot 'README.md') $packageDirectory -Force

    $version = (Select-String -Path (Join-Path $root 'core/Cargo.toml') -Pattern '^version\s*=\s*"([^"]+)"' |
        Select-Object -First 1).Matches.Groups[1].Value
    if (-not $version) { throw 'Could not read WowType version.' }
    New-Item -ItemType Directory -Force -Path $output | Out-Null
    $archive = Join-Path $output "WowType-$version-windows-x64.zip"
    Remove-Item $archive -Force -ErrorAction SilentlyContinue
    Compress-Archive -Path (Join-Path $packageDirectory '*') -DestinationPath $archive -CompressionLevel Optimal
    Write-Output "Windows package: $archive"
} finally {
    Pop-Location
}

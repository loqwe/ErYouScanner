[CmdletBinding()]
param(
    [string]$DependencyRoot = 'D:/mhy-build-deps',
    [string]$QtRoot,
    [string]$FfmpegRoot,
    [string]$JsonSourceRoot,
    [string]$CMakePath,
    [string]$WorkspaceRoot,
    [int]$Parallel = 4,
    [switch]$ReplacePackage
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $WorkspaceRoot) { $WorkspaceRoot = Split-Path $sourceRoot -Parent }
$WorkspaceRoot = [IO.Path]::GetFullPath($WorkspaceRoot)
$cmakeText = Get-Content -LiteralPath (Join-Path $sourceRoot 'CMakeLists.txt') -Raw -Encoding UTF8
$versionMatch = [regex]::Match($cmakeText, '(?s)project\(ErYouScanner\s+VERSION\s+([0-9.]+)')
$displayMatch = [regex]::Match($cmakeText, 'set\(ERYOU_DISPLAY_NAME "([^"]+)"\)')
if (-not $versionMatch.Success -or -not $displayMatch.Success) {
    throw 'Cannot read the application version/display name from CMakeLists.txt.'
}
$version = $versionMatch.Groups[1].Value
$productName = $displayMatch.Groups[1].Value
$artifactName = "${productName}_v${version}_win64"
$buildRoot = Join-Path $WorkspaceRoot "build/v$version"
$releaseRoot = Join-Path $WorkspaceRoot "releases/v$version"
$buildId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ') + '-' + [Guid]::NewGuid().ToString('N')
$symbolVersionRoot = Join-Path $WorkspaceRoot "symbols/v$version"
$symbolRoot = Join-Path $symbolVersionRoot $buildId
$zipPath = Join-Path $releaseRoot "$artifactName.zip"
if ((Test-Path -LiteralPath $zipPath) -and -not $ReplacePackage) {
    throw "Package already exists: $zipPath. Use -ReplacePackage only when replacing this build intentionally."
}

if (-not $CMakePath) {
    $bundled = Join-Path $DependencyRoot 'vcpkg/downloads/tools/cmake-3.31.10-windows/cmake-3.31.10-windows-x86_64/bin/cmake.exe'
    if (Test-Path -LiteralPath $bundled) { $CMakePath = $bundled }
    else { $CMakePath = (Get-Command cmake -ErrorAction Stop).Source }
}
if (-not $QtRoot) { $QtRoot = Join-Path $DependencyRoot 'qt/6.8.0/msvc2022_64' }
if (-not $FfmpegRoot) {
    $sdkCandidates = @(
        (Join-Path $sourceRoot '.deps/ffmpeg'),
        (Join-Path $DependencyRoot 'ffmpeg'),
        (Join-Path $sourceRoot 'build/app-release/ffmpeg-master-latest-win64-gpl-shared'),
        (Join-Path $WorkspaceRoot 'archive/development/build/app-release/ffmpeg-master-latest-win64-gpl-shared')
    )
    foreach ($candidate in $sdkCandidates) {
        if (Test-Path -LiteralPath (Join-Path $candidate 'include/libavcodec/avcodec.h')) {
            $FfmpegRoot = $candidate
            break
        }
    }
    if (-not $FfmpegRoot) {
        throw 'Supply -FfmpegRoot with an existing FFmpeg shared SDK (bin/lib/include). The script does not silently select a newer SDK.'
    }
}
if (-not $JsonSourceRoot) { $JsonSourceRoot = Join-Path $sourceRoot '.deps/nlohmann_json' }
$useJsonSource = Test-Path -LiteralPath (Join-Path $JsonSourceRoot 'CMakeLists.txt')
if (-not $useJsonSource -and $PSBoundParameters.ContainsKey('JsonSourceRoot')) {
    throw 'JsonSourceRoot must point to a complete nlohmann_json source directory.'
}
$toolchain = Join-Path $DependencyRoot 'vcpkg/scripts/buildsystems/vcpkg.cmake'
$installedRoot = Join-Path $DependencyRoot 'installed'
foreach ($required in @($CMakePath, $toolchain, $installedRoot,
        (Join-Path $QtRoot 'lib/cmake/Qt6/Qt6Config.cmake'),
        (Join-Path $FfmpegRoot 'include/libavcodec/avcodec.h'))) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Missing build prerequisite: $required" }
}
if ($Parallel -lt 1) { throw 'Parallel must be at least 1.' }

function Invoke-CMake {
    param([string[]]$Arguments)
    & $CMakePath @Arguments
    if ($LASTEXITCODE -ne 0) { throw "CMake failed with exit code $LASTEXITCODE." }
}

$configure = @('--fresh', '-S', $sourceRoot, '-B', $buildRoot,
    '-G', 'Visual Studio 17 2022', '-A', 'x64',
    '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
    '-DVCPKG_TARGET_TRIPLET=x64-windows-static-md',
    "-DVCPKG_INSTALLED_DIR=$installedRoot", '-DVCPKG_MANIFEST_INSTALL=OFF',
    "-DCMAKE_PREFIX_PATH=$QtRoot", "-DQT6_INSTALL_PREFIX=$QtRoot",
    "-DFFMPEG_DIR=$FfmpegRoot", '-DUNIT_TESTS=OFF', '-DDEV=OFF')
if ($useJsonSource) {
    $configure += "-DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=$([IO.Path]::GetFullPath($JsonSourceRoot))"
}
Invoke-CMake -Arguments $configure
Invoke-CMake -Arguments @('--build', $buildRoot, '--config', 'Release', '--parallel', "$Parallel")

# A new staging directory prevents files from an earlier package entering this ZIP.
$stagingRoot = Join-Path $WorkspaceRoot "build/staging-v$version-$buildId"
$runtimeRoot = Join-Path $stagingRoot $artifactName
$stagedSymbols = Join-Path $stagingRoot 'symbols'
Invoke-CMake -Arguments @('--install', $buildRoot, '--config', 'Release', '--component', 'Runtime', '--prefix', $runtimeRoot)
Invoke-CMake -Arguments @('--install', $buildRoot, '--config', 'Release', '--component', 'Symbols', '--prefix', $stagedSymbols)

$files = @(Get-ChildItem -LiteralPath $runtimeRoot -Recurse -File)
$forbidden = @($files | Where-Object {
    $_.Extension -in @('.pdb', '.dmp', '.log') -or
    $_.FullName -match '[\\/]Config[\\/]' -or
    $_.Name -in @('userinfo.json', 'accounts.dpapi')
})
if ($forbidden.Count) { throw 'Private configuration, logs, dumps or symbols were found in the runtime staging directory.' }
$executable = Join-Path $runtimeRoot "$productName.exe"
if (-not (Test-Path -LiteralPath $executable)) { throw 'The application executable is missing.' }
if ((Get-Item -LiteralPath $executable).VersionInfo.ProductVersion -ne $version) {
    throw 'The executable version does not match CMakeLists.txt.'
}
$symbolFiles = @(Get-ChildItem -LiteralPath $stagedSymbols -File -Filter '*.pdb')
if ($symbolFiles.Count -ne 1 -or $symbolFiles[0].Length -eq 0) {
    throw 'The matching application PDB is missing or empty.'
}
$executableHash = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash.ToLowerInvariant()

$revision = & git -C $sourceRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot read the source revision.' }
$changes = @(& git -C $sourceRoot status --porcelain)
if ($LASTEXITCODE -ne 0) { throw 'Cannot read the source status.' }
$manifest = [ordered]@{
    product = 'ErYouScanner'
    version = $version
    symbolBuildId = $buildId
    executableSha256 = $executableHash
    sourceRevision = "$revision".Trim()
    workingTreeModified = ($changes.Count -gt 0)
    builtAtUtc = [DateTime]::UtcNow.ToString('o')
    qtVersion = (Get-Item -LiteralPath (Join-Path $QtRoot 'bin/Qt6Core.dll')).VersionInfo.ProductVersion
    symbols = @($symbolFiles | ForEach-Object {
        [ordered]@{
            file = $_.Name
            sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    })
    runtime = @($files | ForEach-Object {
        [ordered]@{
            file = $_.FullName.Substring($runtimeRoot.Length + 1).Replace('\', '/')
            sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    })
}
$utf8NoBom = New-Object Text.UTF8Encoding($false)
[IO.File]::WriteAllText((Join-Path $runtimeRoot 'build-info.json'), ($manifest | ConvertTo-Json -Depth 5), $utf8NoBom)
Copy-Item -LiteralPath (Join-Path $runtimeRoot 'build-info.json') -Destination $stagedSymbols
New-Item -ItemType Directory -Path $releaseRoot -Force | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
$temporaryZip = Join-Path $stagingRoot "$artifactName.zip"
[IO.Compression.ZipFile]::CreateFromDirectory($stagingRoot + '/' + $artifactName,
    $temporaryZip, [IO.Compression.CompressionLevel]::Optimal, $true)
$zipHash = (Get-FileHash -LiteralPath $temporaryZip -Algorithm SHA256).Hash.ToLowerInvariant()
$releaseInfo = [ordered]@{
    symbolBuildId = $buildId
    executableSha256 = $executableHash
    package = "$artifactName.zip"
    packageSha256 = $zipHash
}
[IO.File]::WriteAllText((Join-Path $stagedSymbols 'release-info.json'), ($releaseInfo | ConvertTo-Json), $utf8NoBom)
# Archive only after compression succeeds. Unique build IDs preserve every older PDB.
New-Item -ItemType Directory -Path $symbolVersionRoot -Force | Out-Null
if (Test-Path -LiteralPath $symbolRoot) { throw 'The symbol build ID already exists.' }
Move-Item -LiteralPath $stagedSymbols -Destination $symbolRoot
Move-Item -LiteralPath $temporaryZip -Destination $zipPath -Force:$ReplacePackage
[IO.File]::WriteAllText((Join-Path $releaseRoot 'SHA256SUMS.txt'), "$zipHash  $artifactName.zip`n", $utf8NoBom)
Write-Output "ZIP: $zipPath"
Write-Output "SHA256: $zipHash"
Write-Output "Symbols: $symbolRoot"
Write-Output "Symbol build ID: $buildId"
Write-Output "Staged runtime: $runtimeRoot"
Write-Output 'Packaging does not launch, install into app/, publish, commit or push.'

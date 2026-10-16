[CmdletBinding()]
param(
    [string]$DependencyRoot = 'D:/mhy-build-deps',
    [string]$CMakePath,
    [int]$Parallel = 4
)
$ErrorActionPreference = 'Stop'
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$workspaceRoot = Split-Path $sourceRoot -Parent
if (-not $CMakePath) {
    $CMakePath = Join-Path $DependencyRoot 'vcpkg/downloads/tools/cmake-3.31.10-windows/cmake-3.31.10-windows-x86_64/bin/cmake.exe'
}
$ctest = Join-Path (Split-Path $CMakePath) 'ctest.exe'
$qt = Join-Path $DependencyRoot 'qt/6.8.0/msvc2022_64'
$json = Join-Path $sourceRoot '.deps/nlohmann_json/include'
$ffmpeg = Join-Path $sourceRoot '.deps/ffmpeg'
$toolchain = Join-Path $DependencyRoot 'vcpkg/scripts/buildsystems/vcpkg.cmake'
$installed = Join-Path $DependencyRoot 'installed'
$tests = @('kuro_protocol', 'account_storage', 'confirmation_delay', 'task_boundary', 'qr_scanner', 'screen_capture', 'stream_frame')
foreach ($name in $tests) {
    $build = Join-Path $workspaceRoot "build/tests/$name"
    $configure = @('-S', (Join-Path $sourceRoot "tests/$name"), '-B', $build,
        '-G', 'Visual Studio 17 2022', '-A', 'x64',
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain", '-DVCPKG_TARGET_TRIPLET=x64-windows-static-md',
        "-DVCPKG_INSTALLED_DIR=$installed", '-DVCPKG_MANIFEST_INSTALL=OFF',
        "-DCMAKE_PREFIX_PATH=$qt", "-DJSON_INCLUDE_DIR=$json", "-DFFMPEG_ROOT=$ffmpeg")
    & $CMakePath @configure
    if ($LASTEXITCODE -ne 0) { throw "Configure failed: $name" }
    & $CMakePath --build $build --config Release --parallel $Parallel
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $name" }
    & $ctest --test-dir $build -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "Tests failed: $name" }
}
Write-Output 'All configured regression suites passed.'

param(
    [switch]$Test
)

$ErrorActionPreference = 'Stop'
$buildDirectory = Join-Path $PSScriptRoot 'build'
$projectRoot = Split-Path $PSScriptRoot -Parent

$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) {
    $cmake = $cmakeCommand.Source
} else {
    $candidates = @(
        'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
        'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    )
    $cmake = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}

if (-not $cmake) {
    throw 'CMake was not found. Install the Visual Studio 2022 Desktop development with C++ workload.'
}

# Keep FetchContent state isolated by default so parallel worktrees cannot
# modify the same checkout. CI or developers can opt into a shared download
# directory explicitly. If a firmware build has already installed the exact
# LVGL version, use that source directly and avoid a second network fetch.
if ($env:SMART_GRIND_FETCHCONTENT_BASE_DIR) {
    $fetchContentBase = $env:SMART_GRIND_FETCHCONTENT_BASE_DIR
} else {
    $fetchContentBase = Join-Path $buildDirectory '_fetchcontent'
}

$configureArguments = @(
    '-S', $PSScriptRoot,
    '-B', $buildDirectory,
    "-DFETCHCONTENT_BASE_DIR=$fetchContentBase"
)

$lvglCandidates = @()
if ($env:SMART_GRIND_LVGL_SOURCE) {
    $lvglCandidates += $env:SMART_GRIND_LVGL_SOURCE
}
$lvglCandidates += @(
    (Join-Path $projectRoot '.pio\libdeps\waveshare-esp32s3-touch-amoled-164\lvgl'),
    (Join-Path $projectRoot '.pio\libdeps\waveshare-esp32s3-touch-amoled-164-v2\lvgl')
)

$lvglSource = $lvglCandidates |
    Where-Object {
        (Test-Path (Join-Path $_ 'CMakeLists.txt')) -and
        (Test-Path (Join-Path $_ 'library.json')) -and
        (Select-String -LiteralPath (Join-Path $_ 'library.json') -Pattern '"version"\s*:\s*"9\.5\.0"' -Quiet)
    } |
    Select-Object -First 1

if ($lvglSource) {
    Write-Host "Reusing firmware LVGL source: $lvglSource"
    $configureArguments += "-DFETCHCONTENT_SOURCE_DIR_LVGL=$lvglSource"
} else {
    Write-Host "Using shared LVGL download cache: $fetchContentBase"
}

# Let CMake select the installed Visual Studio version so local and GitHub
# Windows builds continue to work as the hosted image advances.
& $cmake @configureArguments
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $cmake --build $buildDirectory --config Release --target smart-grind-sim --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($Test) {
    & $cmake --build $buildDirectory --config Release --target RUN_TESTS
    exit $LASTEXITCODE
}

Write-Host "Simulator built: $buildDirectory\Release\smart-grind-sim.exe"

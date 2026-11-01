param(
    [string]$MiniDbSource = (Join-Path $PSScriptRoot '..\..')
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$bridgeSource = Join-Path $projectRoot 'bridge'
$buildDirectory = Join-Path $bridgeSource 'build'
$resolvedMiniDb = (Resolve-Path $MiniDbSource).Path

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw 'CMake was not found in PATH. Install CMake 3.29+ and reopen PowerShell.'
}

Write-Host "MiniDB source: $resolvedMiniDb"
$cacheFile = Join-Path $buildDirectory 'CMakeCache.txt'
$generatorArguments = @()
if (-not (Test-Path -LiteralPath $cacheFile)) {
    if (Get-Command ninja -ErrorAction SilentlyContinue) {
        $generatorArguments = @('-G', 'Ninja')
    }
    elseif (Get-Command mingw32-make -ErrorAction SilentlyContinue) {
        $generatorArguments = @('-G', 'MinGW Makefiles')
    }
}

cmake -S $bridgeSource -B $buildDirectory @generatorArguments "-DMINIDB_SOURCE_DIR=$resolvedMiniDb" -DPAGECRAFT_BRIDGE_BUILD_TESTS=ON
if ($LASTEXITCODE -ne 0) { throw 'Bridge CMake configuration failed.' }

cmake --build $buildDirectory --config Release
if ($LASTEXITCODE -ne 0) { throw 'Bridge compilation failed.' }

Write-Host 'Real Mode bridge built successfully.' -ForegroundColor Green

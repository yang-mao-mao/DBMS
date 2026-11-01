param(
    [string]$DatabaseDirectory = (Join-Path $PSScriptRoot '..\my_database'),
    [ValidateRange(1, 65535)]
    [int]$Port = 8765,
    [string]$MiniDbSource = (Join-Path $PSScriptRoot '..\..')
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildScript = Join-Path $PSScriptRoot 'build-bridge.ps1'
$buildDirectory = Join-Path $projectRoot 'bridge\build'

& $buildScript -MiniDbSource $MiniDbSource

$candidates = @(
    (Join-Path $buildDirectory 'Release\pagecraft_bridge.exe'),
    (Join-Path $buildDirectory 'pagecraft_bridge.exe'),
    (Join-Path $buildDirectory 'Release\pagecraft_bridge'),
    (Join-Path $buildDirectory 'pagecraft_bridge')
)
$executable = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $executable) {
    throw "pagecraft_bridge executable was not found under $buildDirectory"
}

$databaseFullPath = [System.IO.Path]::GetFullPath($DatabaseDirectory)
New-Item -ItemType Directory -Path $databaseFullPath -Force | Out-Null

Write-Host ''
Write-Host 'Starting PageCraft Real Mode...' -ForegroundColor Cyan
Write-Host "Database directory: $databaseFullPath"
Write-Host "API URL: http://127.0.0.1:$Port"
Write-Host 'Keep this terminal open. Press Ctrl+C to stop.'
Write-Host ''

& $executable $databaseFullPath $Port

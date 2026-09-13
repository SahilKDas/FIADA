param(
  [string]$BuildRoot,
  [string]$OutputRoot
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildRoot) { $BuildRoot = Join-Path $projectRoot 'build-exoskeleton' }
if (-not $OutputRoot) { $OutputRoot = Join-Path $projectRoot 'dist\exoskeleton' }
$BuildRoot = [IO.Path]::GetFullPath($BuildRoot)
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$packageRoot = Join-Path $OutputRoot 'FIADA-windows-x64-exoskeleton'
$zipPath = "$packageRoot.zip"

function Assert-UnderProject([string]$Path) {
  $root = [IO.Path]::GetFullPath($projectRoot).TrimEnd('\')
  $resolved = [IO.Path]::GetFullPath($Path)
  if (-not $resolved.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "Path escapes repository: $resolved"
  }
  $resolved
}
function Remove-Safe([string]$Path) {
  $resolved = Assert-UnderProject $Path
  if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
function Invoke-Checked([string]$File, [string[]]$Arguments) {
  & $File @Arguments
  if ($LASTEXITCODE -ne 0) { throw "$File failed with exit code $LASTEXITCODE" }
}

& (Join-Path $PSScriptRoot 'bootstrap-windows.ps1')

Remove-Safe $BuildRoot
Remove-Safe $packageRoot
if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath (Assert-UnderProject $zipPath) -Force }
New-Item -ItemType Directory -Force $BuildRoot, $packageRoot | Out-Null

$cmake = 'C:\msys64\ucrt64\bin\cmake.exe'
Invoke-Checked $cmake @(
  '-S', $projectRoot, '-B', $BuildRoot, '-G', 'Ninja',
  '-DCMAKE_BUILD_TYPE=Release',
  '-DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/c++.exe'
)
Invoke-Checked $cmake @('--build', $BuildRoot, '--target', 'FIADA', '-j', '2')

Copy-Item -LiteralPath (Join-Path $BuildRoot 'FIADA.exe') -Destination $packageRoot
$assetRoot = Join-Path $packageRoot 'assets'
New-Item -ItemType Directory -Force $assetRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'assets\png') -Destination $assetRoot -Recurse
Copy-Item -LiteralPath (Join-Path $projectRoot 'assets\policy') -Destination $assetRoot -Recurse
Copy-Item -LiteralPath (Join-Path $projectRoot 'packaging\WINDOWS-X64-EXOSKELETON.md') -Destination (Join-Path $packageRoot 'README.md')

$licenseRoot = Join-Path $packageRoot 'licenses'
New-Item -ItemType Directory -Force $licenseRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE') -Destination (Join-Path $licenseRoot 'FIADA-BSD-3-Clause.txt')
Copy-Item -LiteralPath (Join-Path $projectRoot '.deps\skia108\ucrt64\share\licenses\skia') -Destination $licenseRoot -Recurse
foreach ($license in @('gcc-libs','libwinpthread')) {
  Copy-Item -LiteralPath "C:\msys64\ucrt64\share\licenses\$license" -Destination $licenseRoot -Recurse
}
$runtimes = @('libgcc_s_seh-1.dll','libstdc++-6.dll','libwinpthread-1.dll')
foreach ($runtime in $runtimes) {
  Copy-Item -LiteralPath "C:\msys64\ucrt64\bin\$runtime" -Destination $packageRoot
}
$exeCount = @(Get-ChildItem -LiteralPath $packageRoot -Recurse -File -Filter '*.exe').Count
if ($exeCount -ne 1) { throw "Package must contain exactly one executable, found $exeCount" }
if (@(Get-ChildItem -LiteralPath (Join-Path $packageRoot 'assets\png') -File -Filter '*.png').Count -lt 2) {
  throw 'External PNG assets were not staged'
}
$commit = (& git -C $projectRoot rev-parse HEAD).Trim()
if (& git -C $projectRoot status --porcelain) { $commit += '-dirty' }
[IO.File]::WriteAllText((Join-Path $packageRoot 'SOURCE_COMMIT.txt'), "$commit`n")
$hashLines = Get-ChildItem -LiteralPath $packageRoot -Recurse -File |
  Where-Object Name -ne 'SHA256SUMS.txt' |
  Sort-Object FullName |
  ForEach-Object {
    $relative = $_.FullName.Substring($packageRoot.Length + 1).Replace('\','/')
    "$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant())  $relative"
  }
[IO.File]::WriteAllLines((Join-Path $packageRoot 'SHA256SUMS.txt'), $hashLines)
Compress-Archive -LiteralPath $packageRoot -DestinationPath $zipPath -CompressionLevel Optimal
Write-Host "Exoskeleton directory: $packageRoot"
Write-Host "Exoskeleton ZIP: $zipPath"

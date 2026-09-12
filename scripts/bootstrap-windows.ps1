param(
  [string]$EloiRoot = 'C:\SahilAppProjects\Eloi'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$deps = Join-Path $projectRoot '.deps'
New-Item -ItemType Directory -Force $deps | Out-Null

$items = @('skia108', 'static-runtime')
foreach ($item in $items) {
  $source = Join-Path $EloiRoot ".deps\$item"
  $destination = Join-Path $deps $item
  if (-not (Test-Path -LiteralPath $source)) {
    throw "Eloi dependency is missing: $source"
  }
  if (-not (Test-Path -LiteralPath $destination)) {
    Copy-Item -LiteralPath $source -Destination $destination -Recurse
  }
}
$required = @(
  '.deps\skia108\ucrt64\lib\libskia.a',
  '.deps\static-runtime\lib\libjpeg.a',
  '.deps\static-runtime\lib\libpng16.a',
  '.deps\static-runtime\lib\libwebp.a',
  '.deps\static-runtime\lib\libzs.a'
)
foreach ($relative in $required) {
  if (-not (Test-Path -LiteralPath (Join-Path $projectRoot $relative))) {
    throw "Bootstrap validation failed: $relative"
  }
}
Write-Host 'FIADA local Skia 108 and static codecs are ready.'

param(
  [string]$Browser = "C:\Program Files\Google\Chrome\Application\chrome.exe"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $root "assets\svg"
$output = Join-Path $root "assets\png"
New-Item -ItemType Directory -Force $output | Out-Null

$sizes = @{
  "player_car" = "128,72"
  "cone" = "48,64"
}

foreach ($name in $sizes.Keys) {
  $svg = (Resolve-Path (Join-Path $source "$name.svg")).Path.Replace('\', '/')
  $png = Join-Path $output "$name.png"
  & $Browser --headless --disable-gpu --hide-scrollbars --default-background-color=00000000 `
    --window-size=$($sizes[$name]) --screenshot=$png "file:///$svg"
  if ($LASTEXITCODE -ne 0) { throw "Failed to convert $name.svg" }
}


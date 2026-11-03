# Builds the plugin and dist/ModKeybindsMenu-<version>.zip for Nexus (game-folder layout: red4ext/...).
param([string]$Version = "1.0.0")
$root = Split-Path -Parent $PSScriptRoot
& cmd.exe /c (Join-Path $PSScriptRoot "build.bat")
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

$plugin = Join-Path $root "package\red4ext\plugins\ModKeybindsMenu"
Copy-Item (Join-Path $root "README.md") $plugin -Force
Copy-Item (Join-Path $root "LICENSE") (Join-Path $plugin "LICENSE") -Force
Copy-Item (Join-Path $root "deps\RED4ext.SDK\LICENSE.md") (Join-Path $plugin "LICENSE-RED4ext.SDK.md") -Force
Copy-Item (Join-Path $root "deps\pugixml\LICENSE.md") (Join-Path $plugin "LICENSE-pugixml.md") -Force
Copy-Item (Join-Path $root "deps\json\LICENSE.MIT") (Join-Path $plugin "LICENSE-nlohmann-json.txt") -Force
Copy-Item (Join-Path $root "third_party\LICENSE-mod_settings.md") (Join-Path $plugin "LICENSE-mod_settings.md") -Force

$dist = Join-Path $root "dist"
New-Item -ItemType Directory -Force $dist | Out-Null
$zip = Join-Path $dist "ModKeybindsMenu-$Version.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path (Join-Path $root "package\red4ext") -DestinationPath $zip
"Built $zip"

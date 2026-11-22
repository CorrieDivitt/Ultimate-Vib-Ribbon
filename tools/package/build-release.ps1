# Builds the release downloads (task 6.11) on this PC into dist\release-<version>\:
#   Vib-Ribbon-VR-Windows-<version>.zip             launcher, both programs, README, notices, licence
#   Vib-Ribbon-VR-Quest-<version>.zip               the Quest APK, QUEST.txt, notices, licence
#   Vib-Ribbon-Android-<version>-experimental.zip   the phone APK, ANDROID.txt, notices, licence
#   SHA256SUMS.txt                                  checksums of the three, for the release page
# No game data goes in. The APKs are signed with your release key (android\keystore.properties,
# docs\RELEASING.md); without it the script stops rather than ship debug-signed APKs.
#
# Run from a Developer PowerShell (x64) in the repository's folder:   .\tools\package\build-release.ps1
$ErrorActionPreference = "Stop"
$root = Resolve-Path "$PSScriptRoot\..\.."
Set-Location $root

$version = (Select-String -Path CMakeLists.txt -Pattern 'project\(VibRibbonVR VERSION ([0-9.]+)').Matches[0].Groups[1].Value
if (-not (Test-Path android\keystore.properties)) { throw "android\keystore.properties is missing: make your release key first (docs\RELEASING.md)" }
$out = "dist\release-$version"
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory $out | Out-Null

# Windows
cmake --preset package
if ($LASTEXITCODE) { throw "configure failed" }
cmake --build --preset package
if ($LASTEXITCODE) { throw "build failed" }
$built = "build\package"
$win = "$out\Vib-Ribbon-VR"
New-Item -ItemType Directory $win | Out-Null
Copy-Item "$built\apps\launcher\Vib-Ribbon Launcher.exe", "$built\apps\game\Vib-Ribbon.exe", "$built\apps\game\Vib-Ribbon VR.exe" $win
Copy-Item docs\package\README.txt, docs\notices\THIRD_PARTY_NOTICES.txt, LICENSE $win
Compress-Archive -Path $win -DestinationPath "$out\Vib-Ribbon-VR-Windows-$version.zip"

# Android: the Quest and the phone, release builds signed with your key
if (-not $env:JAVA_HOME) { $env:JAVA_HOME = "C:\Program Files\Android\Android Studio\jbr" }
if (-not $env:ANDROID_HOME) { $env:ANDROID_HOME = "$env:LOCALAPPDATA\Android\Sdk" }
Push-Location android
.\gradlew.bat assembleQuestRelease assembleFlatRelease
$gradle = $LASTEXITCODE
Pop-Location
if ($gradle) { throw "the Android build failed" }

$quest = "$out\Vib-Ribbon-VR-Quest"
New-Item -ItemType Directory $quest | Out-Null
Copy-Item android\app\build\outputs\apk\quest\release\app-quest-release.apk "$quest\Vib-Ribbon-VR-Quest-$version.apk"
Copy-Item docs\package\QUEST.txt, docs\notices\THIRD_PARTY_NOTICES.txt, LICENSE $quest
Compress-Archive -Path $quest -DestinationPath "$out\Vib-Ribbon-VR-Quest-$version.zip"

$phone = "$out\Vib-Ribbon-Android"
New-Item -ItemType Directory $phone | Out-Null
Copy-Item android\app\build\outputs\apk\flat\release\app-flat-release.apk "$phone\Vib-Ribbon-Android-$version-experimental.apk"
Copy-Item docs\package\ANDROID.txt, docs\notices\THIRD_PARTY_NOTICES.txt, LICENSE $phone
Compress-Archive -Path $phone -DestinationPath "$out\Vib-Ribbon-Android-$version-experimental.zip"

# Checksums for the release page
Get-ChildItem "$out\*.zip" | ForEach-Object {
  "$((Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower())  $($_.Name)"
} | Set-Content -Encoding ascii "$out\SHA256SUMS.txt"

Write-Host ""
Write-Host "Release $version ready in $(Resolve-Path $out):"
Get-ChildItem "$out\*.zip", "$out\SHA256SUMS.txt" | ForEach-Object { Write-Host "  $($_.Name)  ($([math]::Round($_.Length / 1MB, 1)) MB)" }

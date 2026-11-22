# Builds the Windows download (task 4.16 I) on this PC and zips it for sending:
# dist\Ultimate-Vib-Ribbon-Windows-<date>.zip with Vib-Ribbon Launcher.exe, Vib-Ribbon.exe,
# Vib-Ribbon VR.exe, README.txt and LICENSE. No game data goes in.
#
# Run from a Developer PowerShell (x64: Launch-VsDevShell.ps1 -Arch amd64 -HostArch amd64)
# in the repository's folder:   .\tools\package\build-windows.ps1
$ErrorActionPreference = "Stop"
$root = Resolve-Path "$PSScriptRoot\..\.."
Set-Location $root

cmake --preset package
if ($LASTEXITCODE) { throw "configure failed" }
cmake --build --preset package
if ($LASTEXITCODE) { throw "build failed" }

$built = "build\package"
$folder = "dist\Ultimate-Vib-Ribbon"
if (Test-Path $folder) { Remove-Item -Recurse -Force $folder }
New-Item -ItemType Directory $folder | Out-Null
Copy-Item "$built\apps\launcher\Vib-Ribbon Launcher.exe", "$built\apps\game\Vib-Ribbon.exe", "$built\apps\game\Vib-Ribbon VR.exe" $folder
Copy-Item docs\package\README.txt, docs\notices\THIRD_PARTY_NOTICES.txt, LICENSE $folder

$commit = (git rev-parse --short HEAD).Trim()
$zip = "dist\Ultimate-Vib-Ribbon-Windows-$(Get-Date -Format yyyy-MM-dd)-$commit.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $folder -DestinationPath $zip
Write-Host ""
Write-Host "Ready to send: $(Resolve-Path $zip)"
Write-Host "Your friend unzips it anywhere and starts 'Vib-Ribbon Launcher.exe'."

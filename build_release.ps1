# Builds the firmware and writes two images to .\release:
#   esp32-cyd-musicplayer-ota.bin   app only: for OTA / the web updater (goes into the inactive ota slot)
#   esp32-cyd-musicplayer-full.bin  bootloader + partition table + otadata + app: flash at 0x0 over USB
param(
    [string]$IdfProfile = "C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1"
)
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    . $IdfProfile | Out-Null
}

# Build stamp (see CMakeLists.txt): shown on the upload web page
Set-Content (Join-Path $PSScriptRoot "build_stamp.txt") (Get-Date -Format "yyyy-MM-dd HH:mm:ss")

idf.py build
if ($LASTEXITCODE -ne 0) { throw "build failed" }

New-Item -ItemType Directory -Force release | Out-Null
$name = "esp32-cyd-musicplayer"
Copy-Item "build\$name.bin" "release\$name-ota.bin" -Force

idf.py merge-bin -o "$PSScriptRoot\release\$name-full.bin" -f raw
if ($LASTEXITCODE -ne 0) { throw "merge-bin failed" }

Get-ChildItem release | Format-Table Name, Length
Write-Host "USB:  esptool.py --chip esp32s3 -p COMx write_flash 0x0 release\$name-full.bin"
Write-Host "OTA:  upload release\$name-ota.bin to the (future) web updater"

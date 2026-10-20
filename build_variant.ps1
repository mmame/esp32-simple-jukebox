# Builds an experimental variant: sdkconfig.defaults + sdkconfig.<Name> into build_variant_<Name>\,
# images go to .\release\esp32-cyd-musicplayer-<Name>-ota.bin / -full.bin. The normal build is untouched.
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [string]$IdfProfile = "C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1"
)
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    . $IdfProfile | Out-Null
}

# Build stamp (see CMakeLists.txt): shown on the upload web page
Set-Content (Join-Path $PSScriptRoot "build_stamp.txt") (Get-Date -Format "yyyy-MM-dd HH:mm:ss")

$proj = "esp32-cyd-musicplayer"
$dir = "build_variant_$Name"
$cfg = "-DSDKCONFIG=$dir\sdkconfig", "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.$Name"

idf.py -B $dir @cfg build
if ($LASTEXITCODE -ne 0) { throw "build failed" }

New-Item -ItemType Directory -Force release | Out-Null
Copy-Item "$dir\$proj.bin" "release\$proj-$Name-ota.bin" -Force
idf.py -B $dir @cfg merge-bin -o "$PSScriptRoot\release\$proj-$Name-full.bin" -f raw
if ($LASTEXITCODE -ne 0) { throw "merge-bin failed" }

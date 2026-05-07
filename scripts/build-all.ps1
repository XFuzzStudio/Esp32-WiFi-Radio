$ErrorActionPreference = "Stop"

$cli = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
if (!(Test-Path $cli)) {
  $cli = "C:\Program Files\Arduino CLI\arduino-cli.exe"
  if (!(Test-Path $cli)) {
    $cmd = Get-Command arduino-cli -ErrorAction SilentlyContinue
    if (!$cmd) {
    throw "arduino-cli not found. Install Arduino IDE 2.x or put arduino-cli in PATH."
    }
    $cli = $cmd.Source
  }
}

$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root "build-output"
New-Item -ItemType Directory -Path $out -Force | Out-Null

$fqbnS3 = "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=default"
$fqbnC6 = "esp32:esp32:esp32c6:FlashSize=8M,PartitionScheme=default_8MB"
$fqbnLoader = "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=custom,USBMode=hwcdc,CDCOnBoot=default"

function Build-Sketch {
  param(
  [string]$Name,
    [string]$Fqbn
  )

  $buildPath = Join-Path $out $Name
  if (Test-Path $buildPath) {
    Remove-Item -Recurse -Force $buildPath
  }
  New-Item -ItemType Directory -Path $buildPath | Out-Null

  Write-Host "==> Building $Name"
  $jobs = [Environment]::ProcessorCount
  & $cli compile --fqbn $Fqbn --jobs $jobs --build-path $buildPath (Join-Path $root $Name)
}

Build-Sketch "ESP32WiFiRadio" $fqbnS3
Build-Sketch "ESP-WiFi-Scanner" $fqbnS3
Build-Sketch "ESP-GiF-Player" $fqbnS3
Build-Sketch "ESP32WiFiRadioPilot" $fqbnC6
Build-Sketch "ESP32BinLoader" $fqbnLoader

$loaderAppDir = Join-Path $root "ESP32BinLoader\sd_card\apps"
New-Item -ItemType Directory -Path $loaderAppDir -Force | Out-Null

$loaderApps = @(
  @{ Sketch = "ESP32WiFiRadio"; Bin = "radio_lvgl.bin" },
  @{ Sketch = "ESP-WiFi-Scanner"; Bin = "esp_wifi_scanner.bin" },
  @{ Sketch = "ESP-GiF-Player"; Bin = "esp_gif_player.bin" }
)

foreach ($app in $loaderApps) {
  $source = Join-Path $out "$($app.Sketch)\$($app.Sketch).ino.bin"
  if (Test-Path $source) {
    Copy-Item $source (Join-Path $loaderAppDir $app.Bin) -Force
    Write-Host "Copied $($app.Sketch) bin to ESP32BinLoader\sd_card\apps\$($app.Bin)"
  }
}

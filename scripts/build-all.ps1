param(
  [ValidateSet("all", "ESP32WiFiRadio", "ESP-WiFi-Scanner", "ESP-GiF-Player", "ESP32WiFiRadioPilot", "ESP32BinLoader")]
  [string]$Sketch = "all",
  [int]$Jobs = 0,
  [switch]$Clean
)

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

if ($Jobs -le 0) {
  $logicalCores = [Environment]::ProcessorCount
  $Jobs = [Math]::Max(1, $logicalCores - 1)
  if ($logicalCores -eq 4) {
    $Jobs = 3
  }
}

$fqbnS3 = "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=default"
$fqbnC6 = "esp32:esp32:esp32c6:FlashSize=8M,PartitionScheme=default_8MB"
$fqbnLoader = "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=custom,USBMode=hwcdc,CDCOnBoot=default"

function Receive-BuildOutput {
  param(
    [System.Management.Automation.Job]$Job,
    [ref]$ExitCode
  )

  Receive-Job $Job | ForEach-Object {
    if ($_.PSObject.Properties.Name -contains "BuildExitCode") {
      $ExitCode.Value = [int]$_.BuildExitCode
    } else {
      Write-Host $_
    }
  }
}

function Build-Sketch {
  param(
    [string]$Name,
    [string]$Fqbn,
    [int]$Index,
    [int]$Total
  )

  $buildPath = Join-Path $out $Name
  if ($Clean -and (Test-Path $buildPath)) {
    Remove-Item -Recurse -Force $buildPath
  }
  New-Item -ItemType Directory -Path $buildPath -Force | Out-Null

  $percent = [int](($Index - 1) * 100 / $Total)
  Write-Progress -Activity "Building ESP32 WiFi Radio suite" -Status "[$Index/$Total] $Name" -PercentComplete $percent
  Write-Host ("[{0}/{1}] Building {2} with {3} job(s){4}" -f $Index, $Total, $Name, $Jobs, $(if ($Clean) { " clean" } else { " cached" }))

  $sketchPath = Join-Path $root $Name
  $compileArgs = @("compile", "--fqbn", $Fqbn, "--jobs", "$Jobs", "--build-path", $buildPath, $sketchPath)
  $compileJob = Start-Job -ScriptBlock {
    param(
      [string]$CliPath,
      [string[]]$CliArgs
    )

    & $CliPath @CliArgs 2>&1
    [pscustomobject]@{ BuildExitCode = $LASTEXITCODE }
  } -ArgumentList $cli, $compileArgs

  $exitCode = $null
  $startTime = Get-Date
  $slice = 100.0 / $Total
  $estimatedSeconds = if ($Clean) { 420.0 } else { 180.0 }

  while ($compileJob.State -eq "Running") {
    Receive-BuildOutput $compileJob ([ref]$exitCode)
    $elapsed = (Get-Date) - $startTime
    $innerPercent = [Math]::Min(96, [int](($elapsed.TotalSeconds / $estimatedSeconds) * 96))
    $overallPercent = [int]([Math]::Min(99, $percent + (($innerPercent / 100.0) * $slice)))
    $elapsedText = "{0:mm\:ss}" -f $elapsed
    Write-Progress `
      -Activity "Building ESP32 WiFi Radio suite" `
      -Status ("[{0}/{1}] {2} - {3}% sketch, elapsed {4}, jobs {5}" -f $Index, $Total, $Name, $innerPercent, $elapsedText, $Jobs) `
      -PercentComplete $overallPercent
    Start-Sleep -Milliseconds 500
  }

  Receive-BuildOutput $compileJob ([ref]$exitCode)

  if ($compileJob.State -eq "Failed") {
    $errorText = $compileJob.ChildJobs[0].JobStateInfo.Reason
    Remove-Job $compileJob -Force
    throw "Build failed: $Name. $errorText"
  }

  Remove-Job $compileJob -Force

  if ($null -eq $exitCode -or $exitCode -ne 0) {
    throw "Build failed: $Name"
  }
  Write-Progress -Activity "Building ESP32 WiFi Radio suite" -Status "[$Index/$Total] $Name done" -PercentComplete ([int]($Index * 100 / $Total))
}

$sketches = @(
  [pscustomobject]@{ Name = "ESP32WiFiRadio"; Fqbn = $fqbnS3 },
  [pscustomobject]@{ Name = "ESP-WiFi-Scanner"; Fqbn = $fqbnS3 },
  [pscustomobject]@{ Name = "ESP-GiF-Player"; Fqbn = $fqbnS3 },
  [pscustomobject]@{ Name = "ESP32WiFiRadioPilot"; Fqbn = $fqbnC6 },
  [pscustomobject]@{ Name = "ESP32BinLoader"; Fqbn = $fqbnLoader }
)

$selectedSketches = @(
  if ($Sketch -eq "all") {
    $sketches
  } else {
    $sketches | Where-Object { $_.Name -eq $Sketch }
  }
)

for ($i = 0; $i -lt $selectedSketches.Count; $i++) {
  Build-Sketch $selectedSketches[$i].Name $selectedSketches[$i].Fqbn ($i + 1) $selectedSketches.Count
}

Write-Progress -Activity "Building ESP32 WiFi Radio suite" -Completed

$loaderAppDir = Join-Path $root "ESP32BinLoader\sd_card\apps"
New-Item -ItemType Directory -Path $loaderAppDir -Force | Out-Null

$loaderApps = @(
  @{ Sketch = "ESP32WiFiRadio"; Bin = "radio_lvgl.bin" },
  @{ Sketch = "ESP-WiFi-Scanner"; Bin = "esp_wifi_scanner.bin" },
  @{ Sketch = "ESP-GiF-Player"; Bin = "esp_gif_player.bin" }
)

foreach ($app in $loaderApps) {
  if ($Sketch -ne "all" -and $Sketch -ne $app.Sketch) {
    continue
  }
  $source = Join-Path $out "$($app.Sketch)\$($app.Sketch).ino.bin"
  if (Test-Path $source) {
    Copy-Item $source (Join-Path $loaderAppDir $app.Bin) -Force
    Write-Host "Copied $($app.Sketch) bin to ESP32BinLoader\sd_card\apps\$($app.Bin)"
  }
}

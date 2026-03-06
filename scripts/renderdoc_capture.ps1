param(
    [string]$Exe = "build\\bin\\Debug\\song_demo.exe",
    [string]$RenderDocCmd = "renderdoccmd.exe"
)

$exePath = Resolve-Path -Path $Exe -ErrorAction SilentlyContinue
if (-not $exePath) {
    Write-Error "Executable not found: $Exe"
    exit 1
}

$rd = Get-Command $RenderDocCmd -ErrorAction SilentlyContinue
if (-not $rd) {
    Write-Warning "renderdoccmd not found in PATH. Running without capture."
    & $exePath
    exit $LASTEXITCODE
}

$workingDir = Split-Path -Path $exePath -Parent
Write-Host "Starting RenderDoc capture for $exePath"

& $RenderDocCmd capture --exe $exePath --working-dir $workingDir --cmdline ""
exit $LASTEXITCODE

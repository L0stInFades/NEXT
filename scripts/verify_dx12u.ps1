[CmdletBinding()]
param(
    [string]$Preset = "windows-dx12-dev",
    [string]$Config = "Debug",
    [int]$SmokeFrames = 120,
    [string]$BuildDir = "out/build/windows-dx12-dev",
    [switch]$SkipRuntimeSmoke
)

$ErrorActionPreference = "Stop"

function Resolve-FirstExistingPath {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Candidates
    )

    foreach ($candidate in $Candidates) {
        if (Test-Path -Path $candidate) {
            return (Resolve-Path -Path $candidate).Path
        }
    }

    return $null
}

function Resolve-ToolPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$CommandName,
        [string[]]$Candidates = @()
    )

    $command = Get-Command $CommandName -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    foreach ($candidate in $Candidates) {
        $matches = Get-ChildItem -Path $candidate -ErrorAction SilentlyContinue
        if ($matches) {
            return ($matches | Sort-Object FullName -Descending | Select-Object -First 1).FullName
        }
    }

    return $null
}

function Resolve-WindowsSdkVersion {
    $includeRoot = "${env:ProgramFiles(x86)}\Windows Kits\10\Include"
    if (-not (Test-Path -Path $includeRoot)) {
        return $null
    }

    $versions = Get-ChildItem -Path $includeRoot -Directory -ErrorAction SilentlyContinue |
        ForEach-Object {
            try {
                [version]$_.Name
            } catch {
                $null
            }
        } |
        Where-Object { $_ -ne $null } |
        Sort-Object -Descending

    return $versions | Select-Object -First 1
}

function Resolve-DxcompilerPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$DxcPath
    )

    $candidates = New-Object System.Collections.Generic.List[string]
    $dxcDirectory = Split-Path -Path $DxcPath -Parent
    if ($dxcDirectory) {
        $candidates.Add((Join-Path $dxcDirectory "dxcompiler.dll"))
    }

    if ($env:PATH) {
        foreach ($pathEntry in ($env:PATH -split [System.IO.Path]::PathSeparator)) {
            if ($pathEntry) {
                $candidates.Add((Join-Path $pathEntry "dxcompiler.dll"))
            }
        }
    }

    $system32 = Join-Path $env:WINDIR "System32\dxcompiler.dll"
    $candidates.Add($system32)

    return Resolve-FirstExistingPath -Candidates $candidates.ToArray()
}

$isRunningWindows = ($env:OS -eq "Windows_NT") -or
    ($PSVersionTable.ContainsKey("Platform") -and $PSVersionTable.Platform -eq "Win32NT") -or
    (Get-Variable -Name IsWindows -ValueOnly -ErrorAction SilentlyContinue)

if (-not $isRunningWindows) {
    throw "DX12U verification must run on Windows."
}

$repoRoot = (Resolve-Path -Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $repoRoot

$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) {
    throw "cmake was not found in PATH."
}

$minimumSdkVersion = [version]"10.0.20348.0"
$windowsSdkVersion = Resolve-WindowsSdkVersion
if (-not $windowsSdkVersion -or $windowsSdkVersion -lt $minimumSdkVersion) {
    throw "Windows SDK 10.0.20348.0 or newer is required for DX12U sampler feedback and mesh shader APIs. Found: $windowsSdkVersion"
}
Write-Host "Using Windows SDK: $windowsSdkVersion"

$dxc = Resolve-ToolPath -CommandName "dxc" -Candidates @(
    "$env:ProgramFiles\Microsoft DirectX Shader Compiler\bin\x64\dxc.exe",
    "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\dxc.exe"
)
if (-not $dxc) {
    throw "dxc was not found in PATH. DX12U verification requires DXC for Shader Model 6 paths."
} else {
    Write-Host "Using DXC: $dxc"
}

$dxcompiler = Resolve-DxcompilerPath -DxcPath $dxc
if (-not $dxcompiler) {
    throw "dxcompiler.dll was not found next to dxc or in PATH. Runtime SM6/DXR shader compilation requires dxcompiler.dll."
}
Write-Host "Using dxcompiler.dll: $dxcompiler"

$dxcompilerDir = Split-Path -Path $dxcompiler -Parent
if ($dxcompilerDir -and -not (($env:PATH -split [System.IO.Path]::PathSeparator) -contains $dxcompilerDir)) {
    $env:PATH = "$dxcompilerDir$([System.IO.Path]::PathSeparator)$env:PATH"
}

& (Join-Path $PSScriptRoot "verify_hlsl.ps1") -RequireCompiler -PreferDxc
if (-not $?) {
    exit 1
}

Write-Host "Configuring preset '$Preset'..."
& cmake --preset $Preset
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "Building song_demo with preset '$Preset' ($Config)..."
& cmake --build --preset $Preset --config $Config --target song_demo
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$exe = Resolve-FirstExistingPath -Candidates @(
    (Join-Path $BuildDir "bin/$Config/song_demo.exe"),
    (Join-Path $BuildDir "bin/song_demo.exe"),
    (Join-Path $BuildDir "game/$Config/song_demo.exe"),
    (Join-Path $BuildDir "game/song_demo.exe")
)

if (-not $exe) {
    throw "song_demo.exe was not found under '$BuildDir'."
}

if ($SkipRuntimeSmoke) {
    Write-Host "DX12U build and HLSL verification completed; runtime smoke was skipped."
    return
}

$env:NEXT_RENDERER_BACKEND = "dx12"
$env:NEXT_REQUIRE_DX12U = "1"
$env:NEXT_MESH_SHADER_DEBUG = "1"
$env:NEXT_SAMPLER_FEEDBACK_DEBUG = "1"
$workingDir = Split-Path -Path $exe -Parent

function Invoke-DX12USmoke {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Label,
        [Parameter(Mandatory = $true)]
        [int]$Frames,
        [string]$GITechnique = "",
        [string]$AOTechnique = "",
        [switch]$MeshShaderDebug,
        [switch]$SamplerFeedbackDebug
    )

    if ([string]::IsNullOrEmpty($GITechnique)) {
        Remove-Item Env:NEXT_GI_TECHNIQUE -ErrorAction SilentlyContinue
    } else {
        $env:NEXT_GI_TECHNIQUE = $GITechnique
    }

    if ([string]::IsNullOrEmpty($AOTechnique)) {
        Remove-Item Env:NEXT_AO_TECHNIQUE -ErrorAction SilentlyContinue
    } else {
        $env:NEXT_AO_TECHNIQUE = $AOTechnique
    }

    if ($MeshShaderDebug) {
        $env:NEXT_MESH_SHADER_DEBUG = "1"
    } else {
        Remove-Item Env:NEXT_MESH_SHADER_DEBUG -ErrorAction SilentlyContinue
    }

    if ($SamplerFeedbackDebug) {
        $env:NEXT_SAMPLER_FEEDBACK_DEBUG = "1"
    } else {
        Remove-Item Env:NEXT_SAMPLER_FEEDBACK_DEBUG -ErrorAction SilentlyContinue
    }

    Write-Host "Running DX12U smoke test [$Label]: $exe --smoke-frames $Frames --allow-placeholder-cells"
    $smokeLog = [System.IO.Path]::GetTempFileName()
    & $exe --smoke-frames $Frames --allow-placeholder-cells 2>&1 | Tee-Object -FilePath $smokeLog
    $smokeExitCode = $LASTEXITCODE
    if ($smokeExitCode -ne 0) {
        Remove-Item -Path $smokeLog -ErrorAction SilentlyContinue
        exit $smokeExitCode
    }

    $forbiddenSmokePatterns = @(
        "advanced features disabled",
        "will not be available",
        "RTGI disabled",
        "RTGI render skipped",
        "RTGI acceleration structure build skipped",
        "command list does not expose DXR build API",
        "command list does not support DispatchRays",
        "dynamic voxelization is not bound",
        "VXGI voxelization skipped",
        "Cannot render mesh shader pass",
        "command list does not expose ID3D12GraphicsCommandList6",
        "fell back to neutral AO",
        "neutral AO resolve",
        "NEXT_REQUIRE_DX12U is set, but"
    )

    $strictModeMatches = Select-String -Path $smokeLog -Pattern $forbiddenSmokePatterns -SimpleMatch
    if ($strictModeMatches) {
        $matchText = ($strictModeMatches | ForEach-Object { "$($_.LineNumber): $($_.Line)" }) -join [Environment]::NewLine
        Write-Error "DX12U smoke log contained strict-mode fallback or skipped-path messages:$([Environment]::NewLine)$matchText"
        Remove-Item -Path $smokeLog -ErrorAction SilentlyContinue
        exit 1
    }

    $requiredSmokePatterns = @(
        "Post-processing initialized successfully"
    )

    switch ($Label) {
        "hybrid-default" {
            $requiredSmokePatterns += @(
                "GI Manager initialized (technique: Hybrid)",
                "GTAO pass rendered",
                "GTAO spatial filter rendered",
                "VXGI initialized",
                "DDGI initialized",
                "RTGI initialized with DXR DispatchRays pipeline",
                "RTGI acceleration structures built"
            )
        }
        "hbao-probe" {
            $requiredSmokePatterns += @(
                "AO technique override from NEXT_AO_TECHNIQUE=hbao",
                "Switched to HBAO",
                "HBAO pass rendered",
                "HBAO blur pass rendered"
            )
        }
        "vxao-probe" {
            $requiredSmokePatterns += @(
                "AO technique override from NEXT_AO_TECHNIQUE=vxao",
                "Switched to VXAO",
                "VXAO pass rendered"
            )
        }
        "ssgi-probe" {
            $requiredSmokePatterns += @(
                "GI technique override from NEXT_GI_TECHNIQUE=ssgi",
                "GI technique changed to: 2",
                "Screen-space probe GI rendered"
            )
        }
        "voxelgi-probe" {
            $requiredSmokePatterns += @(
                "GI technique override from NEXT_GI_TECHNIQUE=voxelgi",
                "GI technique changed to: 3",
                "VXGI dynamic voxelization dispatched",
                "VXGI cone trace rendered"
            )
        }
        "mesh-shader-probe" {
            $requiredSmokePatterns += @(
                "DX12U mesh shader debug pass enabled",
                "Mesh shader pass dispatched"
            )
        }
        "sampler-feedback-probe" {
            $requiredSmokePatterns += @(
                "DX12U sampler feedback debug pass enabled",
                "Sampler feedback pass dispatched"
            )
        }
    }

    foreach ($pattern in $requiredSmokePatterns) {
        if (-not (Select-String -Path $smokeLog -Pattern $pattern -SimpleMatch -Quiet)) {
            Write-Error "DX12U smoke log for [$Label] did not contain required evidence: $pattern"
            Remove-Item -Path $smokeLog -ErrorAction SilentlyContinue
            exit 1
        }
    }

    Remove-Item -Path $smokeLog -ErrorAction SilentlyContinue
}

Push-Location $workingDir
try {
    Invoke-DX12USmoke -Label "hybrid-default" -Frames $SmokeFrames
    Invoke-DX12USmoke -Label "hbao-probe" -Frames 2 -AOTechnique "hbao"
    Invoke-DX12USmoke -Label "vxao-probe" -Frames 2 -AOTechnique "vxao"
    Invoke-DX12USmoke -Label "ssgi-probe" -Frames 2 -GITechnique "ssgi"
    Invoke-DX12USmoke -Label "voxelgi-probe" -Frames 2 -GITechnique "voxelgi"
    Invoke-DX12USmoke -Label "mesh-shader-probe" -Frames 2 -MeshShaderDebug
    Invoke-DX12USmoke -Label "sampler-feedback-probe" -Frames 2 -SamplerFeedbackDebug
} finally {
    Remove-Item Env:NEXT_RENDERER_BACKEND -ErrorAction SilentlyContinue
    Remove-Item Env:NEXT_REQUIRE_DX12U -ErrorAction SilentlyContinue
    Remove-Item Env:NEXT_GI_TECHNIQUE -ErrorAction SilentlyContinue
    Remove-Item Env:NEXT_AO_TECHNIQUE -ErrorAction SilentlyContinue
    Remove-Item Env:NEXT_MESH_SHADER_DEBUG -ErrorAction SilentlyContinue
    Remove-Item Env:NEXT_SAMPLER_FEEDBACK_DEBUG -ErrorAction SilentlyContinue
    Pop-Location
}

Write-Host "DX12U smoke verification completed."

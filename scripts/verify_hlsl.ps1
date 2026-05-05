[CmdletBinding()]
param(
    [switch]$RequireCompiler,
    [switch]$PreferDxc
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path -Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $repoRoot

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

$dxcCandidates = @(
    "$env:ProgramFiles\Microsoft DirectX Shader Compiler\bin\x64\dxc.exe",
    "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\dxc.exe"
)
$fxcCandidates = @(
    "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\fxc.exe"
)

$fxc = Resolve-ToolPath -CommandName "fxc" -Candidates $fxcCandidates
$dxc = Resolve-ToolPath -CommandName "dxc" -Candidates $dxcCandidates

if (-not $fxc -and -not $dxc) {
    $message = "Neither fxc nor dxc was found in PATH; HLSL syntax verification was skipped."
    if ($RequireCompiler) {
        throw $message
    }

    Write-Warning $message
    return
}

$tempDir = Join-Path ([System.IO.Path]::GetTempPath()) ("next-hlsl-" + [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $tempDir | Out-Null

$shaders = @(
    @{ Path = "engine/renderer/shaders/cube.vs.hlsl"; Entry = "main"; FxcTarget = "vs_5_1"; DxcTarget = "vs_6_0" },
    @{ Path = "engine/renderer/shaders/cube.ps.hlsl"; Entry = "main"; FxcTarget = "ps_5_1"; DxcTarget = "ps_6_0" },
    @{ Path = "engine/renderer/shaders/triangle.vs.hlsl"; Entry = "main"; FxcTarget = "vs_5_1"; DxcTarget = "vs_6_0" },
    @{ Path = "engine/renderer/shaders/triangle.ps.hlsl"; Entry = "main"; FxcTarget = "ps_5_1"; DxcTarget = "ps_6_0" },
    @{ Path = "engine/renderer/shaders/pbr.vs.hlsl"; Entry = "main"; FxcTarget = "vs_5_1"; DxcTarget = "vs_6_0" },
    @{ Path = "engine/renderer/shaders/pbr.ps.hlsl"; Entry = "main"; FxcTarget = "ps_5_1"; DxcTarget = "ps_6_0" },
    @{ Path = "engine/renderer/shaders/pbr_simple.ps.hlsl"; Entry = "main"; FxcTarget = "ps_5_1"; DxcTarget = "ps_6_0" },
    @{ Path = "engine/renderer/shaders/pbr_step1.ps.hlsl"; Entry = "main"; FxcTarget = "ps_5_1"; DxcTarget = "ps_6_0" },
    @{ Path = "engine/renderer/shaders/post_process.vs.hlsl"; Entry = "main"; FxcTarget = "vs_5_1"; DxcTarget = "vs_6_0" },
    @{ Path = "engine/renderer/shaders/post_process.ps.hlsl"; Entry = "main"; FxcTarget = "ps_5_1"; DxcTarget = "ps_6_0" },
    @{ Path = "engine/renderer/shaders/taa_resolve.ps.hlsl"; Entry = "main"; FxcTarget = "ps_5_1"; DxcTarget = "ps_6_0" },
    @{ Path = "engine/renderer/shaders/gi_combine.ps.hlsl"; Entry = "main"; FxcTarget = "ps_5_1"; DxcTarget = "ps_6_0" },
    @{ Path = "engine/renderer/shaders/gtao.hlsl"; Entry = "PSGTAO"; FxcTarget = "ps_5_1"; DxcTarget = "ps_6_0" },
    @{ Path = "engine/renderer/shaders/gtao_filter.hlsl"; Entry = "PSFilter"; FxcTarget = "ps_5_1"; DxcTarget = "ps_6_0" },
    @{ Path = "engine/renderer/shaders/gtao_filter.hlsl"; Entry = "PSTemporal"; FxcTarget = "ps_5_1"; DxcTarget = "ps_6_0" },
    @{ Path = "engine/renderer/shaders/hbao.hlsl"; Entry = "PSHBAO"; FxcTarget = "ps_5_1"; DxcTarget = "ps_6_0" },
    @{ Path = "engine/renderer/shaders/hbao.hlsl"; Entry = "PSBlur"; FxcTarget = "ps_5_1"; DxcTarget = "ps_6_0" },
    @{ Path = "engine/renderer/shaders/vxao.hlsl"; Entry = "PSVXAO"; FxcTarget = "ps_5_1"; DxcTarget = "ps_6_0" },
    @{ Path = "engine/renderer/shaders/ddgi.hlsl"; Entry = "CSUpdateProbes"; FxcTarget = "cs_5_1"; DxcTarget = "cs_6_0" },
    @{ Path = "engine/renderer/shaders/ddgi.hlsl"; Entry = "PSRenderDDGI"; FxcTarget = "ps_5_1"; DxcTarget = "ps_6_0" },
    @{ Path = "engine/renderer/shaders/ddgi.hlsl"; Entry = "CSGenerateProbeRays"; FxcTarget = "cs_5_1"; DxcTarget = "cs_6_0" },
    @{ Path = "engine/renderer/shaders/ddgi.hlsl"; Entry = "CSUpdateProbeDepth"; FxcTarget = "cs_5_1"; DxcTarget = "cs_6_0" },
    @{ Path = "engine/renderer/shaders/ddgi_render.ps.hlsl"; Entry = "main"; FxcTarget = "ps_5_1"; DxcTarget = "ps_6_0" },
    @{ Path = "engine/renderer/shaders/light_probe.hlsl"; Entry = "CSBakeProbes"; FxcTarget = "cs_5_1"; DxcTarget = "cs_6_0" },
    @{ Path = "engine/renderer/shaders/light_probe.hlsl"; Entry = "PSEvaluateProbes"; FxcTarget = "ps_5_1"; DxcTarget = "ps_6_0" },
    @{ Path = "engine/renderer/shaders/light_probe.hlsl"; Entry = "PSVisualizeProbes"; FxcTarget = "ps_5_1"; DxcTarget = "ps_6_0" },
    @{ Path = "engine/renderer/shaders/screen_space_probe_gi.ps.hlsl"; Entry = "main"; FxcTarget = "ps_5_1"; DxcTarget = "ps_6_0" },
    @{ Path = "engine/renderer/shaders/vxgi_voxelize.cs.hlsl"; Entry = "main"; FxcTarget = "cs_5_1"; DxcTarget = "cs_6_0" },
    @{ Path = "engine/renderer/shaders/vxgi_cone_trace.ps.hlsl"; Entry = "main"; FxcTarget = "ps_5_1"; DxcTarget = "ps_6_0" },
    @{ Path = "engine/renderer/shaders/mesh_debug.ms.hlsl"; Entry = "main"; DxcTarget = "ms_6_5"; DxcOnly = $true },
    @{ Path = "engine/renderer/shaders/mesh_debug.ps.hlsl"; Entry = "main"; DxcTarget = "ps_6_5"; DxcOnly = $true },
    @{ Path = "engine/renderer/shaders/sampler_feedback.cs.hlsl"; Entry = "main"; DxcTarget = "cs_6_5"; DxcOnly = $true },
    @{ Path = "engine/renderer/shaders/rtgi.raytracing.hlsl"; Entry = ""; DxcTarget = "lib_6_3"; DxcOnly = $true; Library = $true }
)

try {
    foreach ($shader in $shaders) {
        if (-not (Test-Path -Path $shader.Path)) {
            throw "Shader file not found: $($shader.Path)"
        }

        $output = Join-Path $tempDir ([System.IO.Path]::GetFileNameWithoutExtension($shader.Path) + ".cso")
        $entryLabel = if ($shader.Library) { "<library>" } else { $shader.Entry }
        Write-Host "Checking HLSL: $($shader.Path) [$entryLabel]"

        if ($dxc -and ($shader.DxcOnly -or $PreferDxc -or -not $fxc)) {
            $dxcArgs = @("-nologo", "-T", $shader.DxcTarget, "-Fo", $output)
            if (-not $shader.Library) {
                $dxcArgs += @("-E", $shader.Entry)
            }
            $dxcArgs += $shader.Path
            & $dxc @dxcArgs
        } else {
            if ($shader.DxcOnly) {
                $message = "Shader $($shader.Path) requires DXC target $($shader.DxcTarget), but DXC was not selected."
                if ($RequireCompiler) {
                    throw $message
                }

                Write-Warning $message
                continue
            }
            & $fxc /nologo /T $shader.FxcTarget /E $shader.Entry /Fo $output $shader.Path
        }

        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    }
} finally {
    Remove-Item -Path $tempDir -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "HLSL syntax verification completed."

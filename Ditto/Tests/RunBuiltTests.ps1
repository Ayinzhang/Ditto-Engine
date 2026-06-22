param(
    [Parameter(Mandatory=$true)][string]$TestsExe,
    [Parameter(Mandatory=$true)][string]$RenderExe,
    [Parameter(Mandatory=$true)][string]$RenderOut,
    [bool]$RunVulkan = $false
)

$ErrorActionPreference = "Stop"

Write-Host "[DittoTests] Stage 1/4: file operations"
& $TestsExe --stage file
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "[DittoTests] Stage 2/4: C# scripting"
& $TestsExe --stage csharp
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "[DittoTests] Stage 3/4: rendering and shader output"
$glRenderOut = Join-Path (Split-Path $RenderOut -Parent) "RenderSmokeGL"
& $RenderExe --backend opengl --out $glRenderOut
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($RunVulkan) {
    Write-Host "[DittoTests] Stage 3/4: Vulkan rendering and shader output"
    $vkRenderOut = Join-Path (Split-Path $RenderOut -Parent) "RenderSmokeVK"
    & $RenderExe --backend vulkan --out $vkRenderOut
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Write-Host "[DittoTests] Stage 4/4: physics simulation"
& $TestsExe --stage simulation
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "[DittoTests] AI scene dump"
& $TestsExe --dump-scene
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

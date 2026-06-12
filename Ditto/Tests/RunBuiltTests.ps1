param(
    [Parameter(Mandatory=$true)][string]$TestsExe,
    [Parameter(Mandatory=$true)][string]$RenderExe,
    [Parameter(Mandatory=$true)][string]$RenderOut
)

$ErrorActionPreference = "Stop"

Write-Host "[DittoTests] Stage 1/4: file operations"
& $TestsExe --stage file
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "[DittoTests] Stage 2/4: C# scripting"
& $TestsExe --stage csharp
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "[DittoTests] Stage 3/4: rendering and shader output"
& $RenderExe --out $RenderOut
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "[DittoTests] Stage 4/4: physics simulation"
& $TestsExe --stage simulation
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "[DittoTests] AI scene dump"
& $TestsExe --dump-scene
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

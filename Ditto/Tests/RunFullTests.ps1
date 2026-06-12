param(
    [string]$BuildDir = "build-tests",
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"

function Invoke-Native {
    param(
        [Parameter(Mandatory=$true)][string]$Command,
        [Parameter(ValueFromRemainingArguments=$true)][string[]]$CommandArgs
    )

    & $Command @CommandArgs
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

Write-Host "[DittoTests] Configure"
Invoke-Native cmake -S Ditto -B $BuildDir

Write-Host "[DittoTests] Build"
Invoke-Native cmake --build $BuildDir --target DittoTests --config $Config
Invoke-Native cmake --build $BuildDir --target DittoRenderSmoke --config $Config

$testsExe = Join-Path $BuildDir "$Config\DittoTests.exe"
$renderExe = Join-Path $BuildDir "$Config\DittoRenderSmoke.exe"
if (!(Test-Path $testsExe)) {
    throw "Test executable not found: $testsExe"
}
if (!(Test-Path $renderExe)) {
    throw "Render smoke executable not found: $renderExe"
}

$renderOut = Join-Path $BuildDir "TestOutput\RenderSmoke"
$builtFlow = Join-Path $PSScriptRoot "RunBuiltTests.ps1"
& $builtFlow -TestsExe $testsExe -RenderExe $renderExe -RenderOut $renderOut
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

param(
    [string]$BuildDir = "build-tests",
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"

function Normalize-ProcessPathEnvironment {
    $pathEntries = [System.Environment]::GetEnvironmentVariables("Process").GetEnumerator() |
        Where-Object { $_.Key -ieq "Path" }

    if ($pathEntries.Count -le 1) {
        return
    }

    $pathValue = $env:Path
    if ([string]::IsNullOrEmpty($pathValue)) {
        $pathValue = ($pathEntries | Select-Object -First 1).Value
    }

    foreach ($entry in $pathEntries) {
        if ($entry.Key -cne "Path") {
            [System.Environment]::SetEnvironmentVariable($entry.Key, $null, "Process")
        }
    }

    [System.Environment]::SetEnvironmentVariable("Path", $pathValue, "Process")
    $env:Path = $pathValue
}

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

Normalize-ProcessPathEnvironment

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
$cmakeCache = Join-Path $BuildDir "CMakeCache.txt"
$runVulkan = $false
if (Test-Path $cmakeCache) {
    $runVulkan = Select-String -Path $cmakeCache -Pattern "^Vulkan_LIBRARY:FILEPATH=.+vulkan-1\.lib$" -Quiet
}
& $builtFlow -TestsExe $testsExe -RenderExe $renderExe -RenderOut $renderOut -RunVulkan:$runVulkan
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

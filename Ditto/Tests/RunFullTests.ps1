param(
    [string]$BuildDir = "x64",
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

function Resolve-MSBuild {
    $candidates = @(
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
    )
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path $candidate)) {
            return $candidate
        }
    }

    $cmd = Get-Command msbuild -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    throw "MSBuild not found. Install Visual Studio 2022 or run from a Developer PowerShell."
}

Normalize-ProcessPathEnvironment
$msbuild = Resolve-MSBuild

Write-Host "[DittoTests] Build via Visual Studio"
Invoke-Native $msbuild Ditto.sln /p:Configuration=$Config /p:Platform=x64 /t:DittoTests /m:1 /v:minimal /nologo
Invoke-Native $msbuild Ditto.sln /p:Configuration=$Config /p:Platform=x64 /t:DittoRenderSmoke /m:1 /v:minimal /nologo

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
$vulkanSdk = if ($env:VULKAN_SDK) { $env:VULKAN_SDK } else { "C:\VulkanSDK\1.4.350.0" }
$runVulkan = (Test-Path (Join-Path $vulkanSdk "Include\vulkan\vulkan.h")) -and
             (Test-Path (Join-Path $vulkanSdk "Lib\vulkan-1.lib"))
& $builtFlow -TestsExe $testsExe -RenderExe $renderExe -RenderOut $renderOut -RunVulkan:$runVulkan
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

# Build the local AMD launcher without changing an installed game or publishing a release.
[CmdletBinding()]
param(
    [string]$FfxPath,
    [switch]$SkipTranslator
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    if (-not (Get-Command uv -ErrorAction SilentlyContinue)) { throw 'Install uv before building.' }
    if (-not $FfxPath) { $FfxPath = Join-Path $projectRoot 'build\deps\amd_fidelityfx_vk.dll' }
    if (-not (Test-Path -LiteralPath $FfxPath -PathType Leaf)) {
        throw 'FSR library missing. Pass -FfxPath with the SDK 1.1.4 amd_fidelityfx_vk.dll path; see docs/performance-dials.md.'
    }
    $FfxPath = (Resolve-Path -LiteralPath $FfxPath).Path
    # Prefer the compiler tools supplied by MSYS2; otherwise add the installed Vulkan SDK.
    if (-not (Test-Path 'C:\msys64\ucrt64\bin\glslc.exe') -and
        -not (Get-Command glslc -ErrorAction SilentlyContinue)) {
        $sdkBin = if ($env:VULKAN_SDK) { Join-Path $env:VULKAN_SDK 'Bin' }
        if (-not $sdkBin -or -not (Test-Path (Join-Path $sdkBin 'glslc.exe'))) {
            $sdk = Get-ChildItem C:\VulkanSDK -Directory -ErrorAction SilentlyContinue |
                Where-Object { Test-Path (Join-Path $_.FullName 'Bin\glslc.exe') } |
                Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
            $sdkBin = if ($sdk) { Join-Path $sdk.FullName 'Bin' }
        }
        if (-not $sdkBin) { throw 'Install the Vulkan SDK or MSYS2 shaderc and spirv-tools first.' }
        $env:PATH = "$sdkBin;$env:PATH"
    }
    if (-not $SkipTranslator) {
        & uv run --no-project --python 3.14 tools\build_xlate.py
        if ($LASTEXITCODE -ne 0) { throw 'Shader translator build failed.' }
    }
    & uv run --no-project --python 3.14 tools\build_shaders.py
    if ($LASTEXITCODE -ne 0) { throw 'Runtime shader build failed.' }
    $localVersion = (Get-Content VERSION -Raw).Trim() + '-fastnavi.1'
    & uv run --no-project --python 3.14 --with numpy --with truststore tools\make_launcher.py `
        --xlate build\xlate --ffx $FfxPath --no-dlss --feed jmmar/fastnavi --version $localVersion `
        --out build\launcher-release-updated
    if ($LASTEXITCODE -ne 0) { throw 'Launcher packaging failed.' }
    Write-Host 'Launcher and setup: build\launcher-release-updated'
} finally {
    Pop-Location
}

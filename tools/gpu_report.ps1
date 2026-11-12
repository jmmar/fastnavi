# Query the Vulkan driver without loading game content or submitting rendering work.
[CmdletBinding()]
param(
    [string]$VulkanInfo = 'vulkaninfo.exe',
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) '../build/gpu-report'
}
$tool = Get-Command $VulkanInfo -CommandType Application -ErrorAction SilentlyContinue
if (-not $tool) {
    throw 'vulkaninfo.exe was not found. Install Vulkan Tools or pass -VulkanInfo with its full path.'
}
$directory = New-Item -ItemType Directory -Force -Path $OutputDirectory
$rawPath = Join-Path $directory.FullName 'vulkaninfo.txt'
$errorPath = Join-Path $directory.FullName 'vulkaninfo-errors.txt'
# Windows PowerShell treats native stderr as error records. Preserve warnings without aborting a successful query.
$ErrorActionPreference = 'Continue'
try {
    & $tool.Source --text 2> $errorPath | Out-File -LiteralPath $rawPath -Encoding UTF8
    $code = $LASTEXITCODE
} finally {
    $ErrorActionPreference = 'Stop'
}
$listing = Get-Content -LiteralPath $rawPath -Raw
if ($code -ne 0) { throw "vulkaninfo exited with $code. See vulkaninfo-errors.txt in $($directory.FullName)." }

# Runtime startup requirements: runtime/vk_device.c, k_required_ext and create_device.
$requiredFeatures = @(
    'timelineSemaphore', 'dynamicRendering', 'synchronization2',
    'shaderStorageImageWriteWithoutFormat', 'depthClipControl', 'shaderInt64',
    'samplerAnisotropy', 'independentBlend', 'geometryShader', 'textureCompressionBC'
)
$requiredExtensions = @(
    'VK_KHR_swapchain', 'VK_KHR_push_descriptor',
    'VK_EXT_depth_clip_control', 'VK_EXT_external_memory_host'
)
$extraFeatures = @('bufferDeviceAddress', 'meshShader', 'taskShader')
$devices = @()
$blocks = [regex]::Matches($listing, '(?ms)^GPU(\d+):\s*\r?\n(.*?)(?=^GPU\d+:\s*\r?\n|\z)')
foreach ($block in $blocks) {
    $body = $block.Groups[2].Value
    $properties = [ordered]@{}
    foreach ($name in @('deviceName', 'apiVersion', 'driverVersion', 'driverName', 'driverInfo')) {
        $match = [regex]::Match($body, '(?m)^\s*' + $name + '\s*=\s*(.+)$')
        $properties[$name] = if ($match.Success) { $match.Groups[1].Value.Trim() } else { $null }
    }
    $features = [ordered]@{}
    foreach ($name in ($requiredFeatures + $extraFeatures)) {
        $match = [regex]::Match($body, '(?m)^\s*' + $name + '\s*=\s*(true|false)\b')
        $features[$name] = if ($match.Success) { $match.Groups[1].Value -eq 'true' } else { $null }
    }
    $extensions = [ordered]@{}
    foreach ($name in ($requiredExtensions + @('VK_EXT_mesh_shader'))) {
        $extensions[$name] = [regex]::IsMatch($body, '(?m)^\s*' + $name + '\s*:')
    }
    $missing = @($requiredFeatures | Where-Object { $features[$_] -ne $true })
    $missing += @($requiredExtensions | Where-Object { -not $extensions[$_] })
    $devices += [pscustomobject]@{
        index = [int]$block.Groups[1].Value
        properties = $properties
        features = $features
        extensions = $extensions
        missingOrUnreportedStartupRequirements = $missing
        # Some drivers omit feature structs for unsupported extensions; null means unreported, not supported.
        meshDrawingAvailable = ($extensions['VK_EXT_mesh_shader'] -and
            $features['meshShader'] -eq $true -and $features['taskShader'] -eq $true)
    }
}
if (-not $devices.Count) { throw 'No GPU blocks recognized. Keep the raw report for inspection.' }
$report = [ordered]@{
    collected = (Get-Date).ToString('o')
    tool = $tool.Source
    scope = 'Driver capabilities only; no game execution, benchmark or rendering validation.'
    devices = $devices
}
$path = Join-Path $directory.FullName 'capabilities.json'
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $path -Encoding UTF8
foreach ($device in $devices) {
    Write-Output "$($device.properties['deviceName']):"
    Write-Output "  Missing/unreported startup requirements: $($device.missingOrUnreportedStartupRequirements -join ', ')"
    Write-Output "  Mesh drawing available: $($device.meshDrawingAvailable)"
}
Write-Output "Report: $path"

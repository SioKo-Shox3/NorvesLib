param([Parameter(Mandatory = $true)][string]$RepoRoot)

$ErrorActionPreference = 'Stop'
$modelRunner = Join-Path $RepoRoot 'Scripts/RunCookedModelGameProfile.ps1'
$textureRunner = Join-Path $RepoRoot 'Scripts/RunCookedTextureGameSmoke.ps1'
$helper = Join-Path $RepoRoot 'Scripts/CookedModelGameProfileContract.ps1'

foreach ($path in @($modelRunner, $textureRunner, $helper)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing Phase 5-U script: $path"
    }
    [void][scriptblock]::Create([System.IO.File]::ReadAllText($path))
}

function Assert-HandleCachedExitCode {
    param([int]$ExpectedExitCode)

    $temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("NorvesLibPhase5U-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $temporaryRoot | Out-Null
    try {
        $process = Start-Process -FilePath $env:ComSpec -ArgumentList '/c', "exit $ExpectedExitCode" `
            -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $temporaryRoot 'stdout.txt') `
            -RedirectStandardError (Join-Path $temporaryRoot 'stderr.txt')
        $null = $process.Handle
        $process.WaitForExit()
        if ($process.ExitCode -ne $ExpectedExitCode) {
            throw "PS5.1 handle-cached process exit mismatch: expected $ExpectedExitCode, got $($process.ExitCode)"
        }
    }
    finally {
        Remove-Item -LiteralPath $temporaryRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Assert-HandleCachedExitCode 0
Assert-HandleCachedExitCode 3

$runnerSource = [System.IO.File]::ReadAllText($modelRunner)
if ($runnerSource.Contains('$process.Refresh()')) {
    throw 'Model runner must not call stale Process.Refresh()'
}
$startPositions = @()
$searchStart = 0
while (($position = $runnerSource.IndexOf('Start-Process', $searchStart, [System.StringComparison]::Ordinal)) -ge 0) {
    $startPositions += $position
    $searchStart = $position + 1
}
if ($startPositions.Count -ne 2) {
    throw "Expected exactly two model-runner Start-Process sites, got $($startPositions.Count)"
}
foreach ($startPosition in $startPositions) {
    $waitPosition = $runnerSource.IndexOf('$process.WaitForExit', $startPosition, [System.StringComparison]::Ordinal)
    $handlePosition = $runnerSource.IndexOf('$null = $process.Handle', $startPosition, [System.StringComparison]::Ordinal)
    if ($waitPosition -lt 0 -or $handlePosition -lt 0 -or $handlePosition -gt $waitPosition) {
        throw 'Model runner must cache Process.Handle before its first WaitForExit'
    }
}

$ErrorActionPreference = 'Continue'
$oldOutput = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $modelRunner -AssetCookExe missing -GameExe missing -FrameCount 3 2>&1
$oldExitCode = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($oldExitCode -eq 0 -or ($oldOutput -join "`n") -notmatch 'FrameCount') {
    throw 'Legacy -FrameCount binding must be rejected'
}
$ErrorActionPreference = 'Continue'
$newOutput = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $modelRunner -AssetCookExe missing -GameExe missing -RenderedFrameCount 3 2>&1
$newExitCode = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($newExitCode -eq 0 -or ($newOutput -join "`n") -notmatch 'AssetCookExe not found') {
    throw 'RenderedFrameCount must reach AssetCookExe validation'
}

. $helper
if (-not (Test-IsAbsolutePath 'C:\\temp') -or -not (Test-IsAbsolutePath '\\\\server\\share')) {
    throw 'Drive and UNC absolute paths must be accepted'
}
foreach ($path in @('C:relative', '\root-relative')) {
    if (Test-IsAbsolutePath $path) {
        throw "Non-absolute path accepted: $path"
    }
}

function Assert-Throws {
    param([scriptblock]$Action, [string]$Name)

    try {
        & $Action
    }
    catch {
        return
    }
    throw "Expected rejection: $Name"
}

$runtimeRoot = 'C:\RuntimeRoot'
$allowedDefaults = @(
    'Textures/Silver/silver_albedo.png',
    'Textures/Silver/silver_normal-ogl.png',
    'Textures/Silver/silver_metallic.png',
    'Textures/Silver/silver_roughness.png',
    'Textures/Silver/silver_ao.png',
    'Textures/CobbleStoneFloor/cobblestone_floor_09_diff_4k.png',
    'Textures/CobbleStoneFloor/cobblestone_floor_09_nor_gl_4k.png',
    'Textures/CobbleStoneFloor/cobblestone_floor_09_rough_4k.png',
    'Textures/CobbleStoneFloor/cobblestone_floor_09_ao_4k.png',
    'Textures/CobbleStoneFloor/cobblestone_floor_09_disp_4k.png'
)
$readyOne = 'stage=asset_gpu_flush_window_ready role=render_thread window_id=1 frames_rendered=4 success=1'
$resumedOne = 'stage=asset_gpu_flush_window_resumed role=render_thread window_id=1 ready_frames=4 frames_rendered=5 success=1'
$readyTwo = 'stage=asset_gpu_flush_window_ready role=render_thread window_id=2 frames_rendered=8 success=1'
$resumedTwo = 'stage=asset_gpu_flush_window_resumed role=render_thread window_id=2 ready_frames=8 frames_rendered=10 success=1'
Assert-AssetGpuFlushWindows -Lines @($readyOne, $resumedOne, $readyTwo, $resumedTwo) -RunName 'valid'
foreach ($case in @(
        @(),
        @('stage=asset_gpu_flush_window_ready role=render_thread window_id=1 success=1'),
        @('stage=asset_gpu_flush_window_ready role=render_thread window_id=1 frames_rendered=1 success=1 extra=1'),
        @('stage=asset_gpu_flush_window_ready role=render_thread window_id=18446744073709551616 frames_rendered=1 success=1'),
        @($readyOne, $readyOne, $resumedOne),
        @($readyOne, $resumedOne, $resumedOne),
        @($readyOne, $resumedOne, $readyOne),
        @($resumedOne),
        @($readyOne, 'stage=asset_gpu_flush_window_resumed role=render_thread window_id=1 ready_frames=3 frames_rendered=5 success=1'),
        @($readyOne, 'stage=asset_gpu_flush_window_resumed role=render_thread window_id=1 ready_frames=4 frames_rendered=4 success=1'),
        @($readyOne)
    )) {
    Assert-Throws { Assert-AssetGpuFlushWindows -Lines $case -RunName 'invalid' } 'window fixture'
}

$allowedError = "[2026-07-17 17:48:01.921] [ERROR] [T:62468] [TextureResources] [TextureAssetRuntime.cpp:NorvesLib::Core::Rendering::TextureAssetRuntime::FlushCompletedTextureLoads:851] Async texture load failed: $runtimeRoot\Textures\CobbleStoneFloor\cobblestone_floor_09_diff_4k.png"
Assert-ProfileRuntimeSignals -Lines @($readyOne, $resumedOne, $allowedError, 'stage=sample success=0') -RuntimeRoot $runtimeRoot -ExpectedDefaultTexturePaths $allowedDefaults
Assert-ProfileRuntimeSignals -Lines @($readyOne, $resumedOne, '[2026-07-17 17:48:01.921] [ERROR] [T:62468] [SlangCompiler] [VulkanSlangCompiler.cpp:NorvesLib::RHI::Vulkan::VulkanSlangCompiler::CompileFromSource:215] Cannot compile [neural_material_decode.slang]: Slang SDK not available. Rebuild with NORVES_HAS_SLANG to enable.') -RuntimeRoot $runtimeRoot -ExpectedDefaultTexturePaths $allowedDefaults
Assert-ProfileRuntimeSignals -Lines @($readyOne, $resumedOne, '[2026-07-17 17:48:01.921] [ERROR] [T:62468] [ShaderManager] [ShaderManager.cpp:NorvesLib::Core::Rendering::ShaderManager::LoadShader:97] Failed to compile shader [neural_material_decode.slang]: Slang SDK not available. Rebuild with NORVES_HAS_SLANG to enable.') -RuntimeRoot $runtimeRoot -ExpectedDefaultTexturePaths $allowedDefaults
Assert-Throws { Assert-ProfileRuntimeSignals -Lines @($readyOne, $resumedOne, 'Assertion failed: fixture') -RuntimeRoot $runtimeRoot -ExpectedDefaultTexturePaths $allowedDefaults } 'Assertion failed blocker'
Assert-Throws { Assert-ProfileRuntimeSignals -Lines @($readyOne, $resumedOne, "[ERROR] [Unexpected] Async texture load failed: $runtimeRoot\Textures\CobbleStoneFloor\cobblestone_floor_09_diff_4k.png") -RuntimeRoot $runtimeRoot -ExpectedDefaultTexturePaths $allowedDefaults } 'unexpected texture logger shape'
Assert-Throws { Assert-ProfileRuntimeSignals -Lines @($readyOne, $resumedOne, "[ERROR] [Unexpected] [ERROR] [T:62468] [TextureResources] [TextureAssetRuntime.cpp:NorvesLib::Core::Rendering::TextureAssetRuntime::FlushCompletedTextureLoads:851] Async texture load failed: $runtimeRoot\Textures\CobbleStoneFloor\cobblestone_floor_09_diff_4k.png") -RuntimeRoot $runtimeRoot -ExpectedDefaultTexturePaths $allowedDefaults } 'prefixed unexpected texture logger record'
Assert-Throws { Assert-ProfileRuntimeSignals -Lines @($readyOne, $resumedOne, '[ERROR] [Unexpected] [ERROR] [T:62468] [SlangCompiler] [VulkanSlangCompiler.cpp:NorvesLib::RHI::Vulkan::VulkanSlangCompiler::CompileFromSource:215] Cannot compile [neural_material_decode.slang]: Slang SDK not available. Rebuild with NORVES_HAS_SLANG to enable.') -RuntimeRoot $runtimeRoot -ExpectedDefaultTexturePaths $allowedDefaults } 'prefixed unexpected Slang logger record'
Assert-Throws { Assert-AssetGpuFlushWindows -Lines @('stage=asset_gpu_flush_window_ready role=render_thread window_id=1 success=0 stage=asset_gpu_flush_window_resumed role=render_thread window_id=1 ready_frames=1 frames_rendered=2 success=1') -RunName 'multiple' } 'multiple stage markers'

$textureRunnerSource = [System.IO.File]::ReadAllText($textureRunner)
if ($textureRunnerSource -notmatch 'RedirectStandardOutput' -or
    $textureRunnerSource -notmatch 'RedirectStandardError' -or
    $textureRunnerSource -notmatch '\$null = \$process\.Handle' -or
    $textureRunnerSource -notmatch 'runtimeLines' -or
    $textureRunnerSource -notmatch 'Assert-ProfileRuntimeSignals -Lines \$runtimeLines' -or
    $textureRunnerSource -notmatch '\[string\]\$CaptureDirectory' -or
    $textureRunnerSource -notmatch '-CaptureDirectory \$ResolvedSmokeDir') {
    throw 'Texture runner must capture and validate Game.log/stdout/stderr'
}
foreach ($line in @(
        '[SKIP] fixture',
        'VUID-vkFixture',
        'validation failure',
        'device lost',
        'UploadFailed fixture',
        'assert fixture',
        '[ERROR] [TextureResources] Async texture load failed: C:\OtherRoot\Textures\CobbleStoneFloor\cobblestone_floor_09_diff_4k.png',
        "[ERROR] [OtherCategory] Async texture load failed: $runtimeRoot\Textures\CobbleStoneFloor\cobblestone_floor_09_diff_4k.png",
        "[ERROR] [TextureResources] Other message: $runtimeRoot\Textures\CobbleStoneFloor\cobblestone_floor_09_diff_4k.png",
        "[ERROR] [TextureResources] Async texture load failed: $runtimeRoot\Textures\Unknown\missing.png"
        '[2026-07-17 17:48:01.921] [ERROR] [T:62468] [SlangCompiler] [VulkanSlangCompiler.cpp:NorvesLib::RHI::Vulkan::VulkanSlangCompiler::CompileFromSource:215] Cannot compile [neural_material_decode.slang]: Slang SDK not available. Rebuild with another flag.'
    )) {
    Assert-Throws { Assert-ProfileRuntimeSignals -Lines @($readyOne, $resumedOne, $line) -RuntimeRoot $runtimeRoot -ExpectedDefaultTexturePaths $allowedDefaults } 'runtime blocker fixture'
}

Write-Host 'CookedModelGameProfileScriptContractTest passed'

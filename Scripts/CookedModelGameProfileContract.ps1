Set-StrictMode -Version Latest

$script:InvariantCulture = [System.Globalization.CultureInfo]::InvariantCulture

function Test-IsAbsolutePath {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return $false
    }
    $trimmed = $Path.Trim()
    return $trimmed -match '^[A-Za-z]:[\\/]' -or $trimmed -match '^[\\/]{2}'
}

function ConvertFrom-ExactUInt64 {
    param([string]$Text, [string]$Name)

    if ([string]::IsNullOrWhiteSpace($Text) -or $Text -notmatch '^[0-9]+$') {
        throw "$Name must be an ASCII unsigned decimal"
    }
    $value = [uint64]0
    if (-not [uint64]::TryParse($Text, [System.Globalization.NumberStyles]::None, $script:InvariantCulture, [ref]$value)) {
        throw "$Name overflows UInt64"
    }
    return $value
}

function Assert-AssetGpuFlushWindows {
    param(
        [string[]]$Lines,
        [string]$RunName
    )

    $readyPattern = '^.*stage=asset_gpu_flush_window_ready role=render_thread window_id=(?<id>[0-9]+) frames_rendered=(?<frames>[0-9]+) success=1$'
    $resumedPattern = '^.*stage=asset_gpu_flush_window_resumed role=render_thread window_id=(?<id>[0-9]+) ready_frames=(?<ready>[0-9]+) frames_rendered=(?<frames>[0-9]+) success=1$'
    $open = @{}
    $readyFramesById = @{}
    $resumedIds = @{}
    $windowCount = 0
    foreach ($line in $Lines) {
        if ([regex]::Matches($line, 'stage=asset_gpu_flush_window_(ready|resumed)').Count -gt 1) {
            throw "Multiple asset GPU flush window markers: $line"
        }
        $ready = [regex]::Match($line, $readyPattern)
        $resumed = [regex]::Match($line, $resumedPattern)
        if ($line -match 'stage=asset_gpu_flush_window_(ready|resumed)' -and -not $ready.Success -and -not $resumed.Success) {
            throw "Malformed asset GPU flush window marker: $line"
        }
        if ($ready.Success) {
            $id = ConvertFrom-ExactUInt64 $ready.Groups['id'].Value 'window_id'
            [void](ConvertFrom-ExactUInt64 $ready.Groups['frames'].Value 'frames_rendered')
            if ($open.ContainsKey($id) -or $readyFramesById.ContainsKey($id)) {
                throw "Duplicate or unbalanced asset GPU flush ready window: $id"
            }
            $open[$id] = $true
            $readyFramesById[$id] = ConvertFrom-ExactUInt64 $ready.Groups['frames'].Value 'frames_rendered'
            ++$windowCount
        }
        if ($resumed.Success) {
            $id = ConvertFrom-ExactUInt64 $resumed.Groups['id'].Value 'window_id'
            $readyFrames = ConvertFrom-ExactUInt64 $resumed.Groups['ready'].Value 'ready_frames'
            $frames = ConvertFrom-ExactUInt64 $resumed.Groups['frames'].Value 'frames_rendered'
            if (-not $open.ContainsKey($id) -or $resumedIds.ContainsKey($id)) {
                throw "Orphan asset GPU flush resume window: $id"
            }
            if ($readyFrames -ne $readyFramesById[$id]) {
                throw "Asset GPU flush resume ready_frames mismatch: $id"
            }
            if ($frames -le $readyFrames) {
                throw "Asset GPU flush resume did not advance rendered frames: $id"
            }
            [void]$open.Remove($id)
            $resumedIds[$id] = $true
        }
    }
    if ($windowCount -eq 0) {
        throw "Game $RunName did not emit an asset GPU flush window"
    }
    if ($open.Count -ne 0) {
        throw 'Trailing asset GPU flush window without resume'
    }
}

function Assert-ProfileRuntimeSignals {
    param(
        [string[]]$Lines,
        [string]$RuntimeRoot,
        [string[]]$ExpectedDefaultTexturePaths
    )

    $expectedDefaultPaths = @{}
    foreach ($relativePath in $ExpectedDefaultTexturePaths) {
        $expectedPath = [System.IO.Path]::GetFullPath((Join-Path $RuntimeRoot $relativePath))
        $expectedDefaultPaths[$expectedPath] = $true
    }

    foreach ($line in $Lines) {
        if ($line -match '(?i)\[SKIP\]|\bVUID\b|validation|device[ -]lost|UploadFailed|\b(assert|assertion|abort)\b') {
            throw "Blocked runtime signal: $line"
        }
        if ($line -match '\[ERROR\]') {
            $textureError = [regex]::Match($line, '^\[[^\]\r\n]+\] \[ERROR\] \[T:[0-9]+\] \[TextureResources\] \[TextureAssetRuntime\.cpp:NorvesLib::Core::Rendering::TextureAssetRuntime::FlushCompletedTextureLoads:851\] Async texture load failed: (?<path>.+)$')
            if ($textureError.Success) {
                $candidatePath = [System.IO.Path]::GetFullPath($textureError.Groups['path'].Value.Trim())
                if ($expectedDefaultPaths.ContainsKey($candidatePath)) {
                    continue
                }
            }

            $approvedNeuralPatterns = @(
                '^\[[^\]\r\n]+\] \[ERROR\] \[T:[0-9]+\] \[SlangCompiler\] \[VulkanSlangCompiler\.cpp:NorvesLib::RHI::Vulkan::VulkanSlangCompiler::CompileFromSource:215\] Cannot compile \[neural_material_decode\.slang\]: Slang SDK not available\. Rebuild with NORVES_HAS_SLANG to enable\.$',
                '^\[[^\]\r\n]+\] \[ERROR\] \[T:[0-9]+\] \[ShaderManager\] \[ShaderManager\.cpp:NorvesLib::Core::Rendering::ShaderManager::LoadShader:97\] Failed to compile shader \[neural_material_decode\.slang\]: Slang SDK not available\. Rebuild with NORVES_HAS_SLANG to enable\.$'
            )
            if (@($approvedNeuralPatterns | Where-Object { $line -match $_ }).Count -eq 1) {
                continue
            }
            throw "Blocked runtime error: $line"
        }
    }
    Assert-AssetGpuFlushWindows -Lines $Lines -RunName 'profile'
}

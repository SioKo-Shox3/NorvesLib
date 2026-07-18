Set-StrictMode -Version Latest

function Assert-ProfileRuntimeSignals {
    param(
        [string[]]$Lines
    )

    $approvedPatterns = @(
        '^\[[^\]\r\n]+\] \[ERROR\] \[T:[0-9]+\] \[TextureResources\] \[TextureAssetRuntime\.cpp:NorvesLib::Core::Rendering::TextureAssetRuntime::FlushCompletedTextureLoads:851\] Async texture load failed: C:\\RuntimeRoot\\Textures\\CobbleStoneFloor\\cobblestone_floor_09_diff_4k\.png$',
        '^\[[^\]\r\n]+\] \[ERROR\] \[T:[0-9]+\] \[SlangCompiler\] \[VulkanSlangCompiler\.cpp:NorvesLib::RHI::Vulkan::VulkanSlangCompiler::CompileFromSource:215\] Cannot compile \[neural_material_decode\.slang\]: Slang SDK not available\. Rebuild with NORVES_HAS_SLANG to enable\.$',
        '^\[[^\]\r\n]+\] \[ERROR\] \[T:[0-9]+\] \[ShaderManager\] \[ShaderManager\.cpp:NorvesLib::Core::Rendering::ShaderManager::LoadShader:97\] Failed to compile shader \[neural_material_decode\.slang\]: Slang SDK not available\. Rebuild with NORVES_HAS_SLANG to enable\.$'
    )

    foreach ($line in $Lines) {
        if ($line -match '\[ERROR\]') {
            if (@($approvedPatterns | Where-Object { $line -match $_ }).Count -eq 1) {
                continue
            }
            throw "Blocked runtime error: $line"
        }
    }
}

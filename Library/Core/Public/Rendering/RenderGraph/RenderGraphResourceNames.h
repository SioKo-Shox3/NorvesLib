#pragma once

#include "Text/IdentityPool.h"

namespace NorvesLib::Core::Rendering::RenderGraphResourceNames
{
    inline constexpr Identity GBufferAlbedo = Identity::Literal("GBuffer.Albedo", sizeof("GBuffer.Albedo") - 1);
    inline constexpr Identity GBufferNormal = Identity::Literal("GBuffer.Normal", sizeof("GBuffer.Normal") - 1);
    inline constexpr Identity GBufferMaterial = Identity::Literal("GBuffer.Material", sizeof("GBuffer.Material") - 1);
    inline constexpr Identity GBufferEmissive = Identity::Literal("GBuffer.Emissive", sizeof("GBuffer.Emissive") - 1);
    inline constexpr Identity GBufferDepth = Identity::Literal("GBuffer.Depth", sizeof("GBuffer.Depth") - 1);
    inline constexpr Identity SSAORaw = Identity::Literal("SSAO.Raw", sizeof("SSAO.Raw") - 1);
    inline constexpr Identity SSAOBlurred = Identity::Literal("SSAO.Blurred", sizeof("SSAO.Blurred") - 1);
    inline constexpr Identity SceneColor = Identity::Literal("Scene.Color", sizeof("Scene.Color") - 1);
    inline constexpr Identity SceneDepth = Identity::Literal("Scene.Depth", sizeof("Scene.Depth") - 1);
    inline constexpr Identity SSRSceneColor = Identity::Literal("SSR.SceneColor", sizeof("SSR.SceneColor") - 1);
    inline constexpr Identity BloomSceneColor = Identity::Literal("Bloom.SceneColor", sizeof("Bloom.SceneColor") - 1);
    inline constexpr Identity ToneMappedColor = Identity::Literal("ToneMappedColor", sizeof("ToneMappedColor") - 1);
    inline constexpr Identity PresentationColor = Identity::Literal("PresentationColor", sizeof("PresentationColor") - 1);
} // namespace NorvesLib::Core::Rendering::RenderGraphResourceNames

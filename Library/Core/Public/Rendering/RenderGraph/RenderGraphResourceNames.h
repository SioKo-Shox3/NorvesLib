#pragma once

#include "Text/IdentityPool.h"

namespace NorvesLib::Core::Rendering::RenderGraphResourceNames
{
    inline constexpr Identity GBufferAlbedo = Identity::Literal("GBuffer.Albedo", sizeof("GBuffer.Albedo") - 1);
    inline constexpr Identity GBufferNormal = Identity::Literal("GBuffer.Normal", sizeof("GBuffer.Normal") - 1);
    inline constexpr Identity GBufferMaterial = Identity::Literal("GBuffer.Material", sizeof("GBuffer.Material") - 1);
    inline constexpr Identity GBufferEmissive = Identity::Literal("GBuffer.Emissive", sizeof("GBuffer.Emissive") - 1);
    inline constexpr Identity GBufferVelocity = Identity::Literal("GBuffer.Velocity", sizeof("GBuffer.Velocity") - 1);
    inline constexpr Identity GBufferDepth = Identity::Literal("GBuffer.Depth", sizeof("GBuffer.Depth") - 1);
    inline constexpr Identity ShadowMap = Identity::Literal("ShadowMap", sizeof("ShadowMap") - 1);
    inline constexpr Identity SkyAtmosphereTransmittance =
        Identity::Literal("SkyAtmosphere.Transmittance", sizeof("SkyAtmosphere.Transmittance") - 1);
    inline constexpr Identity SkyAtmosphereRadiance =
        Identity::Literal("SkyAtmosphere.Radiance", sizeof("SkyAtmosphere.Radiance") - 1);
    inline constexpr Identity SkyAtmosphereSunDisk =
        Identity::Literal("SkyAtmosphere.SunDisk", sizeof("SkyAtmosphere.SunDisk") - 1);
    inline constexpr Identity SSAORaw = Identity::Literal("SSAO.Raw", sizeof("SSAO.Raw") - 1);
    inline constexpr Identity SSAOBlurred = Identity::Literal("SSAO.Blurred", sizeof("SSAO.Blurred") - 1);
    inline constexpr Identity SceneColor = Identity::Literal("Scene.Color", sizeof("Scene.Color") - 1);
    inline constexpr Identity SceneDepth = Identity::Literal("Scene.Depth", sizeof("Scene.Depth") - 1);
    inline constexpr Identity RTGIDiffuseIndirect =
        Identity::Literal("RTGI.DiffuseIndirect", sizeof("RTGI.DiffuseIndirect") - 1);
    inline constexpr Identity RTGIHistoryCurrent =
        Identity::Literal("RTGI.History.Current", sizeof("RTGI.History.Current") - 1);
    inline constexpr Identity RTGIHistoryHistory =
        Identity::Literal("RTGI.History.History", sizeof("RTGI.History.History") - 1);
    inline constexpr Identity RTGIHistoryCurrentAge =
        Identity::Literal("RTGI.History.CurrentAge", sizeof("RTGI.History.CurrentAge") - 1);
    inline constexpr Identity RTGIHistoryHistoryAge =
        Identity::Literal("RTGI.History.HistoryAge", sizeof("RTGI.History.HistoryAge") - 1);
    inline constexpr Identity RTGIHistoryCurrentConfidence = Identity::Literal(
        "RTGI.History.CurrentConfidence", sizeof("RTGI.History.CurrentConfidence") - 1);
    inline constexpr Identity RTGIHistoryHistoryConfidence = Identity::Literal(
        "RTGI.History.HistoryConfidence", sizeof("RTGI.History.HistoryConfidence") - 1);
    inline constexpr Identity RTGIHistoryCurrentDepth = Identity::Literal(
        "RTGI.History.CurrentDepth", sizeof("RTGI.History.CurrentDepth") - 1);
    inline constexpr Identity RTGIHistoryHistoryDepth = Identity::Literal(
        "RTGI.History.HistoryDepth", sizeof("RTGI.History.HistoryDepth") - 1);
    inline constexpr Identity RTGIHistoryCurrentNormal = Identity::Literal(
        "RTGI.History.CurrentNormal", sizeof("RTGI.History.CurrentNormal") - 1);
    inline constexpr Identity RTGIHistoryHistoryNormal = Identity::Literal(
        "RTGI.History.HistoryNormal", sizeof("RTGI.History.HistoryNormal") - 1);
    inline constexpr Identity RTGIHistoryCurrentMaterial = Identity::Literal(
        "RTGI.History.CurrentMaterial", sizeof("RTGI.History.CurrentMaterial") - 1);
    inline constexpr Identity RTGIHistoryHistoryMaterial = Identity::Literal(
        "RTGI.History.HistoryMaterial", sizeof("RTGI.History.HistoryMaterial") - 1);
    inline constexpr Identity SSRSceneColor = Identity::Literal("SSR.SceneColor", sizeof("SSR.SceneColor") - 1);
    inline constexpr Identity BloomSceneColor = Identity::Literal("Bloom.SceneColor", sizeof("Bloom.SceneColor") - 1);
    inline constexpr Identity ToneMappedColor = Identity::Literal("ToneMappedColor", sizeof("ToneMappedColor") - 1);
    inline constexpr Identity PresentationColor = Identity::Literal("PresentationColor", sizeof("PresentationColor") - 1);
    inline constexpr Identity CanvasColor = Identity::Literal("Canvas.Color", sizeof("Canvas.Color") - 1);
    inline constexpr Identity CompositeColor = Identity::Literal("Composite.Color", sizeof("Composite.Color") - 1);
} // namespace NorvesLib::Core::Rendering::RenderGraphResourceNames

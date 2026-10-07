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
    // 計算シェーダーでスキニングした今・前のフレームの頂点（ワールド空間。1 頂点 32 バイト）
    inline constexpr Identity SkinningCurrentVertices =
        Identity::Literal("Skinning.CurrentVertices", sizeof("Skinning.CurrentVertices") - 1);
    inline constexpr Identity SkinningPreviousVertices =
        Identity::Literal("Skinning.PreviousVertices", sizeof("Skinning.PreviousVertices") - 1);
    // ビジビリティバッファ: 画素ごとの ID（R32_UINT。深度は GBuffer.Depth を共有）と、フレームごとの描画の記録の表
    inline constexpr Identity VisBufferId = Identity::Literal("VisBuffer.Id", sizeof("VisBuffer.Id") - 1);
    inline constexpr Identity VisBufferDrawRecords =
        Identity::Literal("VisBuffer.DrawRecords", sizeof("VisBuffer.DrawRecords") - 1);
    // 材質の解決の前段（MaterialTileClassifyPass）が作る、材質ごとのタイルの一覧と間接 dispatch の引数
    inline constexpr Identity MaterialTileArgs =
        Identity::Literal("MaterialTile.Args", sizeof("MaterialTile.Args") - 1);
    inline constexpr Identity MaterialTileList =
        Identity::Literal("MaterialTile.List", sizeof("MaterialTile.List") - 1);
    inline constexpr Identity MaterialTileCursors =
        Identity::Literal("MaterialTile.Cursors", sizeof("MaterialTile.Cursors") - 1);
    inline constexpr Identity MaterialTileStats =
        Identity::Literal("MaterialTile.Stats", sizeof("MaterialTile.Stats") - 1);
    inline constexpr Identity ShadowMap = Identity::Literal("ShadowMap", sizeof("ShadowMap") - 1);
    // 太陽の仮想シャドウマップ（--shadow-method=vsm）の資源。物理ページのプール・ページの表・今フレームの要求・空きページの一覧・統計
    inline constexpr Identity VsmPhysicalPool = Identity::Literal("VSM.PhysicalPool", sizeof("VSM.PhysicalPool") - 1);
    inline constexpr Identity VsmPageTable = Identity::Literal("VSM.PageTable", sizeof("VSM.PageTable") - 1);
    inline constexpr Identity VsmRequestBits = Identity::Literal("VSM.RequestBits", sizeof("VSM.RequestBits") - 1);
    inline constexpr Identity VsmFreeList = Identity::Literal("VSM.FreeList", sizeof("VSM.FreeList") - 1);
    inline constexpr Identity VsmStats = Identity::Literal("VSM.Stats", sizeof("VSM.Stats") - 1);
    inline constexpr Identity PointShadowCubeMap =
        Identity::Literal("PointShadowCubeMap", sizeof("PointShadowCubeMap") - 1);
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
    inline constexpr Identity LightingIndirectSpecular =
        Identity::Literal("Lighting.IndirectSpecular", sizeof("Lighting.IndirectSpecular") - 1);
    inline constexpr Identity LightingSpecularReflectance =
        Identity::Literal("Lighting.SpecularReflectance", sizeof("Lighting.SpecularReflectance") - 1);
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

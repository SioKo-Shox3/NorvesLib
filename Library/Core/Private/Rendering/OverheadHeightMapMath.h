// 真上からの雨の遮蔽に使う窓、変換、遮蔽物の CPU 計算をまとめる。
#pragma once

#include "Container/Span.h"
#include "Container/VariableArray.h"
#include "Math/Matrix4x4.h"
#include "Rendering/SkyOcclusion.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    struct OverheadHeightMapSettings
    {
        uint32_t Resolution = 1024u;
        float CoverageMeters = 96.0f;
        float TopAboveCameraMeters = 128.0f;
        float TopSnapMeters = 16.0f;
        float RangeMeters = 512.0f;
        uint32_t RedrawThresholdTexels = 8u;
        double MinRedrawIntervalSeconds = 0.25;
        uint32_t MaxOccluders = 16384u;
    };

    struct OverheadHeightMapWindow
    {
        int32_t OriginTexelX = 0;
        int32_t OriginTexelZ = 0;
        float TexelMeters = 0.09375f;
        float CoverageMeters = 96.0f;
        uint32_t Resolution = 1024u;
        float TopY = 0.0f;
        float RangeMeters = 512.0f;

        float GetOriginX() const;
        float GetOriginZ() const;
    };

    // 設定の寸法は有限な正数、カメラのテクセル位置は int32_t の範囲内とする。
    OverheadHeightMapWindow ComputeOverheadHeightMapWindow(
        const OverheadHeightMapSettings& settings, float cameraX, float cameraY, float cameraZ);
    float OverheadDepthFromHeight(const OverheadHeightMapWindow& window, float y);
    float OverheadRoofYFromDepth(const OverheadHeightMapWindow& window, float depth);
    bool WorldToOverheadTexel(
        const OverheadHeightMapWindow& window, float x, float z, uint32_t& i, uint32_t& j);
    void OverheadTexelCenterToWorld(
        const OverheadHeightMapWindow& window, uint32_t i, uint32_t j, float& x, float& z);
    bool SampleOverheadRoofY(
        const OverheadHeightMapWindow& window, Container::Span<const float> depths,
        float x, float z, float& outRoofY);
    Math::Matrix4x4 BuildOverheadHeightMapViewProjection(const OverheadHeightMapWindow& window);

    // GLSL の std140 と列優先 mat4 にそのまま渡す。
    struct alignas(16) OverheadHeightMapGpuParams
    {
        float ViewProjection[16] = {};
        float Window[4] = {};
        float Depth[4] = {};
        uint32_t Counts[4] = {};
    };

    static_assert(alignof(OverheadHeightMapGpuParams) == 16u);
    static_assert(sizeof(OverheadHeightMapGpuParams) == 112u);
    static_assert(offsetof(OverheadHeightMapGpuParams, ViewProjection) == 0u);
    static_assert(offsetof(OverheadHeightMapGpuParams, Window) == 64u);
    static_assert(offsetof(OverheadHeightMapGpuParams, Depth) == 80u);
    static_assert(offsetof(OverheadHeightMapGpuParams, Counts) == 96u);

    OverheadHeightMapGpuParams MakeOverheadHeightMapGpuParams(
        const OverheadHeightMapWindow& window, uint32_t boxCount, uint32_t ellipsoidCount);

    struct OverheadHeightMapRedrawState
    {
        bool bHasContent = false;
        OverheadHeightMapWindow Window;
        double LastRedrawSeconds = 0.0;
        uint64_t OccluderHash = 0u;
        uint32_t OccluderCount = 0u;
    };

    struct OverheadHeightMapRedrawDecision
    {
        bool bRedraw = false;
        OverheadHeightMapWindow Window;
    };

    OverheadHeightMapRedrawDecision DecideOverheadHeightMapRedraw(
        const OverheadHeightMapSettings& settings, const OverheadHeightMapRedrawState& state,
        const OverheadHeightMapWindow& candidate, double nowSeconds, bool bOccludersChanged);

    bool MakeOverheadBoxFromLocalBounds(
        const float localMin[3], const float localMax[3],
        const Math::Matrix4x4& worldRowVector, SkyOccluderProxy& out);
    bool MakeOverheadEllipsoidFromSphere(
        float cx, float cy, float cz, float radius, SkyOccluderProxy& out);

    // derived は有効で予約欄がゼロの遮蔽物とし、箱と楕円体の各群では入力順を保つ。
    uint32_t CollectOverheadOccluders(
        const OverheadHeightMapSettings& settings, const OverheadHeightMapWindow& window,
        Container::Span<const SkyOccluderProxy> skyOccluders,
        Container::Span<const SkyOccluderProxy> derived,
        Container::VariableArray<SkyOccluderProxy>& out, bool& outTruncated);
    uint64_t HashOverheadOccluders(Container::Span<const SkyOccluderProxy> occluders);

    // 箱の上面、または 8 方位・4 環の三角形ドームの高さを返す。
    bool EvaluateOverheadRoofTopY(
        const SkyOccluderProxy& proxy, float x, float z, float& outTopY);
} // namespace NorvesLib::Core::Rendering

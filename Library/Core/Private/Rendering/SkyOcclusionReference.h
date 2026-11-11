// 空の遮蔽を CPU で再現する格子と積分を表す。
#pragma once

#include "Container/Span.h"
#include "Container/VariableArray.h"
#include "Math/Vector3.h"
#include "Rendering/SkyOcclusion.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief GPU の積分にも同じ方位数、仰角区間数、歩数上限、距離を使う。
     *
     * 歩数上限は MaxHorizonMeters の有効な最大値 4096 m を含む。
     */
    inline constexpr std::uint32_t SkyOcclusionAzimuthCount = 12u;
    inline constexpr std::uint32_t SkyOcclusionElevationCount = 6u;
    inline constexpr std::uint32_t SkyOcclusionHorizonMaxStepCount = 28u;
    inline constexpr float SkyOcclusionHorizonFirstStepMeters = 0.5f;
    inline constexpr float SkyOcclusionHorizonStepRatio = 1.4f;

    struct SkyOcclusionReferenceColumn
    {
        float Ground;
        float CanopyBottom;
        float CanopyTop;
        float CanopyOpacity;
    };

    struct SkyOcclusionReferenceColumnRef
    {
        float& Ground;
        float& CanopyBottom;
        float& CanopyTop;
        float& CanopyOpacity;
    };

    /**
     * @brief Y を上とし、XZ 平面に並ぶ 1 段の 2.5D 格子。
     *
     * 原点は最小の角、距離はメートル。解像度とテクセル幅は正とする。
     * At の添字は格子内、Sample の座標は有限とする。
     */
    class SkyOcclusionReferenceGrid
    {
    public:
        SkyOcclusionReferenceGrid(
            float originX, float originZ, float texelMeters, std::uint32_t resolution);

        SkyOcclusionReferenceColumnRef At(std::uint32_t ix, std::uint32_t iz);
        SkyOcclusionReferenceColumn At(std::uint32_t ix, std::uint32_t iz) const;
        SkyOcclusionReferenceColumn Sample(float worldX, float worldZ) const;

        const float OriginX;
        const float OriginZ;
        const float TexelMeters;
        const std::uint32_t Resolution;

    private:
        Container::VariableArray<float> m_Ground;
        Container::VariableArray<float> m_CanopyBottom;
        Container::VariableArray<float> m_CanopyTop;
        Container::VariableArray<float> m_CanopyOpacity;
    };

    /** @brief テクセル中心に遮蔽物を写し、外側では端が続く格子を作る。 */
    SkyOcclusionReferenceGrid BuildSkyOcclusionReferenceGrid(
        const SkyOcclusionParameters& parameters,
        Container::Span<const SkyOccluderProxy> occluders,
        float originX, float originZ, float texelMeters, std::uint32_t resolution);

    /** @brief 上半球の遮蔽率を定数成分と 1 次成分で表す。 */
    struct SkyOcclusionL1
    {
        float B0 = 0.0f;
        float Cx = 0.0f;
        float Cy = 0.0f;
        float Cz = 0.0f;
    };

    /**
     * @brief 上半球を等立体角で標本し、格子の地面と樹冠を積分する。
     *
     * 設定は SanitizeSkyOcclusionParameters が定める有効範囲を前提とする。
     */
    SkyOcclusionL1 IntegrateSkyOcclusion(
        const SkyOcclusionReferenceGrid& grid, const SkyOcclusionParameters& parameters,
        float worldX, float worldZ);

    /**
     * @brief 距離と窓の包含で段を選び、クリップマップの地平線を積分する。
     *
     * levels は細かい順の空でない格子列で、各窓はワールドテクセルに整列している。
     * receiverLevel は levels の範囲内とし、格子のポインタはすべて有効とする。
     */
    SkyOcclusionL1 IntegrateSkyOcclusionClipmap(
        Container::Span<const SkyOcclusionReferenceGrid* const> levels,
        std::uint32_t receiverLevel, const SkyOcclusionParameters& parameters,
        float worldX, float worldZ);

    /** @brief 単位法線に対する L1 のコサイン畳み込みを返す。 */
    float EvaluateSkyOcclusionB(const SkyOcclusionL1& coefficients, float nx, float ny, float nz);
    float EvaluateSkyOcclusionVisibility(
        const SkyOcclusionL1& coefficients, float nx, float ny, float nz);
    Math::Vector3 EvaluateSkyOcclusionBentNormal(const SkyOcclusionL1& coefficients);

    inline constexpr std::uint32_t SkySpecularOcclusionLobeSampleCount = 13u;

    /**
     * @brief 可視円錐内の GGX 鏡面ローブの割合を求める。
     *
     * Jimenez らの一様球冠は GGX の裾を表せず、粗さ 0.6 で約 0.13 ずれた。
     * 輪と円錐の重なりを閉形式で求め、D の逆関数で得た角を G1 で重み付けする。
     * GLSL にも同じ層数 SkySpecularOcclusionLobeSampleCount と式を使う。
     */
    float EvaluateSkySpecularOcclusionCone(
        const Math::Vector3& visibleAxis, float visibility,
        const Math::Vector3& reflection, float roughness);

    /** @brief L1 から曲げた法線と可視円錐を作り、鏡面可視率を返す。 */
    float EvaluateSkySpecularOcclusion(
        const SkyOcclusionL1& coefficients, const Math::Vector3& reflection, float roughness);
} // namespace NorvesLib::Core::Rendering

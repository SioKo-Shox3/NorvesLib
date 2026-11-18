// 散布の層で使うインポスターの登録情報・配置・影の受け渡しを定義する。
#pragma once

#include "Rendering/RenderTypes.h"
#include "Rendering/ImpostorMath.h"
#include "Rendering/ImpostorBake.h"
#include "Rendering/MeshTypes.h"
#include "Math/Vector3.h"
#include "Math/Matrix4x4.h"
#include "Math/MatrixUtils.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace NorvesLib::Core::Rendering
{
    /// MegaMesh の焼きと切り替え条件。テクスチャは登録側が所有し、解放前に登録を外す。
    struct MegaMeshImpostorDesc
    {
        TextureHandle AlbedoAtlas = TextureHandle::Invalid(); // 色と被覆（A）。必須
        TextureHandle NormalAtlas = TextureHandle::Invalid(); // 無効なら視線へ向く法線
        TextureHandle OrmAtlas = TextureHandle::Invalid();    // 無効なら既定の材質
        TextureHandle DepthAtlas = TextureHandle::Invalid();  // 無効なら板の面の深度
        ImpostorOctahedralLayout Layout = ImpostorOctahedralLayout::HemiOctahedral;
        uint32_t CellResolution = 0;
        uint32_t AxisCellCountX = 0;
        uint32_t AxisCellCountY = 0;
        uint32_t AtlasWidth = 0;
        uint32_t AtlasHeight = 0;
        Math::Vector3 BoundsCenter; // オブジェクト空間の焼きの境界球の中心
        float BoundsRadius = 0.0f;  // オブジェクト空間の焼きの境界球の半径
        float SwitchScreenSize = 0.0f; // この値より小さい個体をインポスターにする
        bool bCastShadow = true; // インポスターの段で太陽の影を落とすか
        float ShadowDistance = 0.0f; // カメラからの距離の上限（メートル）。0 は制限なし

        /// 必須アトラス、寸法、切り替え条件と影の距離が有効かを返す。
        bool IsValid() const
        {
            return AlbedoAtlas.IsValid() && CellResolution > 0 &&
                   AxisCellCountX > 0 && AxisCellCountY > 0 &&
                   AtlasWidth > 0 && AtlasHeight > 0 &&
                   std::isfinite(BoundsRadius) && BoundsRadius > 0.0f &&
                   std::isfinite(SwitchScreenSize) && SwitchScreenSize > 0.0f &&
                   std::isfinite(ShadowDistance) && ShadowDistance >= 0.0f;
        }
    };

    /// 焼きの結果から記述を作る。テクスチャの所有権は移さない。
    inline MegaMeshImpostorDesc MakeMegaMeshImpostorDesc(const ImpostorBakeResult& result, float switchScreenSize)
    {
        MegaMeshImpostorDesc desc;
        desc.AlbedoAtlas = result.AtlasTexture;
        desc.NormalAtlas = result.NormalAtlasTexture;
        desc.OrmAtlas = result.OrmAtlasTexture;
        desc.DepthAtlas = result.DepthAtlasTexture;
        desc.Layout = result.Metadata.Layout;
        desc.CellResolution = result.Metadata.CellResolution;
        desc.AxisCellCountX = result.Metadata.AxisCellCountX;
        desc.AxisCellCountY = result.Metadata.AxisCellCountY;
        desc.AtlasWidth = result.Metadata.AtlasWidth;
        desc.AtlasHeight = result.Metadata.AtlasHeight;
        desc.BoundsCenter = result.Metadata.BoundsCenter;
        desc.BoundsRadius = result.Metadata.BoundsRadius;
        desc.SwitchScreenSize = switchScreenSize;
        return desc;
    }

    /// 層の個体を分ける XZ 格子のセルの一辺（メートル）。
    constexpr float LAYER_IMPOSTOR_CELL_SIZE_METERS = 64.0f;

    /// floor(位置 / セルの一辺) で得た X・Z の int32 セル座標を上位・下位 32 ビットに詰める。
    /// 非有限位置は原点のセル、int32 の範囲外は表現可能な端のセルとする。
    inline uint64_t MakeLayerImpostorCellKey(float worldX, float worldZ)
    {
        if (!std::isfinite(worldX) || !std::isfinite(worldZ))
        {
            return 0;
        }

        // 表現できないセル座標は端に収め、浮動小数点から整数への範囲外変換を避ける。
        const auto cellCoordinate = [](float position) -> int32_t
        {
            const double cell = std::floor(static_cast<double>(position) /
                                           static_cast<double>(LAYER_IMPOSTOR_CELL_SIZE_METERS));
            if (cell <= static_cast<double>(std::numeric_limits<int32_t>::min()))
            {
                return std::numeric_limits<int32_t>::min();
            }
            if (cell >= static_cast<double>(std::numeric_limits<int32_t>::max()))
            {
                return std::numeric_limits<int32_t>::max();
            }
            return static_cast<int32_t>(cell);
        };
        const uint32_t x = static_cast<uint32_t>(cellCoordinate(worldX));
        const uint32_t z = static_cast<uint32_t>(cellCoordinate(worldZ));
        return (static_cast<uint64_t>(x) << 32) | static_cast<uint64_t>(z);
    }

    /// セル全体がメッシュ、インポスター、または個体ごとの判定を要する帯。
    enum class LayerImpostorBand : uint8_t
    {
        Mesh = 0,
        Impostor = 1,
        Mixed = 2
    };

    /// 境界球を含むセル AABB までの最短・最長距離と、個体の半径の最小・最大から帯を求める。
    /// 下限がしきい値 × (1 + hysteresis) 以上なら Mesh、上限がしきい値 × (1 - hysteresis) 未満なら Impostor。
    /// それ以外、または入力が非有限・範囲が不正なら Mixed。カメラがセル内なら上限を最大値とする。
    inline LayerImpostorBand ClassifyLayerImpostorCell(float minDistance, float maxDistance,
                                                       float minRadius, float maxRadius,
                                                       float switchScreenSize, bool bOrthographic,
                                                       float tanHalfFovY, float orthoHeight,
                                                       float hysteresis = DEFAULT_LOD_HYSTERESIS)
    {
        if (!std::isfinite(minDistance) || !std::isfinite(maxDistance) ||
            !std::isfinite(minRadius) || !std::isfinite(maxRadius) ||
            !std::isfinite(switchScreenSize) || !std::isfinite(tanHalfFovY) ||
            !std::isfinite(orthoHeight) || !std::isfinite(hysteresis) ||
            minDistance < 0.0f || maxDistance < minDistance ||
            minRadius < 0.0f || maxRadius < minRadius || switchScreenSize <= 0.0f ||
            (bOrthographic ? orthoHeight <= 0.0f : tanHalfFovY <= 0.0f))
        {
            return LayerImpostorBand::Mixed;
        }

        // LOD の段の選択と同じ幅に収める。
        hysteresis = std::clamp(hysteresis, 0.0f, 0.5f);
        const float minScreenSize = bOrthographic
            ? ComputeOrthographicScreenSize(minRadius, orthoHeight)
            : ComputePerspectiveScreenSize(minRadius, maxDistance, tanHalfFovY);
        const float maxScreenSize = minDistance == 0.0f
            ? std::numeric_limits<float>::max()
            : (bOrthographic ? ComputeOrthographicScreenSize(maxRadius, orthoHeight)
                             : ComputePerspectiveScreenSize(maxRadius, minDistance, tanHalfFovY));

        if (minScreenSize >= switchScreenSize * (1.0f + hysteresis))
        {
            return LayerImpostorBand::Mesh;
        }
        if (maxScreenSize < switchScreenSize * (1.0f - hysteresis))
        {
            return LayerImpostorBand::Impostor;
        }
        return LayerImpostorBand::Mixed;
    }

    /// 描画と影で共有する、焼きの境界球から求めた板の中心・大きさ・軸。
    struct LayerImpostorPlacement
    {
        Math::Vector3 Center;
        float HalfSize = 0.0f;
        Math::Vector3 AxisX;
        Math::Vector3 AxisY;
        Math::Vector3 AxisZ;
    };

    /// 行ベクトル規約のワールド行列から配置を作る。無効な記述や拡大では false を返す。
    inline bool ComputeLayerImpostorPlacement(const Math::Matrix4x4& world, const MegaMeshImpostorDesc& desc,
                                               LayerImpostorPlacement& outPlacement)
    {
        if (!desc.IsValid())
        {
            return false;
        }

        const auto isFinite = [](const Math::Vector3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        };
        const Math::Vector3 center = Math::MatrixUtils::TransformPointRowVector(world, desc.BoundsCenter);
        const Math::Vector3 axisX = Math::MatrixUtils::TransformVectorRowVector(world, Math::Vector3::UnitX);
        const Math::Vector3 axisY = Math::MatrixUtils::TransformVectorRowVector(world, Math::Vector3::UnitY);
        const Math::Vector3 axisZ = Math::MatrixUtils::TransformVectorRowVector(world, Math::Vector3::UnitZ);
        if (!isFinite(center) || !isFinite(axisX) || !isFinite(axisY) || !isFinite(axisZ))
        {
            return false;
        }

        Math::Vector3 scale = Math::MatrixUtils::ExtractScale(world);
        // 二乗が桁あふれ・桁落ちする尺度では、成分から安定した長さを求め直す。
        const auto safeScale = [](float extracted, const Math::Vector3& axis)
        {
            if (!std::isfinite(extracted) || extracted < std::sqrt(std::numeric_limits<float>::min()))
            {
                return std::hypot(axis.x, axis.y, axis.z);
            }
            return extracted;
        };
        scale.x = safeScale(scale.x, axisX);
        scale.y = safeScale(scale.y, axisY);
        scale.z = safeScale(scale.z, axisZ);
        if (!isFinite(scale) || scale.x <= 0.0f || scale.y <= 0.0f || scale.z <= 0.0f)
        {
            return false;
        }

        const float halfSize = desc.BoundsRadius * std::max({scale.x, scale.y, scale.z});
        if (!std::isfinite(halfSize) || halfSize <= 0.0f)
        {
            return false;
        }
        LayerImpostorPlacement placement;
        placement.Center = center;
        placement.HalfSize = halfSize;
        placement.AxisX = Math::Vector3(axisX.x / scale.x, axisX.y / scale.x, axisX.z / scale.x);
        placement.AxisY = Math::Vector3(axisY.x / scale.y, axisY.y / scale.y, axisY.z / scale.y);
        placement.AxisZ = Math::Vector3(axisZ.x / scale.z, axisZ.y / scale.z, axisZ.z / scale.z);
        outPlacement = placement;
        return true;
    }

    /// 太陽の影に使うインポスターのアトラス。
    struct ImpostorShadowAtlas
    {
        TextureHandle AlbedoAtlas = TextureHandle::Invalid(); // 被覆は A
        ImpostorOctahedralLayout Layout = ImpostorOctahedralLayout::HemiOctahedral;
        uint32_t AxisCellCountX = 0;
        uint32_t AxisCellCountY = 0;
        uint32_t CellResolution = 0;
        uint32_t AtlasWidth = 0;
        uint32_t AtlasHeight = 0;
    };

    /// 太陽の影へ落とす個体。中心・大きさ・軸は LayerImpostorPlacement と同じ値。
    struct ImpostorShadowCaster
    {
        uint64_t Key = 0; // 散布の proxy の ObjectId
        float Center[3] = {};
        float HalfSize = 0.0f;
        float AxisX[3] = {1.0f, 0.0f, 0.0f};
        float AxisY[3] = {0.0f, 1.0f, 0.0f};
        float AxisZ[3] = {0.0f, 0.0f, 1.0f};
        uint32_t AtlasIndex = 0; // FramePacket::ImpostorShadowAtlases の番号
    };
} // namespace NorvesLib::Core::Rendering

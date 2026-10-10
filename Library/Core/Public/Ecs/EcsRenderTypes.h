#pragma once

/**
 * @file EcsRenderTypes.h
 * @brief Mesh・MegaGeometry・SkinnedMesh・Light 系・Board の Component の行に置く、描画の値の列
 *
 * 値は Object の参照でないもの（資源の handle と bool・enum）だけ。Component の行が World の木にいる間は列が正で、
 * 行が無い間は Component の予備の値が正になる。
 */

#include "Component/LightIntensityUnit.h"
#include "Ecs/EcsTypes.h"
#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"
#include "Rendering/MaterialTypes.h"
#include "Rendering/MegaGeometry/MegaGeometryTypes.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/SkinnedMeshTypes.h"

#include <cstdint>

namespace NorvesLib::Core::Ecs
{
    /// MeshComponent の行の描画の値（プロパティ。ECS_PROPERTY で読み書きする）
    struct EcsMeshRender
    {
        Rendering::MeshDataHandle MeshHandle;
        bool bVisible;
        bool bCastShadow;
        bool bReceiveShadow;
        bool Reserved;
        Rendering::RenderLayer Layer;
    };

    /// MegaGeometryComponent の行の MegaMesh の handle（プロパティ。ECS_PROPERTY で読み書きする）
    struct EcsMegaMesh
    {
        Rendering::MegaGeometry::MegaMeshHandle Handle;
    };

    /// SkinnedMeshComponent の行の描画の値。メッシュの handle・可視・影を落とすはプロパティ（ECS_PROPERTY で読み書きする）。
    /// 材質の handle はスロットごとの上書きと一緒に扱うので、プロパティにはせず Component が読み書きする
    struct EcsSkinnedRender
    {
        Rendering::SkinnedMeshHandle MeshHandle;
        Rendering::MaterialHandle Material;
        bool bVisible;
        bool bCastShadow;
        bool Reserved0;
        bool Reserved1;
    };

    /// LightComponent（とその派生）の行の共通の値（プロパティ。ECS_PROPERTY で読み書きする）
    struct EcsLightRender
    {
        float Intensity;
        float ShadowBias;
        uint32_t ShadowMapResolution;
        Math::Vector3 Color;
        Rendering::LightType Type;
        Component::LightIntensityUnit Unit;
        bool bCastShadows;
        bool bVisible;
        Rendering::RenderLayer AffectedLayers;
        uint8_t Reserved0;
        uint8_t Reserved1;
        uint8_t Reserved2;
    };

    /// PointLightComponent・SpotLightComponent の行の影響範囲と減衰（プロパティ。ECS_PROPERTY で読み書きする）
    struct EcsLightFalloff
    {
        float Range;
        float AttenuationConstant;
        float AttenuationLinear;
        float AttenuationQuadratic;
    };

    /// SpotLightComponent の行のコーン角（度。プロパティ。ECS_PROPERTY で読み書きする）
    struct EcsSpotCone
    {
        float InnerConeAngle;
        float OuterConeAngle;
    };

    /// BoardComponent（とその派生）の行の描画の値（プロパティ。ECS_PROPERTY で読み書きする）。
    /// フリップブックの設定（コマ数・速度・アトラス）は描画の値ではないので列に置かない
    struct EcsBoardRender
    {
        Rendering::TextureHandle Texture;
        Math::Vector4 Tint;
        Math::Vector4 UVRect;
        Math::Vector2 Pivot;
        Math::Vector2 SizePx;
        uint32_t LayerPriority;
        uint32_t OrderInLayer;
        bool bVisible;
        bool bFlipX;
        bool bFlipY;
        Rendering::RenderLayer Layer;
        Rendering::BoardSpace Space;
        Rendering::BlendMode Blend;
        uint8_t Reserved0;
        uint8_t Reserved1;
    };
}

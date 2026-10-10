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

    /// LightComponent（とその派生）の行の共通の値。Direction 以外はプロパティ（ECS_PROPERTY で読み書きする）。
    /// 方向は保存の対象外なので、プロパティにはせず Component が列の一部として読み書きする
    struct EcsLightRender
    {
        Math::Vector3 Direction;
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

    /// BillboardComponent（とその派生）の行の、ワールド単位の大きさ（プロパティ。ECS_PROPERTY で読み書きする）
    struct EcsBillboardSize
    {
        Math::Vector2 SizeWorld;
    };

    /// 描画へ出す行（Mesh・MegaGeometry・SkinnedMesh・Light 系・Board）の公開の状態。World だけが書く。
    /// 望む状態（可視・有効）と比べて、公開から非公開へ変わった行の Remove を出すのに使う。
    /// 公開したときに読んだ版も持ち、今の版と違えば「変わった行」になる
    struct EcsPublishState
    {
        /// 公開したときの描画の変換の版（Entity::GetRenderTransformVersion）
        uint64_t RenderTransformVersion;
        /// 公開したときのポーズの版（EcsPoseVersion::Serial。SkinnedMesh 以外は 0）
        uint64_t PoseSerial;
        /// proxy を作り直して渡した回数
        uint32_t Version;
        /// SceneView（または画面空間の sink）に proxy を出している
        bool bPublished;
        uint8_t Reserved0;
        uint8_t Reserved1;
        uint8_t Reserved2;
    };

    /// SkinnedMeshComponent の行のポーズの版。ポーズを求め直すたびに Component が書く（World は公開した版と比べる）
    struct EcsPoseVersion
    {
        uint64_t Serial;
    };
}

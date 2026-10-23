#include "GameModes/InteriorLight/InteriorLightMode.h"

#include "Core/Public/Component/CameraComponent.h"
#include "Core/Public/Component/MegaGeometryComponent.h"
#include "Core/Public/Component/MeshComponent.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/GameMode/GameModeScope.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Math/Quaternion.h"
#include "Core/Public/Math/Vector3.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/RHI/ITexture.h"
#include "Core/Public/Rendering/MegaGeometry/CookedMeshMegaMeshAdapter.h"
#include "Core/Public/Rendering/MegaGeometry/CookedMeshPageSource.h"
#include "Core/Public/Rendering/ProceduralMeshGenerator.h"
#include "Core/Public/Rendering/RenderResources.h"
#include "Core/Public/Rendering/RenderWorld.h"
#include "Core/Public/Rendering/SkyAtmosphere.h"
#include "Core/Public/Rendering/VolumetricFog.h"
#include "GameModes/Rendering3DTest/SkySunControl.h"
#include "Scenes/SceneCatalog.h"
#include "Scenes/SceneReadiness.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Container;
using namespace NorvesLib::Core::GameMode;
using namespace NorvesLib::Core::Rendering;
using namespace NorvesLib::Core::Rendering::MegaGeometry;

namespace Game::GameModes
{

    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kDegreesToRadians = kPi / 180.0f;

        // 低い夕日: 高さ 15°・方位 240°（窓の壁の法線（西、-Z）から南（-X）へ 30°）。空の方位は +X から +Z へ測るので、
        // 太陽への水平の向きは (cos 240°, sin 240°) = (-0.5, -0.87)。InteriorLightReference.md の「窓の向き」「太陽の高さ」
        constexpr float kSunAltitudeDegrees = 15.0f;
        constexpr float kSunAzimuthDegrees = 240.0f;
        // 自動露出の補正[EV]
        constexpr float kExposureCompensationEV = 0.5f;
        // SceneReady を出すまでの最小の Tick の数
        constexpr uint32_t kReadyTickCount = 3;

        // 他のシーンのメッシュの ID（100〜、200〜、900〜942、材質ギャラリーの 1000〜1144）と重ならない値
        constexpr uint32_t kPlasterMeshHandleId = 1200;
        constexpr uint32_t kFloorMeshHandleId = 1201;
        constexpr uint32_t kBrickMeshHandleId = 1202;
        constexpr uint32_t kTimberMeshHandleId = 1203;
        constexpr uint32_t kGroundMeshHandleId = 1204;

        // ---- 部屋（m）。x は窓の壁に沿って北へ、z は窓の壁から東へ、y は上へ ----
        constexpr float kRoomSizeX = 12.0f;
        constexpr float kRoomSizeZ = 8.0f;
        constexpr float kRoomHeight = 5.0f;
        // 壁・床・天井の厚さ（外の光が角から漏れないよう、窓の壁と奥の壁は南北の壁の外まで延ばす）
        constexpr float kWallThickness = 0.3f;
        // 窓: 幅 2.0 m・高さ 3.2 m・窓台 0.45 m。中心は x = 2.5・6.0・9.5 m
        constexpr float kWindowCentersX[] = {2.5f, 6.0f, 9.5f};
        constexpr float kWindowHalfWidth = 1.0f;
        constexpr float kWindowSillHeight = 0.45f;
        constexpr float kWindowTopHeight = 3.65f;
        // 桟: 縦 2 列 × 横 4 段のガラスに分ける。断面 7 cm、壁の厚さの中ほど（z -0.2〜-0.1）
        constexpr uint32_t kWindowPaneRows = 4;
        constexpr float kMullionHalfWidth = 0.035f;
        constexpr float kMullionMinZ = -0.2f;
        constexpr float kMullionMaxZ = -0.1f;
        // 天井の梁: 断面 0.25（x）× 0.35（y）m、z の向きに渡し、x に 2 m ごと
        constexpr uint32_t kBeamCount = 5;
        constexpr float kBeamPitchX = 2.0f;
        constexpr float kBeamHalfWidth = 0.125f;
        constexpr float kBeamDepth = 0.35f;
        // 窓の外の地面: 部屋を中心に 600 m 四方。床の板の上の面（y 0）より 2 cm 下
        constexpr float kGroundSize = 600.0f;
        constexpr float kGroundHeight = -0.02f;

        /// 部屋の材質 1 つ（Poly Haven の資産 id とテクスチャの使い方）
        struct RoomMaterialSpec
        {
            const char *AssetId;
            /// テクスチャ 1 枚の実寸[m]（Poly Haven の dimensions）
            float TileMeters;
            /// 単位のアルベド（Scripts/MakeInteriorLightAlbedo.py）を張るか。偽なら無地で、色は ObjectColor だけ
            bool bUnitAlbedo;
            /// ORM を張るか。偽なら粗さは Roughness の定数
            bool bOrm;
            float Roughness;
            /// 物体の色（GBuffer がアルベドに掛け、RTGI の光線の当たりの反射率にもする線形の色）。単位のアルベドを張るときは
            /// Scripts/MakeInteriorLightAlbedo.py の倍率 k（線形のアルベドのチャンネルごとの 99 百分位）
            float ObjectColor[3];
        };
        // 漆喰: 拡散の反射率 約 0.7 の温かい白（InteriorLightReference.md の「部屋の大きさ」）。beige_wall_001 の色の画像は暗い
        // ベージュ（線形の平均 0.27）なので張らず、法線と ORM（粗さ 0.62）で漆喰の凹凸だけを出す
        constexpr RoomMaterialSpec kPlasterSpec{"beige_wall_001", 3.0f, false, true, 0.0f, {0.74f, 0.69f, 0.60f}};
        // 樫の板張り: 艶のある塗装の粗さ 0.3（手本 0.25〜0.35。wood_floor の粗さの画像は 0.47 で一様なので使わない）
        constexpr RoomMaterialSpec kFloorSpec{"wood_floor", 1.7f, true, false, 0.3f, {0.3185f, 0.1746f, 0.0823f}};
        constexpr RoomMaterialSpec kBrickSpec{"red_brick", 1.4f, true, true, 0.0f, {0.4452f, 0.3095f, 0.2423f}};
        // 天井の梁と窓の桟
        constexpr RoomMaterialSpec kTimberSpec{"dark_wood", 2.0f, true, true, 0.0f, {0.2423f, 0.0666f, 0.0203f}};
        constexpr RoomMaterialSpec kGroundSpec{"forrest_ground_01", 2.0f, true, true, 0.0f, {0.6514f, 0.5333f, 0.3712f}};

        /// 家具 1 種（生成した GLB。テクスチャは GLB に内蔵の img1 が ORM・img2 が法線で、アルベドは単位のアルベドの PNG）
        struct FurnitureKindSpec
        {
            const char *Id;
            /// 材質の BaseColor（GBuffer がアルベドに掛け、RTGI の光線の当たりの反射率にもする）。Scripts/MakeInteriorLightAlbedo.py の k
            float AlbedoScale[3];
        };
        constexpr FurnitureKindSpec kFurnitureKinds[] = {
            {"wooden_table", {0.3372f, 0.1812f, 0.0742f}},
            {"wooden_chair", {0.4397f, 0.2051f, 0.0742f}},
            {"bookshelf", {0.5210f, 0.3968f, 0.2423f}},
            {"sofa", {0.3325f, 0.3467f, 0.3613f}},
            {"potted_plant", {0.6240f, 0.3763f, 0.1529f}},
            {"floor_lamp", {0.9911f, 0.9911f, 1.0000f}},
            {"rug", {0.9911f, 0.5906f, 0.3864f}},
        };
        constexpr uint32_t kFurnitureKindCount =
            static_cast<uint32_t>(sizeof(kFurnitureKinds) / sizeof(kFurnitureKinds[0]));

        /// 家具 1 つの置き場所（床の上の中心と向き。向き 0 は模型の正面（+Z）を部屋の奥（東）へ向ける）
        struct FurniturePlacement
        {
            uint32_t KindIndex;
            float X;
            float Z;
            float YawDegrees;
        };
        // InteriorLightReference.md の「家具の並び」。ラグは中央の窓の日だまり、机と東の椅子は南の窓の日だまりに入る
        constexpr FurniturePlacement kFurniturePlacements[] = {
            // ラグ（3.5 × 2.4 m）: x 6.0〜9.5・z 2.2〜4.7
            {6, 7.75f, 3.45f, 0.0f},
            // ソファ: x 7.8〜9.6・z 5.6〜6.5、窓を向く
            {3, 8.7f, 6.05f, 180.0f},
            // 消灯のフロアランプ: ソファの北の脇
            {5, 10.3f, 6.3f, 0.0f},
            // 机（1.9 × 0.9 m）: x 2.0〜3.8・z 2.5〜3.4。椅子は机の西と東から机を向く
            {0, 2.9f, 2.95f, 0.0f},
            {1, 2.9f, 2.33f, 0.0f},
            {1, 2.9f, 3.57f, 180.0f},
            // 本棚: 奥の壁（z 8）に背を付け、日の当たらない奥の南の隅に部屋へ向けて置く
            {2, 1.2f, 7.68f, 180.0f},
            // 植木鉢: 北の窓と北の端の壁の間
            {4, 11.2f, 1.2f, 0.0f},
        };

        struct Vec3
        {
            float X;
            float Y;
            float Z;
        };

        Vec3 Add(const Vec3 &a, const Vec3 &b)
        {
            return {a.X + b.X, a.Y + b.Y, a.Z + b.Z};
        }

        Vec3 Scale(const Vec3 &a, float s)
        {
            return {a.X * s, a.Y * s, a.Z * s};
        }

        float Dot(const Vec3 &a, const Vec3 &b)
        {
            return a.X * b.X + a.Y * b.Y + a.Z * b.Z;
        }

        Vec3 Cross(const Vec3 &a, const Vec3 &b)
        {
            return {a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X};
        }

        /// 四角形を 1 枚足す。center を中心に axisU 方向へ ±halfU、axisV 方向へ ±halfV、法線は normal。
        /// UV はワールドの位置を uvU・uvV へ投影して tileMeters で割った値（隣り合う箱の面で模様がつながる）。
        /// 三角形の巡回は材質ギャラリーの板と同じ（2 辺の外積が法線と逆向き）にそろえる
        void AppendWorldQuad(VariableArray<Mesh3DVertex> &vertices, VariableArray<uint32_t> &indices, const Vec3 &center,
                             const Vec3 &axisU, const Vec3 &axisV, const Vec3 &normal, float halfU, float halfV,
                             const Vec3 &uvU, const Vec3 &uvV, float tileMeters)
        {
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            const float signs[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {-1.0f, 1.0f}, {1.0f, 1.0f}};
            Vec3 corners[4];
            for (uint32_t index = 0; index < 4; ++index)
            {
                corners[index] = Add(center, Add(Scale(axisU, signs[index][0] * halfU), Scale(axisV, signs[index][1] * halfV)));
                Mesh3DVertex vertex{};
                vertex.Position[0] = corners[index].X;
                vertex.Position[1] = corners[index].Y;
                vertex.Position[2] = corners[index].Z;
                vertex.Normal[0] = normal.X;
                vertex.Normal[1] = normal.Y;
                vertex.Normal[2] = normal.Z;
                vertex.TexCoord[0] = Dot(corners[index], uvU) / tileMeters;
                vertex.TexCoord[1] = Dot(corners[index], uvV) / tileMeters;
                vertices.push_back(vertex);
            }
            const Vec3 edge1 = Add(corners[1], Scale(corners[0], -1.0f));
            const Vec3 edge2 = Add(corners[2], Scale(corners[0], -1.0f));
            const bool bFlip = Dot(Cross(edge1, edge2), normal) > 0.0f;
            const uint32_t triangles[6] = {0, bFlip ? 2u : 1u, bFlip ? 1u : 2u, 1, bFlip ? 2u : 3u, bFlip ? 3u : 2u};
            for (uint32_t index : triangles)
            {
                indices.push_back(base + index);
            }
        }

        /// 軸に沿った箱（lo〜hi）の 6 面を足す。UV は面ごとにワールドの平面へ投影する（x の面は (z, -y)、y の面は (x, z)、
        /// z の面は (x, -y)）
        void AppendBox(VariableArray<Mesh3DVertex> &vertices, VariableArray<uint32_t> &indices, const Vec3 &lo,
                       const Vec3 &hi, float tileMeters)
        {
            const Vec3 center = Scale(Add(lo, hi), 0.5f);
            const Vec3 half = Scale(Add(hi, Scale(lo, -1.0f)), 0.5f);
            const Vec3 unitX{1.0f, 0.0f, 0.0f};
            const Vec3 unitY{0.0f, 1.0f, 0.0f};
            const Vec3 unitZ{0.0f, 0.0f, 1.0f};
            const Vec3 downY{0.0f, -1.0f, 0.0f};
            for (float sign : {-1.0f, 1.0f})
            {
                AppendWorldQuad(vertices, indices, Add(center, Scale(unitX, sign * half.X)), unitZ, unitY,
                                Scale(unitX, sign), half.Z, half.Y, unitZ, downY, tileMeters);
                AppendWorldQuad(vertices, indices, Add(center, Scale(unitY, sign * half.Y)), unitX, unitZ,
                                Scale(unitY, sign), half.X, half.Z, unitX, unitZ, tileMeters);
                AppendWorldQuad(vertices, indices, Add(center, Scale(unitZ, sign * half.Z)), unitX, unitY,
                                Scale(unitZ, sign), half.X, half.Y, unitX, downY, tileMeters);
            }
        }

        bool RegisterMesh(GameModeContext &ctx, uint32_t meshId, const VariableArray<Mesh3DVertex> &vertices,
                          const VariableArray<uint32_t> &indices)
        {
            const MeshDataHandle mesh{meshId};
            if (!ctx.RenderResourcesRef.Meshes().Register(mesh, vertices.data(),
                                                          static_cast<uint32_t>(vertices.size() * sizeof(Mesh3DVertex)),
                                                          indices.data(), static_cast<uint32_t>(indices.size())))
            {
                LOG_ERROR_F("interior-light: メッシュ %u を登録できませんでした", meshId);
                return false;
            }
            ctx.ScopeRef.TrackMesh(mesh);
            return true;
        }

        /// ワールドの座標で作ったメッシュを原点に置く。objectColor は描画の物体の色（MeshComponent の CustomData の 0〜2）
        bool SpawnMesh(GameModeContext &ctx, uint32_t meshId, MaterialHandle material, const float objectColor[3])
        {
            Entity *entity = ctx.ScopeRef.SpawnObject<Entity>();
            if (entity == nullptr)
            {
                return false;
            }
            entity->SetPosition(0.0f, 0.0f, 0.0f);
            auto *meshComponent = ctx.WorldRef.CreateComponent<Component::MeshComponent>(entity);
            if (meshComponent == nullptr)
            {
                return false;
            }
            meshComponent->SetMeshHandle(MeshDataHandle{meshId});
            meshComponent->SetMaterial(0, material);
            // 部屋の壁・床・天井は太陽の影を落とし、RTGI の拡散の光線も当てる（影を落とさない物体は既定で光線が素通りする）
            meshComponent->SetCastShadow(true);
            for (uint32_t channel = 0; channel < 3; ++channel)
            {
                meshComponent->SetCustomData(channel, objectColor[channel]);
            }
            meshComponent->SetCustomData(3, 1.0f);
            return true;
        }

        String MakePolyHavenPath(const char *assetId, const char *suffix)
        {
            return String("Assets/Textures/PolyHaven/") + assetId + "/" + assetId + "_" + suffix;
        }

        String MakeGeneratedPath(const char *id, const char *suffix)
        {
            return String("Assets/Models/Generated/") + id + "/" + id + suffix;
        }

        bool IsTextureCooked(const Asset::AssetSystem *assetSystem, const String &path)
        {
            return assetSystem != nullptr &&
                   assetSystem->FindCookedVariant(AnsiString(path.c_str()), Asset::AssetKind::Texture).ShouldUseCooked();
        }

        /// テクスチャ 1 枚を非同期で読み、結果を createData.*member に入れる。読み終えたら（読めなくても）*pendingCount を減らし、
        /// 0 になったら onComplete を呼ぶ。焼いていないテクスチャは読まずに欠けとして数える
        template <typename Owner, typename OnComplete>
        void LoadTexture(TextureResources &textures, const Asset::AssetSystem *assetSystem, const TSharedPtr<Owner> &owner,
                         MaterialCreateData Owner::*createData, uint32_t Owner::*pendingCount, const String &path,
                         TextureHandle MaterialCreateData::*member, bool bNormal, TextureColorEncoding colorEncoding,
                         OnComplete onComplete)
        {
            if (!IsTextureCooked(assetSystem, path))
            {
                LOG_WARNING_F("INTERIOR_LIGHT_ASSET_MISSING path=%s クック済みのテクスチャがありません。"
                              "Scripts/FetchPolyHavenTextures.ps1 -Set interior と Scripts/MakeInteriorLightAlbedo.py を流し、"
                              "CookAssets で焼いてください",
                              path.c_str());
                if (--((*owner).*pendingCount) == 0)
                {
                    onComplete();
                }
                return;
            }
            TextureResources *pTextures = &textures;
            textures.LoadTextureAsync(
                path,
                [pTextures, owner, createData, pendingCount, path, member, bNormal, onComplete](TextureHandle handle)
                {
                    if (owner->bCancelled)
                    {
                        // シーンを出た後に届いた結果。パスで読んだテクスチャはキャッシュが所有するので、何もしない
                        return;
                    }
                    if (!handle.IsValid())
                    {
                        LOG_WARNING_F("INTERIOR_LIGHT_ASSET_MISSING path=%s テクスチャを読めませんでした", path.c_str());
                    }
                    MaterialCreateData &data = (*owner).*createData;
                    data.*member = handle;
                    if (bNormal)
                    {
                        // クック済みの法線が BC5（2 チャンネル）なら印を付ける
                        const NorvesLib::RHI::ITexture *rhiTexture = handle.IsValid() ? pTextures->GetRHITexture(handle) : nullptr;
                        data.bNormalTwoChannel =
                            rhiTexture != nullptr && rhiTexture->GetFormat() == NorvesLib::RHI::Format::BC5_UNORM;
                    }
                    if (--((*owner).*pendingCount) == 0)
                    {
                        onComplete();
                    }
                },
                colorEncoding);
        }

        /// 部屋の材質を 1 つ作り（テクスチャが来るまでは無地）、テクスチャの読み込みを始める
        MaterialHandle CreateRoomMaterial(GameModeContext &ctx, InteriorLightData &data,
                                          const Asset::AssetSystem *assetSystem, const RoomMaterialSpec &spec)
        {
            auto pending = MakeShared<InteriorPendingMaterial>();
            pending->CreateData.DebugName = String("InteriorLight_") + spec.AssetId;
            pending->CreateData.HeightScale = 0.0f;
            pending->CreateData.Metallic = 0.0f;
            pending->CreateData.Roughness = spec.bOrm ? 0.8f : spec.Roughness;
            MaterialResources &materials = ctx.RenderResourcesRef.Materials();
            pending->TargetMaterial = materials.Create(pending->CreateData);
            if (!pending->TargetMaterial.IsValid())
            {
                LOG_ERROR_F("interior-light: 材質 %s を作れませんでした", spec.AssetId);
                return MaterialHandle{};
            }
            ctx.ScopeRef.TrackMaterial(pending->TargetMaterial);
            data.m_PendingMaterials.push_back(pending);
            ++data.m_PendingMaterialCount;

            if (spec.bOrm)
            {
                // 粗さ・AO は ORM から読む（金属は 0 のまま）
                pending->CreateData.Roughness = -1.0f;
            }
            pending->PendingTextureCount = 1u + (spec.bUnitAlbedo ? 1u : 0u) + (spec.bOrm ? 1u : 0u);

            InteriorLightData *pData = &data;
            MaterialResources *pMaterials = &materials;
            const float fallbackRoughness = spec.bOrm ? 0.8f : spec.Roughness;
            const auto onComplete = [pData, pending, pMaterials, fallbackRoughness]()
            {
                if (!pending->CreateData.ORMTexture.IsValid())
                {
                    pending->CreateData.Roughness = fallbackRoughness;
                }
                pMaterials->Update(pending->TargetMaterial, pending->CreateData);
                if (pData->m_PendingMaterialCount > 0)
                {
                    --pData->m_PendingMaterialCount;
                }
            };
            TextureResources &textures = ctx.RenderResourcesRef.Textures();
            const auto createData = &InteriorPendingMaterial::CreateData;
            const auto pendingCount = &InteriorPendingMaterial::PendingTextureCount;
            if (spec.bUnitAlbedo)
            {
                LoadTexture(textures, assetSystem, pending, createData, pendingCount,
                            MakePolyHavenPath(spec.AssetId, "diff_unit_4k.png"), &MaterialCreateData::AlbedoTexture, false,
                            TextureColorEncoding::Srgb, onComplete);
            }
            LoadTexture(textures, assetSystem, pending, createData, pendingCount,
                        MakePolyHavenPath(spec.AssetId, "nor_dx_4k.jpg"), &MaterialCreateData::NormalTexture, true,
                        TextureColorEncoding::Default, onComplete);
            if (spec.bOrm)
            {
                LoadTexture(textures, assetSystem, pending, createData, pendingCount, MakePolyHavenPath(spec.AssetId, "orm_4k"),
                            &MaterialCreateData::ORMTexture, false, TextureColorEncoding::Default, onComplete);
            }
            return pending->TargetMaterial;
        }

        /// 部屋の殻（床・壁・天井・梁・窓の桟）と窓の外の地面を作って置く
        bool CreateRoom(GameModeContext &ctx, InteriorLightData &data, const Asset::AssetSystem *assetSystem)
        {
            const float t = kWallThickness;
            VariableArray<Mesh3DVertex> vertices;
            VariableArray<uint32_t> indices;

            // 漆喰: 窓の壁（窓台の下・窓の上・窓の間の柱）、南の壁、奥の壁、天井
            const float tile = kPlasterSpec.TileMeters;
            AppendBox(vertices, indices, {-t, 0.0f, -t}, {kRoomSizeX + t, kWindowSillHeight, 0.0f}, tile);
            AppendBox(vertices, indices, {-t, kWindowTopHeight, -t}, {kRoomSizeX + t, kRoomHeight, 0.0f}, tile);
            float pierStart = -t;
            for (float centerX : kWindowCentersX)
            {
                AppendBox(vertices, indices, {pierStart, kWindowSillHeight, -t}, {centerX - kWindowHalfWidth, kWindowTopHeight, 0.0f},
                          tile);
                pierStart = centerX + kWindowHalfWidth;
            }
            AppendBox(vertices, indices, {pierStart, kWindowSillHeight, -t}, {kRoomSizeX + t, kWindowTopHeight, 0.0f}, tile);
            AppendBox(vertices, indices, {-t, 0.0f, 0.0f}, {0.0f, kRoomHeight, kRoomSizeZ}, tile);
            AppendBox(vertices, indices, {-t, 0.0f, kRoomSizeZ}, {kRoomSizeX + t, kRoomHeight, kRoomSizeZ + t}, tile);
            AppendBox(vertices, indices, {-t, kRoomHeight, -t}, {kRoomSizeX + t, kRoomHeight + t, kRoomSizeZ + t}, tile);
            const MaterialHandle plaster = CreateRoomMaterial(ctx, data, assetSystem, kPlasterSpec);
            if (!plaster.IsValid() || !RegisterMesh(ctx, kPlasterMeshHandleId, vertices, indices) ||
                !SpawnMesh(ctx, kPlasterMeshHandleId, plaster, kPlasterSpec.ObjectColor))
            {
                return false;
            }

            // 床の板
            vertices.clear();
            indices.clear();
            AppendBox(vertices, indices, {-t, -t, -t}, {kRoomSizeX + t, 0.0f, kRoomSizeZ + t}, kFloorSpec.TileMeters);
            const MaterialHandle floor = CreateRoomMaterial(ctx, data, assetSystem, kFloorSpec);
            if (!floor.IsValid() || !RegisterMesh(ctx, kFloorMeshHandleId, vertices, indices) ||
                !SpawnMesh(ctx, kFloorMeshHandleId, floor, kFloorSpec.ObjectColor))
            {
                return false;
            }

            // 北の端のレンガの壁
            vertices.clear();
            indices.clear();
            AppendBox(vertices, indices, {kRoomSizeX, 0.0f, 0.0f}, {kRoomSizeX + t, kRoomHeight, kRoomSizeZ},
                      kBrickSpec.TileMeters);
            const MaterialHandle brick = CreateRoomMaterial(ctx, data, assetSystem, kBrickSpec);
            if (!brick.IsValid() || !RegisterMesh(ctx, kBrickMeshHandleId, vertices, indices) ||
                !SpawnMesh(ctx, kBrickMeshHandleId, brick, kBrickSpec.ObjectColor))
            {
                return false;
            }

            // 天井の梁と窓の桟（濃い色の木）
            vertices.clear();
            indices.clear();
            for (uint32_t beam = 1; beam <= kBeamCount; ++beam)
            {
                const float x = static_cast<float>(beam) * kBeamPitchX;
                AppendBox(vertices, indices, {x - kBeamHalfWidth, kRoomHeight - kBeamDepth, 0.0f},
                          {x + kBeamHalfWidth, kRoomHeight, kRoomSizeZ}, kTimberSpec.TileMeters);
            }
            const float paneHeight = (kWindowTopHeight - kWindowSillHeight) / static_cast<float>(kWindowPaneRows);
            for (float centerX : kWindowCentersX)
            {
                AppendBox(vertices, indices, {centerX - kMullionHalfWidth, kWindowSillHeight, kMullionMinZ},
                          {centerX + kMullionHalfWidth, kWindowTopHeight, kMullionMaxZ}, kTimberSpec.TileMeters);
                for (uint32_t row = 1; row < kWindowPaneRows; ++row)
                {
                    const float y = kWindowSillHeight + static_cast<float>(row) * paneHeight;
                    AppendBox(vertices, indices, {centerX - kWindowHalfWidth, y - kMullionHalfWidth, kMullionMinZ},
                              {centerX + kWindowHalfWidth, y + kMullionHalfWidth, kMullionMaxZ}, kTimberSpec.TileMeters);
                }
            }
            const MaterialHandle timber = CreateRoomMaterial(ctx, data, assetSystem, kTimberSpec);
            if (!timber.IsValid() || !RegisterMesh(ctx, kTimberMeshHandleId, vertices, indices) ||
                !SpawnMesh(ctx, kTimberMeshHandleId, timber, kTimberSpec.ObjectColor))
            {
                return false;
            }

            // 窓の外の地面
            vertices.clear();
            indices.clear();
            const float groundCenterX = 0.5f * kRoomSizeX;
            const float groundCenterZ = 0.5f * kRoomSizeZ;
            AppendWorldQuad(vertices, indices, {groundCenterX, kGroundHeight, groundCenterZ}, {1.0f, 0.0f, 0.0f},
                            {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, 0.5f * kGroundSize, 0.5f * kGroundSize,
                            {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, kGroundSpec.TileMeters);
            const MaterialHandle ground = CreateRoomMaterial(ctx, data, assetSystem, kGroundSpec);
            return ground.IsValid() && RegisterMesh(ctx, kGroundMeshHandleId, vertices, indices) &&
                   SpawnMesh(ctx, kGroundMeshHandleId, ground, kGroundSpec.ObjectColor);
        }

        /// 家具 1 種のクック済みメッシュを解決・解析し、材質のテクスチャ 3 枚の読み込みを始める。
        /// 焼いていない・壊れているときは理由を出して置かない（load.bDone を立てる）
        void StartFurnitureLoad(GameModeContext &ctx, const TSharedPtr<const Asset::AssetSystem> &assetSystem,
                                const TSharedPtr<InteriorFurnitureLoad> &load)
        {
            const FurnitureKindSpec &kind = kFurnitureKinds[load->KindIndex];
            const String meshPath = MakeGeneratedPath(kind.Id, ".glb");
            const String albedoPath = MakeGeneratedPath(kind.Id, "_albedo_unit.png");
            const String ormPath = MakeGeneratedPath(kind.Id, ".glb.img1.png");
            const String normalPath = MakeGeneratedPath(kind.Id, ".glb.img2.png");
            const Asset::AssetResolveResult resolved =
                assetSystem ? assetSystem->ResolveAsset(meshPath.c_str(), Asset::AssetKind::Model) : Asset::AssetResolveResult{};
            if (!assetSystem || !resolved.UsedCooked())
            {
                LOG_WARNING_F("INTERIOR_LIGHT_ASSET_MISSING path=%s クック済みのメッシュがありません。CookAssets で焼いてください",
                              meshPath.c_str());
                load->bDone = true;
                return;
            }
            Asset::CookedMeshParseResult parsed = Asset::ParseCookedMesh(resolved.Blob);
            if (!parsed.Succeeded() || parsed.Mesh.Clusters.empty() || parsed.Mesh.Vertices.empty())
            {
                LOG_WARNING_F("INTERIOR_LIGHT_ASSET_MISSING path=%s status=%u クック済みのメッシュを解析できません",
                              meshPath.c_str(), static_cast<unsigned int>(parsed.Status));
                load->bDone = true;
                return;
            }
            Asset::AssetCookedRange pageRange;
            if (parsed.Mesh.Pages.size() >= 2 && Asset::AssetSystem::TryMakeCookedRange(resolved, pageRange))
            {
                load->PageSource = MakeCookedMeshPageSource(parsed.Mesh, assetSystem->GetCookedFileReader(),
                                                            pageRange.Request, pageRange.BaseOffset);
            }
            float minY = std::numeric_limits<float>::max();
            for (const Asset::CookedMeshVertex &vertex : parsed.Mesh.Vertices)
            {
                minY = std::min(minY, vertex.Position.Y);
            }
            load->MinY = minY;
            load->Mesh = MakeShared<Asset::CookedMeshData>(std::move(parsed.Mesh));
            load->Material.DebugName = kind.Id;
            load->PendingTextureCount = 3;

            TextureResources &textures = ctx.RenderResourcesRef.Textures();
            const auto createData = &InteriorFurnitureLoad::Material;
            const auto pendingCount = &InteriorFurnitureLoad::PendingTextureCount;
            // テクスチャがそろったかは Tick が PendingTextureCount で見る
            const auto onComplete = []() {};
            LoadTexture(textures, assetSystem.get(), load, createData, pendingCount, albedoPath,
                        &MaterialCreateData::AlbedoTexture, false, TextureColorEncoding::Srgb, onComplete);
            LoadTexture(textures, assetSystem.get(), load, createData, pendingCount, normalPath,
                        &MaterialCreateData::NormalTexture, true, TextureColorEncoding::Default, onComplete);
            LoadTexture(textures, assetSystem.get(), load, createData, pendingCount, ormPath, &MaterialCreateData::ORMTexture,
                        false, TextureColorEncoding::Default, onComplete);
        }

        /// テクスチャがそろった家具を MegaMesh にして、その種類の置き場所すべてに置く（置けなければ理由を出して飛ばす）
        void PlaceFurniture(GameModeContext &ctx, InteriorLightData &data, InteriorFurnitureLoad &load)
        {
            load.bDone = true;
            const FurnitureKindSpec &kind = kFurnitureKinds[load.KindIndex];
            const MaterialCreateData &material = load.Material;
            if (!material.AlbedoTexture.IsValid() || !material.NormalTexture.IsValid() || !material.ORMTexture.IsValid())
            {
                LOG_WARNING_F("INTERIOR_LIGHT_ASSET_MISSING id=%s 材質のテクスチャを読めないため置きません（albedo=%d normal=%d orm=%d）",
                              kind.Id, material.AlbedoTexture.IsValid() ? 1 : 0, material.NormalTexture.IsValid() ? 1 : 0,
                              material.ORMTexture.IsValid() ? 1 : 0);
                load.Mesh.reset();
                return;
            }
            MegaMeshCreateInfo createInfo;
            if (!BuildMegaMeshCreateInfoFromCookedMesh(*load.Mesh, createInfo))
            {
                LOG_WARNING_F("INTERIOR_LIGHT_ASSET_MISSING id=%s MegaMesh の入力を作れませんでした", kind.Id);
                load.Mesh.reset();
                return;
            }
            createInfo.PageSource = load.PageSource;
            createInfo.Material.AlbedoTexture = material.AlbedoTexture;
            createInfo.Material.NormalTexture = material.NormalTexture;
            createInfo.Material.ORMTexture = material.ORMTexture;
            createInfo.Material.bNormalTwoChannel = material.bNormalTwoChannel;
            for (uint32_t channel = 0; channel < 3; ++channel)
            {
                createInfo.Material.BaseColor[channel] = kind.AlbedoScale[channel];
            }
            createInfo.DebugName = kind.Id;
            auto &megaGeometry = ctx.RenderResourcesRef.MegaGeometry();
            const MegaMeshHandle megaMesh = megaGeometry.CreateMegaMesh(createInfo);
            load.Mesh.reset();
            if (!megaMesh.IsValid())
            {
                LOG_WARNING_F("INTERIOR_LIGHT_ASSET_MISSING id=%s MegaMesh を作れませんでした", kind.Id);
                return;
            }
            const String meshPath = MakeGeneratedPath(kind.Id, ".glb");
            const ModelHandle model = megaGeometry.RegisterModel(megaMesh, kind.Id, meshPath.c_str());
            if (!model.IsValid())
            {
                megaGeometry.ReleaseMegaMesh(megaMesh);
                LOG_WARNING_F("INTERIOR_LIGHT_ASSET_MISSING id=%s モデルとして登録できませんでした", kind.Id);
                return;
            }
            ctx.ScopeRef.TrackModel(model);
            for (const FurniturePlacement &placement : kFurniturePlacements)
            {
                if (placement.KindIndex != load.KindIndex)
                {
                    continue;
                }
                Entity *object = ctx.ScopeRef.SpawnObject<Entity>();
                auto *component =
                    object != nullptr ? ctx.WorldRef.CreateComponent<Component::MegaGeometryComponent>(object) : nullptr;
                if (component == nullptr)
                {
                    continue;
                }
                // 模型の底（ローカルの最も低い頂点）を床（y 0）に載せる
                object->SetPosition(placement.X, -load.MinY, placement.Z);
                object->SetRotation(NorvesLib::Math::Quaternion(NorvesLib::Math::Vector3(0.0f, 1.0f, 0.0f),
                                                                placement.YawDegrees * kDegreesToRadians));
                component->SetMegaMeshHandle(megaMesh);
                component->SetCastShadow(true);
                ++data.m_PlacedFurniture;
            }
        }

        void ApplyEnvironment(GameModeContext &ctx)
        {
            SkyAtmosphereParameters sky = MakeDefaultSkyAtmosphereParameters();
            sky.bEnabled = true;
            sky.SunAltitudeDegrees = kSunAltitudeDegrees;
            sky.SunAzimuthDegrees = kSunAzimuthDegrees;
            ctx.EngineRef.GetRenderWorld().SetSkyAtmosphere(sky);
            ctx.EngineRef.GetRenderWorld().SetVolumetricFogParameters(MakeDefaultVolumetricFogParameters());
            ctx.EngineRef.GetRenderWorld().SetStaticEnvironmentIntensityScale(1.0f);
            ctx.EngineRef.GetRenderWorld().SetStaticEnvironmentMaxRadiance(0.0f);
        }

        /// 空の設定を、ほかのモードへ持ち越さないよう既定へ戻す
        void ResetEnvironment(GameModeContext &ctx)
        {
            ctx.EngineRef.GetRenderWorld().SetSkyAtmosphere(MakeDefaultSkyAtmosphereParameters());
            ctx.EngineRef.GetRenderWorld().SetVolumetricFogParameters(MakeDefaultVolumetricFogParameters());
            ctx.EngineRef.GetRenderWorld().SetStaticEnvironmentIntensityScale(1.0f);
            ctx.EngineRef.GetRenderWorld().SetStaticEnvironmentMaxRadiance(0.0f);
        }

        /// 届いていない読み込みの結果を捨てる印を立てる
        void CancelPendingLoads(InteriorLightData &data)
        {
            for (const TSharedPtr<InteriorPendingMaterial> &pending : data.m_PendingMaterials)
            {
                pending->bCancelled = true;
            }
            for (const TSharedPtr<InteriorFurnitureLoad> &load : data.m_FurnitureLoads)
            {
                load->bCancelled = true;
                load->Mesh.reset();
            }
            data.m_PendingMaterials.clear();
            data.m_PendingMaterialCount = 0;
            data.m_FurnitureLoads.clear();
        }
    } // namespace

    GameModeEnterResult InteriorLightRoutine::Enter(GameModeContext &ctx, InteriorLightData &data)
    {
        LOG_INFO("interior-light を開始します");
        data.m_TickCount = 0;
        data.m_PlacedFurniture = 0;
        data.m_bAssemblyLogged = false;
        Game::Scenes::BeginSceneAssembly(ctx.EngineRef);

        const Game::Scenes::SceneDescriptor *scene = Game::Scenes::FindScene(Game::Scenes::kInteriorLightSceneId);
        Game::Scenes::FreeCameraPose pose{};
        if (scene == nullptr || !Game::Scenes::ResolveFreeCameraPose(*scene, data.m_ViewName.c_str(), pose))
        {
            LOG_ERROR("interior-light: シーンの一覧に視点の定義がありません");
            Game::Scenes::EndSceneAssembly(ctx.EngineRef);
            return GameModeEnterResult::Failed;
        }

        TSharedPtr<const Asset::AssetSystem> assetSystem;
        if (data.m_GetAssetSystem.IsBound())
        {
            assetSystem = data.m_GetAssetSystem.Invoke();
        }
        if (!assetSystem)
        {
            LOG_WARNING("INTERIOR_LIGHT_ASSET_MISSING AssetSystem が無いため、テクスチャと家具を読まずに部屋を無地で描きます");
        }

        ApplyEnvironment(ctx);
        if (!CreateRoom(ctx, data, assetSystem.get()))
        {
            CancelPendingLoads(data);
            ResetEnvironment(ctx);
            Game::Scenes::EndSceneAssembly(ctx.EngineRef);
            return GameModeEnterResult::Failed;
        }
        for (uint32_t kindIndex = 0; kindIndex < kFurnitureKindCount; ++kindIndex)
        {
            auto load = MakeShared<InteriorFurnitureLoad>();
            load->KindIndex = kindIndex;
            data.m_FurnitureLoads.push_back(load);
            StartFurnitureLoad(ctx, assetSystem, load);
        }
        LOG_INFO_F("INTERIOR_LIGHT_LAYOUT room=%.1fx%.1fx%.1f windows=%u beams=%u furniture_kinds=%u furniture_planned=%u "
                   "sun_altitude=%.1f sun_azimuth=%.1f",
                   static_cast<double>(kRoomSizeX), static_cast<double>(kRoomSizeZ), static_cast<double>(kRoomHeight),
                   static_cast<uint32_t>(sizeof(kWindowCentersX) / sizeof(kWindowCentersX[0])), kBeamCount,
                   kFurnitureKindCount,
                   static_cast<uint32_t>(sizeof(kFurniturePlacements) / sizeof(kFurniturePlacements[0])),
                   static_cast<double>(kSunAltitudeDegrees), static_cast<double>(kSunAzimuthDegrees));

        data.m_Rig.ConfigurePath(scene->CameraPath, data.m_CameraPathTime, data.m_bCameraPathPlay);
        if (!data.m_Rig.Enter(ctx, pose, data.m_CameraScriptPath.c_str()))
        {
            LOG_ERROR("interior-light: カメラを作れませんでした");
            CancelPendingLoads(data);
            ResetEnvironment(ctx);
            Game::Scenes::EndSceneAssembly(ctx.EngineRef);
            return GameModeEnterResult::Failed;
        }

        // 窓の外の日向と室内の日陰の明るさの差が大きく、視点ごとに見る所の明るさが違うので自動露出にする。
        // 室内の写真と同じく室内に露出を合わせ、日陰の梁と隅が黒くつぶれないよう +0.5 EV 上げる（窓の外は白く飛んでよい）
        Component::CameraComponent *camera = data.m_Rig.GetCamera();
        camera->SetAperture(16.0f);
        camera->SetISO(100.0f);
        camera->SetExposureMode(CameraExposureMode::Auto);
        camera->SetExposureCompensation(kExposureCompensationEV);
        camera->SetAntiAliasingMode(CameraAntiAliasingMode::TemporalAA);
        return GameModeEnterResult::Succeeded;
    }

    void InteriorLightRoutine::Tick(GameModeContext &ctx, InteriorLightData &data, float deltaTime)
    {
        ++data.m_TickCount;
        // 撮影の視点の切り替えは、このフレームのカメラを渡す前に置き直す
        if (const Game::Scenes::SceneDescriptor *scene = Game::Scenes::FindScene(Game::Scenes::kInteriorLightSceneId))
        {
            data.m_Rig.ApplyRequestedView(data.m_pViewSwitch, *scene);
        }
        const bool bSuspended = data.m_pPauseMenu != nullptr && data.m_pPauseMenu->IsOpen();
        data.m_Rig.Tick(ctx, deltaTime, bSuspended);

        bool bFurnitureDone = true;
        for (const TSharedPtr<InteriorFurnitureLoad> &load : data.m_FurnitureLoads)
        {
            if (!load->bDone && load->PendingTextureCount == 0)
            {
                PlaceFurniture(ctx, data, *load);
            }
            bFurnitureDone = bFurnitureDone && load->bDone;
        }
        const bool bAssembled = data.m_PendingMaterialCount == 0 && bFurnitureDone;
        if (bAssembled && !data.m_bAssemblyLogged)
        {
            LOG_INFO_F("INTERIOR_LIGHT_ASSEMBLED materials=%u furniture_placed=%u furniture_planned=%u tick=%u",
                       static_cast<uint32_t>(data.m_PendingMaterials.size()), data.m_PlacedFurniture,
                       static_cast<uint32_t>(sizeof(kFurniturePlacements) / sizeof(kFurniturePlacements[0])),
                       data.m_TickCount);
            data.m_bAssemblyLogged = true;
        }
        // 材質のテクスチャと家具がそろい、台本を流し終えるまで、決定的な撮影を待たせる
        const bool bReady = bAssembled && data.m_TickCount >= kReadyTickCount && !data.m_Rig.IsScriptRunning();
        if (bReady)
        {
            data.m_Rig.NotifySceneReady();
        }
        Game::Scenes::UpdateSceneAssembly(ctx.EngineRef, Game::Scenes::kInteriorLightSceneId, bReady);
    }

    void InteriorLightRoutine::Leave(GameModeContext &ctx, InteriorLightData &data, GameModeExitReason)
    {
        Game::Scenes::EndSceneAssembly(ctx.EngineRef);
        CancelPendingLoads(data);
        data.m_Rig.Leave(ctx);
        // 部屋・家具・材質・カメラの実体は GameModeScope::Cleanup が返す。空は RenderWorld の設定なので戻す。
        ResetEnvironment(ctx);
        LOG_INFO("interior-light を終了します");
    }

} // namespace Game::GameModes

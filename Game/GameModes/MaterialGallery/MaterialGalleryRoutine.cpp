#include "GameModes/MaterialGallery/MaterialGalleryMode.h"

#include "Core/Public/Component/CameraComponent.h"
#include "Core/Public/Component/MeshComponent.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/GameMode/GameModeScope.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Math/Vector3.h"
#include "Core/Public/Module/ModuleRegistry.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/RHI/ITexture.h"
#include "Core/Public/Rendering/MaterialTypes.h"
#include "Core/Public/Rendering/ProceduralMeshGenerator.h"
#include "Core/Public/Rendering/RenderResources.h"
#include "Core/Public/Rendering/RenderWorld.h"
#include "Core/Public/Rendering/SkyAtmosphere.h"
#include "Core/Public/Rendering/VolumetricFog.h"
#include "GameModes/Rendering3DTest/SkySunControl.h"
#include "Scenes/SceneCatalog.h"
#include "Scenes/SceneReadiness.h"

#if defined(NORVES_ENABLE_IMGUI)
#include "imgui.h"
#endif

#include <cmath>
#include <cstdint>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Container;
using namespace NorvesLib::Core::GameMode;
using namespace NorvesLib::Core::Rendering;

namespace Game::GameModes
{

    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kDegreesToRadians = kPi / 180.0f;

        // 太陽は仰角 40°・方位 235°（-X・-Z の側。台座の板が向く -Z の側から斜めに当たり、板と球の凹凸に影が付く）
        constexpr float kSunAltitudeDegrees = 40.0f;
        constexpr float kSunAzimuthDegrees = 235.0f;
        // 晴天の昼の手動露出（絞り f/16・ISO 100 のままシャッター速度で合わせる。晴天の f/16 の目安）
        constexpr float kExposureEV100 = 15.0f;
        // SceneReady を出すまでの最小の Tick の数。決定的な撮影は「読み込み中」を一度観測してから数え始める
        constexpr uint32_t kReadyTickCount = 3;

        // 他のシーンのメッシュの ID（100〜、200〜、900〜942）と重ならない値
        constexpr uint32_t kFloorMeshHandleId = 1000;
        constexpr uint32_t kPedestalMeshHandleId = 1001;
        constexpr uint32_t kExhibitSphereMeshHandleId = 1002;
        constexpr uint32_t kChartSphereMeshHandleId = 1003;
        constexpr uint32_t kStepMeshHandleBase = 1010;  // ひな壇の段（6 つ）
        constexpr uint32_t kPlateMeshHandleBase = 1100; // 展示の板（材質ごと。UV の実寸が材質ごとに違う）

        // 展示場の床: Poly Haven の stone_floor（1 枚 2 m）。展示の列とひな壇を囲む 70 × 50 m
        constexpr const char *kFloorAssetId = "stone_floor";
        constexpr float kFloorTileMeters = 2.0f;
        constexpr float kFloorHeightDepthMeters = 0.02f;
        constexpr float kFloorCenterX = -4.0f;
        constexpr float kFloorCenterZ = 10.0f;
        constexpr float kFloorSizeX = 70.0f;
        constexpr float kFloorSizeZ = 50.0f;

        // 展示の材質（Poly Haven の資産 id）。並びは台座の列（手前から奥）×列の中（画面の左から右）の順。
        // テクスチャは Scripts/FetchPolyHavenTextures.ps1 -Set gallery が落とし、Assets/AssetSets の一覧が焼く。
        struct GalleryMaterialSpec
        {
            const char *AssetId;     // フォルダ名・ファイル名の接頭辞
            const char *Resolution;  // ファイル名の解像度（4k・8k）
            float TileMeters;        // テクスチャ 1 枚の実寸[m]
            float HeightDepthMeters; // 視差オクルージョンの深さ[m]。0 なら高さを読まない
            bool bCalibratedAlbedo;  // 色を荒野の組の較正済みの色（_diff_cal_*.png）で読む
        };
        constexpr GalleryMaterialSpec kGalleryMaterials[] = {
            // 1 列目: 地面（起動画面の地面の区画と負荷用の組）
            {"snow_02", "4k", 2.0f, 0.02f, false},
            {"brown_mud_leaves_01", "4k", 1.3f, 0.02f, false},
            {"forrest_ground_01", "4k", 2.0f, 0.03f, false},
            {"sand_01", "4k", 1.5f, 0.01f, false},
            {"asphalt_02", "4k", 3.0f, 0.005f, false},
            {"marble_01", "4k", 1.5f, 0.002f, false},
            {"aerial_rocks_02", "4k", 2.0f, 0.04f, false},
            {"coast_sand_rocks_02", "4k", 2.0f, 0.04f, false},
            {"dry_ground_rocks", "4k", 2.0f, 0.03f, false},
            {"gravel_ground_01", "4k", 2.0f, 0.02f, false},
            {"forest_ground_04", "4k", 2.0f, 0.03f, false},
            // 2 列目: 岩・壁・木
            {"rock_04", "4k", 2.0f, 0.04f, false},
            {"castle_brick_01", "4k", 2.0f, 0.04f, false},
            {"concrete_wall_003", "4k", 2.0f, 0.01f, false},
            {"quarry_wall", "4k", 3.0f, 0.05f, false},
            {"mossy_stone_wall", "4k", 2.0f, 0.04f, false},
            {"brown_planks_03", "4k", 1.5f, 0.005f, false},
            {"dark_planks", "4k", 1.5f, 0.005f, false},
            {"herringbone_parquet", "4k", 1.5f, 0.003f, false},
            {"bark_brown_01", "4k", 2.0f, 0.03f, false},
            {"cobblestone_floor_01", "4k", 2.0f, 0.03f, false},
            {"brick_floor", "4k", 2.0f, 0.02f, false},
            // 3 列目: 床・金属・屋根
            {"concrete_floor_worn_001", "4k", 3.0f, 0.005f, false},
            {"terracotta_floor_tiles", "4k", 2.0f, 0.01f, false},
            {"corrugated_iron", "4k", 2.0f, 0.02f, false},
            {"rusty_metal_02", "4k", 2.0f, 0.005f, false},
            {"metal_plate", "4k", 2.0f, 0.005f, false},
            {"clay_roof_tiles", "4k", 2.0f, 0.04f, false},
            {"roof_slates_02", "4k", 2.0f, 0.03f, false},
            {"grey_roof_tiles", "4k", 2.0f, 0.03f, false},
            {"stone_floor", "4k", 2.0f, 0.02f, false},
            {"rock_tile_floor", "4k", 2.0f, 0.03f, false},
            // 4 列目: 荒野の組（SCN-PH-WASTELAND の候補と地形の層）。荒野の地形の砂・土（_8k）は高さを持たない
            {"red_mud_stones", "4k", 1.5f, 0.03f, true},
            {"red_laterite_soil_stones", "4k", 2.0f, 0.03f, true},
            {"cliff_side", "4k", 1.8f, 0.05f, true},
            {"cracked_red_ground", "4k", 2.0f, 0.02f, true},
            {"red_sand", "4k", 3.0f, 0.01f, true},
            {"red_dirt_mud_01", "4k", 2.0f, 0.02f, true},
            {"sandy_gravel_02", "4k", 2.0f, 0.02f, false},
            {"dry_ground_01", "4k", 2.0f, 0.02f, false},
            {"rock_face", "4k", 2.0f, 0.05f, false},
            {"wasteland_soil_sand", "8k", 1.5f, 0.0f, true},
            {"wasteland_dirt_crust", "8k", 2.0f, 0.0f, true},
        };
        constexpr uint32_t kGalleryMaterialCount =
            static_cast<uint32_t>(sizeof(kGalleryMaterials) / sizeof(kGalleryMaterials[0]));
        // 1 列目から 3 列目までの台座の数（4 列目は残り）
        constexpr uint32_t kRowStarts[] = {0u, 11u, 22u, 32u};
        constexpr uint32_t kRowCount = static_cast<uint32_t>(sizeof(kRowStarts) / sizeof(kRowStarts[0]));

        // 台座の列: 列の中の間隔 2.4 m、列の間隔 5 m。台座は -Z（通路の側）を向く。列の中の番号 0 が画面の左（+X）
        constexpr float kPedestalPitchX = 2.4f;
        constexpr float kRowPitchZ = 5.0f;
        constexpr float kPedestalWidth = 1.9f;
        constexpr float kPedestalDepth = 0.9f;
        constexpr float kPedestalHeight = 0.8f;
        // 板: 0.85 m 四方・厚さ 3 cm を、水平から 55° 起こして通路へ向ける。台座の上の左半分に置く
        constexpr float kPlateSize = 0.85f;
        constexpr float kPlateThickness = 0.03f;
        constexpr float kPlateTiltDegrees = 55.0f;
        constexpr float kPlateOffsetX = 0.45f;
        // 板の下の縁の z（台座の中心から）
        constexpr float kPlateBottomOffsetZ = -0.35f;
        // 球: 半径 0.3 m。台座の上の右半分に載せる
        constexpr float kExhibitSphereRadius = 0.3f;
        constexpr float kExhibitSphereOffsetX = -0.5f;
        constexpr float kExhibitSphereOffsetZ = -0.05f;

        // 金属度 × 粗さの格子: 6 × 6 の球（半径 0.4 m、間隔 1.1 m）を、奥へ 0.35 m ずつ上がるひな壇に並べる。
        // 列（画面の左から右）が金属度 0〜1、段（手前から奥）が粗さ 0〜1。展示の列の右（-X）の一角に置く
        constexpr uint32_t kChartSteps = 6u;
        constexpr float kChartCenterX = -20.0f;
        constexpr float kChartFirstStepZ = 2.0f;
        constexpr float kChartPitch = 1.1f;
        constexpr float kChartSphereRadius = 0.4f;
        constexpr float kChartFirstStepHeight = 0.4f;
        constexpr float kChartStepRise = 0.35f;
        // 格子の球の色（リニア。鉄に近い中間の灰色）
        constexpr float kChartBaseColor[3] = {0.56f, 0.57f, 0.58f};
        constexpr const char *kMetallicLabels[kChartSteps] = {"metallic 0.0", "metallic 0.2", "metallic 0.4",
                                                              "metallic 0.6", "metallic 0.8", "metallic 1.0"};
        constexpr const char *kRoughnessLabels[kChartSteps] = {"roughness 0.0", "roughness 0.2", "roughness 0.4",
                                                               "roughness 0.6", "roughness 0.8", "roughness 1.0"};

        // 台座・ひな壇の材質（無地の灰色のコンクリート。展示の色より目立たない暗さ）
        constexpr float kPedestalBaseColor[3] = {0.2f, 0.2f, 0.19f};
        constexpr float kPedestalRoughness = 0.8f;
        // 読み込む前の展示の材質（無地の灰色。テクスチャが欠けた材質もこのまま描く）
        constexpr float kPlaceholderBaseColor[3] = {0.3f, 0.3f, 0.3f};

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

        /// 四角形を 1 枚足す。center を中心に、axisU 方向へ ±halfU、axisV 方向へ ±halfV。法線は normal。
        /// UV は (axisU, axisV) 方向の距離を tileMeters で割った値（0 から始まる）。三角形の巡回は GeneratePlane と同じ
        /// （2 辺の外積が法線と逆向き）にそろえる
        void AppendQuad(VariableArray<Mesh3DVertex> &vertices, VariableArray<uint32_t> &indices, const Vec3 &center,
                        const Vec3 &axisU, const Vec3 &axisV, const Vec3 &normal, float halfU, float halfV,
                        float tileMeters)
        {
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            const float signs[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {-1.0f, 1.0f}, {1.0f, 1.0f}};
            Vec3 corners[4];
            for (uint32_t index = 0; index < 4; ++index)
            {
                const float u = signs[index][0] * halfU;
                const float v = signs[index][1] * halfV;
                corners[index] = Add(center, Add(Scale(axisU, u), Scale(axisV, v)));
                Mesh3DVertex vertex{};
                vertex.Position[0] = corners[index].X;
                vertex.Position[1] = corners[index].Y;
                vertex.Position[2] = corners[index].Z;
                vertex.Normal[0] = normal.X;
                vertex.Normal[1] = normal.Y;
                vertex.Normal[2] = normal.Z;
                vertex.TexCoord[0] = (u + halfU) / tileMeters;
                vertex.TexCoord[1] = (v + halfV) / tileMeters;
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

        /// 向きのある箱を足す。表（+axisN）の面の UV は (axisU, axisV) に沿う。ほかの面の UV も実寸で割る
        void AppendOrientedBox(VariableArray<Mesh3DVertex> &vertices, VariableArray<uint32_t> &indices,
                               const Vec3 &center, const Vec3 &axisU, const Vec3 &axisV, const Vec3 &axisN, float halfU,
                               float halfV, float halfN, float tileMeters)
        {
            const Vec3 minusU = Scale(axisU, -1.0f);
            const Vec3 minusN = Scale(axisN, -1.0f);
            const Vec3 minusV = Scale(axisV, -1.0f);
            AppendQuad(vertices, indices, Add(center, Scale(axisN, halfN)), axisU, axisV, axisN, halfU, halfV, tileMeters);
            AppendQuad(vertices, indices, Add(center, Scale(axisN, -halfN)), minusU, axisV, minusN, halfU, halfV,
                       tileMeters);
            AppendQuad(vertices, indices, Add(center, Scale(axisU, halfU)), minusN, axisV, axisU, halfN, halfV, tileMeters);
            AppendQuad(vertices, indices, Add(center, Scale(axisU, -halfU)), axisN, axisV, minusU, halfN, halfV,
                       tileMeters);
            AppendQuad(vertices, indices, Add(center, Scale(axisV, halfV)), axisU, axisN, axisV, halfU, halfN, tileMeters);
            AppendQuad(vertices, indices, Add(center, Scale(axisV, -halfV)), axisU, minusN, minusV, halfU, halfN,
                       tileMeters);
        }

        /// 通路（-Z）を向いた台座・段の箱（中心が原点、下の面が y = -height/2）
        void BuildFacingBox(float width, float height, float depth, VariableArray<Mesh3DVertex> &vertices,
                            VariableArray<uint32_t> &indices)
        {
            vertices.clear();
            indices.clear();
            // 表の面は -Z。画面の右が -X、下が -Y
            AppendOrientedBox(vertices, indices, {0.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
                              {0.0f, 0.0f, -1.0f}, width * 0.5f, height * 0.5f, depth * 0.5f, 1.0f);
        }

        /// 傾けた板（中心が原点）。表の面は水平から kPlateTiltDegrees 起こして -Z へ向き、UV は板の左上から実寸で貼る
        void BuildPlate(float tileMeters, VariableArray<Mesh3DVertex> &vertices, VariableArray<uint32_t> &indices)
        {
            vertices.clear();
            indices.clear();
            const float tilt = kPlateTiltDegrees * kDegreesToRadians;
            const Vec3 normal{0.0f, std::cos(tilt), -std::sin(tilt)};
            // 画面の右（-X）が U、板を下る向きが V（画像の行が下へ進む向き）
            const Vec3 axisU{-1.0f, 0.0f, 0.0f};
            const Vec3 axisV{0.0f, -std::sin(tilt), -std::cos(tilt)};
            AppendOrientedBox(vertices, indices, {0.0f, 0.0f, 0.0f}, axisU, axisV, normal, kPlateSize * 0.5f,
                              kPlateSize * 0.5f, kPlateThickness * 0.5f, tileMeters);
        }

        bool RegisterMesh(GameModeContext &ctx, uint32_t meshId, const VariableArray<Mesh3DVertex> &vertices,
                          const VariableArray<uint32_t> &indices)
        {
            const MeshDataHandle mesh{meshId};
            if (!ctx.RenderResourcesRef.Meshes().Register(mesh, vertices.data(),
                                                          static_cast<uint32_t>(vertices.size() * sizeof(Mesh3DVertex)),
                                                          indices.data(), static_cast<uint32_t>(indices.size())))
            {
                LOG_ERROR_F("material-gallery: メッシュ %u を登録できませんでした", meshId);
                return false;
            }
            ctx.ScopeRef.TrackMesh(mesh);
            return true;
        }

        /// メッシュを 1 つ置く。失敗したら false。objectColor を渡すと、描画の物体の色（GBuffer がアルベドに掛ける
        /// ObjectColor。MeshComponent の CustomData の 0〜2）にする。無地の材質の色はここで決まる
        bool SpawnMesh(GameModeContext &ctx, uint32_t meshId, MaterialHandle material, const Vec3 &position,
                       bool bCastShadow = true, const float *objectColor = nullptr)
        {
            Entity *entity = ctx.ScopeRef.SpawnObject<Entity>();
            if (entity == nullptr)
            {
                return false;
            }
            entity->SetPosition(position.X, position.Y, position.Z);
            auto *meshComponent = ctx.WorldRef.CreateComponent<Component::MeshComponent>(entity);
            if (meshComponent == nullptr)
            {
                return false;
            }
            meshComponent->SetMeshHandle(MeshDataHandle{meshId});
            meshComponent->SetMaterial(0, material);
            meshComponent->SetCastShadow(bCastShadow);
            if (objectColor != nullptr)
            {
                for (uint32_t channel = 0; channel < 3; ++channel)
                {
                    meshComponent->SetCustomData(channel, objectColor[channel]);
                }
                meshComponent->SetCustomData(3, 1.0f);
            }
            return true;
        }

        MaterialHandle CreatePlainMaterial(GameModeContext &ctx, const char *debugName, const float baseColor[3],
                                           float metallic, float roughness)
        {
            MaterialCreateData materialData;
            materialData.DebugName = debugName;
            materialData.BaseColor[0] = baseColor[0];
            materialData.BaseColor[1] = baseColor[1];
            materialData.BaseColor[2] = baseColor[2];
            materialData.BaseColor[3] = 1.0f;
            // 高さのテクスチャを持たないので視差を掛けない
            materialData.HeightScale = 0.0f;
            materialData.Metallic = metallic;
            materialData.Roughness = roughness;
            const MaterialHandle material = ctx.RenderResourcesRef.Materials().Create(materialData);
            if (material.IsValid())
            {
                ctx.ScopeRef.TrackMaterial(material);
            }
            return material;
        }

        String MakeTexturePath(const GalleryMaterialSpec &spec, const char *suffix, const char *extension)
        {
            return String("Assets/Textures/PolyHaven/") + spec.AssetId + "/" + spec.AssetId + "_" + suffix + "_" +
                   spec.Resolution + extension;
        }

        /// テクスチャ 1 枚を非同期で読み、結果を member へ入れる。全部そろったら材質を更新して onComplete を呼ぶ。
        /// 焼いていないテクスチャは読まず（ばらの 4K の元画像を無圧縮で全部常駐させない）、欠けとして数える
        template <typename OnComplete>
        void LoadGalleryTexture(TextureResources &textures, const Asset::AssetSystem *assetSystem,
                                const TSharedPtr<GalleryPendingMaterial> &pending, const String &path,
                                TextureHandle MaterialCreateData::*member, bool bNormal, OnComplete onComplete)
        {
            if (assetSystem == nullptr ||
                !assetSystem->FindCookedVariant(AnsiString(path.c_str()), Asset::AssetKind::Texture).ShouldUseCooked())
            {
                LOG_WARNING_F("MATERIAL_GALLERY_TEXTURE_MISSING path=%s クック済みのテクスチャがありません。"
                              "Scripts/FetchPolyHavenTextures.ps1 -Set gallery で落とし、CookAssets で焼いてください",
                              path.c_str());
                if (--pending->PendingTextureCount == 0)
                {
                    onComplete();
                }
                return;
            }
            TextureResources *pTextures = &textures;
            textures.LoadTextureAsync(
                path,
                [pTextures, pending, path, member, bNormal, onComplete](TextureHandle handle)
                {
                    if (pending->bCancelled)
                    {
                        // シーンを出た後に届いた結果。パスで読んだテクスチャはキャッシュが所有するので、何もしない
                        return;
                    }
                    if (!handle.IsValid())
                    {
                        LOG_WARNING_F("MATERIAL_GALLERY_TEXTURE_MISSING path=%s テクスチャを読めませんでした", path.c_str());
                    }
                    pending->CreateData.*member = handle;
                    if (bNormal)
                    {
                        // クック済みの法線が BC5（2 チャンネル）なら印を付ける
                        const NorvesLib::RHI::ITexture *rhiTexture = handle.IsValid() ? pTextures->GetRHITexture(handle) : nullptr;
                        pending->CreateData.bNormalTwoChannel =
                            rhiTexture != nullptr && rhiTexture->GetFormat() == NorvesLib::RHI::Format::BC5_UNORM;
                    }
                    if (--pending->PendingTextureCount == 0)
                    {
                        onComplete();
                    }
                });
        }

        /// 材質 1 つ（無地の仮の材質）を作り、テクスチャの読み込みを始める。そろったら材質を更新する
        MaterialHandle CreateTexturedMaterial(GameModeContext &ctx, MaterialGalleryData &data,
                                              const Asset::AssetSystem *assetSystem, const GalleryMaterialSpec &spec)
        {
            auto pending = MakeShared<GalleryPendingMaterial>();
            pending->CreateData.DebugName = String("MaterialGallery_") + spec.AssetId;
            pending->CreateData.BaseColor[0] = kPlaceholderBaseColor[0];
            pending->CreateData.BaseColor[1] = kPlaceholderBaseColor[1];
            pending->CreateData.BaseColor[2] = kPlaceholderBaseColor[2];
            pending->CreateData.Metallic = 0.0f;
            pending->CreateData.Roughness = 0.8f;
            pending->CreateData.HeightScale = 0.0f;
            MaterialResources &materials = ctx.RenderResourcesRef.Materials();
            pending->TargetMaterial = materials.Create(pending->CreateData);
            if (!pending->TargetMaterial.IsValid())
            {
                LOG_ERROR_F("material-gallery: 材質 %s を作れませんでした", spec.AssetId);
                return MaterialHandle{};
            }
            ctx.ScopeRef.TrackMaterial(pending->TargetMaterial);
            data.m_PendingMaterials.push_back(pending);
            ++data.m_PendingMaterialCount;

            const bool bHeight = spec.HeightDepthMeters > 0.0f;
            const String albedoPath = spec.bCalibratedAlbedo ? MakeTexturePath(spec, "diff_cal", ".png")
                                                             : MakeTexturePath(spec, "diff", ".jpg");
            const char *sourceExtension = spec.Resolution[0] == '8' ? ".png" : ".jpg";
            const String normalPath = MakeTexturePath(spec, "nor_dx", sourceExtension);
            const String ormPath = MakeTexturePath(spec, "orm", "");
            const String heightPath = MakeTexturePath(spec, "disp", sourceExtension);

            // テクスチャが来たら、色は白（テクスチャの色そのまま）、金属度・粗さ・AO は ORM から読む
            pending->CreateData.BaseColor[0] = 1.0f;
            pending->CreateData.BaseColor[1] = 1.0f;
            pending->CreateData.BaseColor[2] = 1.0f;
            pending->CreateData.Metallic = -1.0f;
            pending->CreateData.Roughness = -1.0f;
            pending->CreateData.HeightScale = bHeight ? spec.HeightDepthMeters / spec.TileMeters : 0.0f;
            pending->PendingTextureCount = bHeight ? 4u : 3u;

            MaterialGalleryData *pData = &data;
            MaterialResources *pMaterials = &materials;
            const TSharedPtr<GalleryPendingMaterial> pendingCopy = pending;
            const auto onComplete = [pData, pendingCopy, pMaterials]()
            {
                // 色が欠けたら仮の灰色に戻す（白のままにしない）
                if (!pendingCopy->CreateData.AlbedoTexture.IsValid())
                {
                    pendingCopy->CreateData.BaseColor[0] = kPlaceholderBaseColor[0];
                    pendingCopy->CreateData.BaseColor[1] = kPlaceholderBaseColor[1];
                    pendingCopy->CreateData.BaseColor[2] = kPlaceholderBaseColor[2];
                }
                if (!pendingCopy->CreateData.ORMTexture.IsValid())
                {
                    pendingCopy->CreateData.Metallic = 0.0f;
                    pendingCopy->CreateData.Roughness = 0.8f;
                }
                if (!pendingCopy->CreateData.HeightTexture.IsValid())
                {
                    pendingCopy->CreateData.HeightScale = 0.0f;
                }
                pMaterials->Update(pendingCopy->TargetMaterial, pendingCopy->CreateData);
                if (pData->m_PendingMaterialCount > 0)
                {
                    --pData->m_PendingMaterialCount;
                }
            };
            TextureResources &textures = ctx.RenderResourcesRef.Textures();
            LoadGalleryTexture(textures, assetSystem, pending, albedoPath, &MaterialCreateData::AlbedoTexture, false,
                               onComplete);
            LoadGalleryTexture(textures, assetSystem, pending, normalPath, &MaterialCreateData::NormalTexture, true,
                               onComplete);
            LoadGalleryTexture(textures, assetSystem, pending, ormPath, &MaterialCreateData::ORMTexture, false,
                               onComplete);
            if (bHeight)
            {
                LoadGalleryTexture(textures, assetSystem, pending, heightPath, &MaterialCreateData::HeightTexture, false,
                                   onComplete);
            }
            return pending->TargetMaterial;
        }

        void AddLabel(MaterialGalleryData &data, const Vec3 &position, const char *text)
        {
            GalleryLabel label;
            label.Position[0] = position.X;
            label.Position[1] = position.Y;
            label.Position[2] = position.Z;
            label.Text = text;
            data.m_Labels.push_back(label);
        }

        bool CreateFloor(GameModeContext &ctx, MaterialGalleryData &data, const Asset::AssetSystem *assetSystem)
        {
            const GalleryMaterialSpec floorSpec{kFloorAssetId, "4k", kFloorTileMeters, kFloorHeightDepthMeters, false};
            const MaterialHandle material = CreateTexturedMaterial(ctx, data, assetSystem, floorSpec);
            if (!material.IsValid())
            {
                return false;
            }
            VariableArray<Mesh3DVertex> vertices;
            VariableArray<uint32_t> indices;
            ProceduralMeshGenerator::GeneratePlane(kFloorSizeX, kFloorSizeZ, 1, 1, vertices, indices);
            for (Mesh3DVertex &vertex : vertices)
            {
                // 世界の x・z を床の 1 枚の実寸で割った値
                vertex.TexCoord[0] = (vertex.Position[0] + kFloorCenterX) / kFloorTileMeters;
                vertex.TexCoord[1] = (vertex.Position[2] + kFloorCenterZ) / kFloorTileMeters;
            }
            return RegisterMesh(ctx, kFloorMeshHandleId, vertices, indices) &&
                   SpawnMesh(ctx, kFloorMeshHandleId, material, {kFloorCenterX, 0.0f, kFloorCenterZ}, false);
        }

        bool CreateExhibits(GameModeContext &ctx, MaterialGalleryData &data, const Asset::AssetSystem *assetSystem,
                            MaterialHandle pedestalMaterial)
        {
            VariableArray<Mesh3DVertex> vertices;
            VariableArray<uint32_t> indices;
            BuildFacingBox(kPedestalWidth, kPedestalHeight, kPedestalDepth, vertices, indices);
            if (!RegisterMesh(ctx, kPedestalMeshHandleId, vertices, indices))
            {
                return false;
            }
            ProceduralMeshGenerator::GenerateUVSphere(kExhibitSphereRadius, 64, 32, vertices, indices);
            if (!RegisterMesh(ctx, kExhibitSphereMeshHandleId, vertices, indices))
            {
                return false;
            }

            const float tilt = kPlateTiltDegrees * kDegreesToRadians;
            for (uint32_t row = 0; row < kRowCount; ++row)
            {
                const uint32_t begin = kRowStarts[row];
                const uint32_t end = row + 1 < kRowCount ? kRowStarts[row + 1] : kGalleryMaterialCount;
                const uint32_t count = end - begin;
                const float rowZ = static_cast<float>(row) * kRowPitchZ;
                for (uint32_t column = 0; column < count; ++column)
                {
                    const uint32_t materialIndex = begin + column;
                    const GalleryMaterialSpec &spec = kGalleryMaterials[materialIndex];
                    // 列の中の番号 0 を画面の左（+X）に置き、列の中央を x = 0 にそろえる
                    const float x = (0.5f * static_cast<float>(count - 1) - static_cast<float>(column)) * kPedestalPitchX;
                    const MaterialHandle material = CreateTexturedMaterial(ctx, data, assetSystem, spec);
                    if (!material.IsValid())
                    {
                        return false;
                    }
                    const uint32_t plateMeshId = kPlateMeshHandleBase + materialIndex;
                    BuildPlate(spec.TileMeters, vertices, indices);
                    if (!RegisterMesh(ctx, plateMeshId, vertices, indices))
                    {
                        return false;
                    }
                    // 板の下の縁を台座の上の面の手前に載せる（板の中心は下の縁から板を半分上った所）
                    const Vec3 plateCenter{x + kPlateOffsetX,
                                           kPedestalHeight + 0.5f * kPlateSize * std::sin(tilt) +
                                               0.5f * kPlateThickness * std::cos(tilt),
                                           rowZ + kPlateBottomOffsetZ + 0.5f * kPlateSize * std::cos(tilt)};
                    const Vec3 sphereCenter{x + kExhibitSphereOffsetX, kPedestalHeight + kExhibitSphereRadius,
                                            rowZ + kExhibitSphereOffsetZ};
                    if (!SpawnMesh(ctx, kPedestalMeshHandleId, pedestalMaterial, {x, 0.5f * kPedestalHeight, rowZ}, true,
                                   kPedestalBaseColor) ||
                        !SpawnMesh(ctx, plateMeshId, material, plateCenter) ||
                        !SpawnMesh(ctx, kExhibitSphereMeshHandleId, material, sphereCenter))
                    {
                        return false;
                    }
                    // 名札を隠す箱: 台座と、その上の板（上の縁は高さ約 1.5 m）・球の範囲
                    GalleryOccluder occluder;
                    occluder.Min[0] = x - 0.5f * kPedestalWidth;
                    occluder.Min[1] = 0.0f;
                    occluder.Min[2] = rowZ - 0.5f * kPedestalDepth;
                    occluder.Max[0] = x + 0.5f * kPedestalWidth;
                    occluder.Max[1] = kPedestalHeight + kPlateSize * std::sin(tilt) + kPlateThickness;
                    occluder.Max[2] = rowZ + 0.5f * kPedestalDepth;
                    data.m_Occluders.push_back(occluder);
                    // 名札は台座の前面の上寄り
                    AddLabel(data, {x, kPedestalHeight * 0.75f, rowZ - 0.5f * kPedestalDepth - 0.01f}, spec.AssetId);
                }
            }
            return true;
        }

        bool CreateChart(GameModeContext &ctx, MaterialGalleryData &data, MaterialHandle pedestalMaterial)
        {
            VariableArray<Mesh3DVertex> vertices;
            VariableArray<uint32_t> indices;
            ProceduralMeshGenerator::GenerateUVSphere(kChartSphereRadius, 64, 32, vertices, indices);
            if (!RegisterMesh(ctx, kChartSphereMeshHandleId, vertices, indices))
            {
                return false;
            }
            const float chartWidth = static_cast<float>(kChartSteps) * kChartPitch + 0.4f;
            for (uint32_t step = 0; step < kChartSteps; ++step)
            {
                const float stepHeight = kChartFirstStepHeight + static_cast<float>(step) * kChartStepRise;
                const float stepZ = kChartFirstStepZ + static_cast<float>(step) * kChartPitch;
                BuildFacingBox(chartWidth, stepHeight, kChartPitch, vertices, indices);
                const uint32_t stepMeshId = kStepMeshHandleBase + step;
                if (!RegisterMesh(ctx, stepMeshId, vertices, indices) ||
                    !SpawnMesh(ctx, stepMeshId, pedestalMaterial, {kChartCenterX, 0.5f * stepHeight, stepZ}, true,
                               kPedestalBaseColor))
                {
                    return false;
                }
                const float roughness = static_cast<float>(step) / static_cast<float>(kChartSteps - 1);
                for (uint32_t column = 0; column < kChartSteps; ++column)
                {
                    const float metallic = static_cast<float>(column) / static_cast<float>(kChartSteps - 1);
                    const MaterialHandle material =
                        CreatePlainMaterial(ctx, "MaterialGalleryChart", kChartBaseColor, metallic, roughness);
                    const float x =
                        kChartCenterX + (0.5f * static_cast<float>(kChartSteps - 1) - static_cast<float>(column)) * kChartPitch;
                    if (!material.IsValid() ||
                        !SpawnMesh(ctx, kChartSphereMeshHandleId, material, {x, stepHeight + kChartSphereRadius, stepZ}, true,
                                   kChartBaseColor))
                    {
                        return false;
                    }
                    if (step == 0)
                    {
                        // 金属度の名札は 1 段目の前面
                        AddLabel(data, {x, 0.5f * kChartFirstStepHeight, kChartFirstStepZ - 0.5f * kChartPitch - 0.01f},
                                 kMetallicLabels[column]);
                    }
                }
                // 粗さの名札は段の右の端（-X）の前寄りの上
                AddLabel(data, {kChartCenterX - 0.5f * chartWidth + 0.05f, stepHeight + 0.05f, stepZ - 0.35f},
                         kRoughnessLabels[step]);
            }
            return true;
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
        void CancelPendingMaterials(MaterialGalleryData &data)
        {
            for (const TSharedPtr<GalleryPendingMaterial> &pending : data.m_PendingMaterials)
            {
                pending->bCancelled = true;
            }
            data.m_PendingMaterials.clear();
            data.m_PendingMaterialCount = 0;
        }

        /// eye から方向 dir（正規化済み）へ進む線分 [0, maxDistance] が箱を通るか（軸ごとの区間を重ねるスラブ法）
        bool SegmentHitsBox(const Vec3 &eye, const Vec3 &dir, float maxDistance, const GalleryOccluder &box)
        {
            const float origin[3] = {eye.X, eye.Y, eye.Z};
            const float direction[3] = {dir.X, dir.Y, dir.Z};
            float enter = 0.0f;
            float exit = maxDistance;
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                if (std::fabs(direction[axis]) < 1.0e-6f)
                {
                    if (origin[axis] < box.Min[axis] || origin[axis] > box.Max[axis])
                    {
                        return false;
                    }
                    continue;
                }
                float t0 = (box.Min[axis] - origin[axis]) / direction[axis];
                float t1 = (box.Max[axis] - origin[axis]) / direction[axis];
                if (t0 > t1)
                {
                    const float swap = t0;
                    t0 = t1;
                    t1 = swap;
                }
                enter = t0 > enter ? t0 : enter;
                exit = t1 < exit ? t1 : exit;
                if (enter > exit)
                {
                    return false;
                }
            }
            return true;
        }

        void UpdateLabelCamera(MaterialGalleryData &data)
        {
            const Component::CameraComponent *camera = data.m_Rig.GetCamera();
            if (camera == nullptr)
            {
                data.m_LabelCamera.bValid = false;
                return;
            }
            const NorvesLib::Core::Input::FreeFlyCameraController &controller = data.m_Rig.GetController();
            const NorvesLib::Math::Vector3 position = controller.GetPosition();
            data.m_LabelCamera.Position[0] = position.x;
            data.m_LabelCamera.Position[1] = position.y;
            data.m_LabelCamera.Position[2] = position.z;
            data.m_LabelCamera.YawDegrees = controller.GetYaw();
            data.m_LabelCamera.PitchDegrees = controller.GetPitch();
            data.m_LabelCamera.FieldOfViewDegrees = camera->GetFieldOfView();
            data.m_LabelCamera.bValid = true;
        }
    } // namespace

#if defined(NORVES_ENABLE_IMGUI)
    void MaterialGalleryLabelView::OnImGui()
    {
        if (m_pLabels == nullptr || m_pCamera == nullptr || !m_pCamera->bValid)
        {
            return;
        }
        ImGuiIO &io = ImGui::GetIO();
        const ImVec2 display = io.DisplaySize;
        if (display.x <= 0.0f || display.y <= 0.0f || io.Fonts->Fonts.Size == 0)
        {
            return;
        }
        const int fontIndex = static_cast<int>(NorvesLib::Modules::Gui::ImGuiFontSlot::DisplayCaption);
        ImFont *font = fontIndex < io.Fonts->Fonts.Size && io.Fonts->Fonts[fontIndex] != nullptr ? io.Fonts->Fonts[fontIndex]
                                                                                                  : io.Fonts->Fonts[0];

        // FreeFlyCameraController の向き: 前 = (-sinYaw cosPitch, sinPitch, cosYaw cosPitch)、右 = (-cosYaw, 0, -sinYaw)
        const float yaw = m_pCamera->YawDegrees * kDegreesToRadians;
        const float pitch = m_pCamera->PitchDegrees * kDegreesToRadians;
        const Vec3 forward{-std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
        const Vec3 right{-std::cos(yaw), 0.0f, -std::sin(yaw)};
        const Vec3 up = Cross(right, forward);
        const Vec3 eye{m_pCamera->Position[0], m_pCamera->Position[1], m_pCamera->Position[2]};
        const float tanHalfFov = std::tan(0.5f * m_pCamera->FieldOfViewDegrees * kDegreesToRadians);
        const float aspect = display.x / display.y;
        // 名札を出す最大の距離[m]と、文字の大きさ（窓の高さに対する割合。距離 4 m で基準の大きさ）
        constexpr float kMaxDistance = 14.0f;
        constexpr float kReferenceDistance = 4.0f;
        constexpr float kReferenceHeightRatio = 0.03f;
        constexpr float kMinHeightRatio = 0.011f;
        constexpr float kMaxHeightRatio = 0.034f;

        // 遠い名札から描く（近い名札が手前に重なる）
        struct Projected
        {
            float X;
            float Y;
            float Distance;
            const char *Text;
        };
        VariableArray<Projected> projected;
        projected.reserve(m_pLabels->size());
        for (const GalleryLabel &label : *m_pLabels)
        {
            const Vec3 offset{label.Position[0] - eye.X, label.Position[1] - eye.Y, label.Position[2] - eye.Z};
            const float depth = Dot(offset, forward);
            const float distance = std::sqrt(Dot(offset, offset));
            if (label.Text == nullptr || depth < 0.3f || distance > kMaxDistance)
            {
                continue;
            }
            const float ndcX = Dot(offset, right) / (depth * tanHalfFov * aspect);
            const float ndcY = Dot(offset, up) / (depth * tanHalfFov);
            if (ndcX < -1.2f || ndcX > 1.2f || ndcY < -1.2f || ndcY > 1.2f)
            {
                continue;
            }
            // 名札より 5 cm 以上手前で視線を遮る展示があれば描かない（名札の載る台座の箱は名札の後ろから始まる）
            bool bOccluded = false;
            if (m_pOccluders != nullptr)
            {
                const Vec3 direction = Scale(offset, 1.0f / distance);
                for (const GalleryOccluder &occluder : *m_pOccluders)
                {
                    if (SegmentHitsBox(eye, direction, distance - 0.05f, occluder))
                    {
                        bOccluded = true;
                        break;
                    }
                }
            }
            if (bOccluded)
            {
                continue;
            }
            Projected item{(0.5f + 0.5f * ndcX) * display.x, (0.5f - 0.5f * ndcY) * display.y, distance, label.Text};
            uint32_t insertAt = static_cast<uint32_t>(projected.size());
            projected.push_back(item);
            while (insertAt > 0 && projected[insertAt - 1].Distance < item.Distance)
            {
                projected[insertAt] = projected[insertAt - 1];
                --insertAt;
            }
            projected[insertAt] = item;
        }

        ImDrawList *drawList = ImGui::GetBackgroundDrawList();
        for (const Projected &item : projected)
        {
            float ratio = kReferenceHeightRatio * kReferenceDistance / item.Distance;
            ratio = ratio < kMinHeightRatio ? kMinHeightRatio : (ratio > kMaxHeightRatio ? kMaxHeightRatio : ratio);
            const float fontSize = ratio * display.y;
            const ImVec2 textSize = font->CalcTextSizeA(fontSize, 1.0e6f, 0.0f, item.Text);
            const float padX = fontSize * 0.35f;
            const float padY = fontSize * 0.15f;
            const ImVec2 textPos(item.X - 0.5f * textSize.x, item.Y - 0.5f * textSize.y);
            drawList->AddRectFilled(ImVec2(textPos.x - padX, textPos.y - padY),
                                    ImVec2(textPos.x + textSize.x + padX, textPos.y + textSize.y + padY),
                                    IM_COL32(16, 16, 20, 200), fontSize * 0.2f);
            drawList->AddText(font, fontSize, textPos, IM_COL32(242, 238, 228, 255), item.Text);
        }
    }
#endif

    GameModeEnterResult MaterialGalleryRoutine::Enter(GameModeContext &ctx, MaterialGalleryData &data)
    {
        LOG_INFO("material-gallery を開始します");
        data.m_TickCount = 0;
        data.m_bMaterialsReadyLogged = false;
        data.m_Labels.clear();
        data.m_Occluders.clear();
        Game::Scenes::BeginSceneAssembly(ctx.EngineRef);

        const Game::Scenes::SceneDescriptor *scene = Game::Scenes::FindScene(Game::Scenes::kMaterialGallerySceneId);
        Game::Scenes::FreeCameraPose pose{};
        if (scene == nullptr || !Game::Scenes::ResolveFreeCameraPose(*scene, data.m_ViewName.c_str(), pose))
        {
            LOG_ERROR("material-gallery: シーンの一覧に視点の定義がありません");
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
            LOG_WARNING("MATERIAL_GALLERY_TEXTURE_MISSING AssetSystem が無いため、テクスチャを読まずに無地で描きます");
        }

        ApplyEnvironment(ctx);
        const MaterialHandle pedestalMaterial =
            CreatePlainMaterial(ctx, "MaterialGalleryPedestal", kPedestalBaseColor, 0.0f, kPedestalRoughness);
        if (!pedestalMaterial.IsValid() || !CreateFloor(ctx, data, assetSystem.get()) ||
            !CreateExhibits(ctx, data, assetSystem.get(), pedestalMaterial) || !CreateChart(ctx, data, pedestalMaterial))
        {
            CancelPendingMaterials(data);
            ResetEnvironment(ctx);
            Game::Scenes::EndSceneAssembly(ctx.EngineRef);
            return GameModeEnterResult::Failed;
        }
        LOG_INFO_F("MATERIAL_GALLERY_LAYOUT exhibits=%u chart_spheres=%u labels=%u", kGalleryMaterialCount,
                   kChartSteps * kChartSteps, static_cast<uint32_t>(data.m_Labels.size()));

        data.m_Rig.ConfigurePath(scene->CameraPath, data.m_CameraPathTime, data.m_bCameraPathPlay);
        if (!data.m_Rig.Enter(ctx, pose, data.m_CameraScriptPath.c_str()))
        {
            LOG_ERROR("material-gallery: カメラを作れませんでした");
            CancelPendingMaterials(data);
            ResetEnvironment(ctx);
            Game::Scenes::EndSceneAssembly(ctx.EngineRef);
            return GameModeEnterResult::Failed;
        }

        Component::CameraComponent *camera = data.m_Rig.GetCamera();
        camera->SetAperture(16.0f);
        camera->SetISO(100.0f);
        camera->SetShutterSpeed(ComputeShutterSpeedForEV100(camera->GetAperture(), camera->GetISO(), kExposureEV100));
        camera->SetExposureCompensation(0.0f);
        camera->SetAntiAliasingMode(CameraAntiAliasingMode::FXAA);
        UpdateLabelCamera(data);
#if defined(NORVES_ENABLE_IMGUI)
        if (Module::GetModuleRegistry().FindModule(Identity("NorvesImGuiModule")) != nullptr)
        {
            NorvesLib::Modules::Gui::RegisterImGuiView(&data.m_LabelView);
            // 名札はシーンの一部として撮影にも載せる
            NorvesLib::Modules::Gui::SetImGuiOverlayInCapture(true);
            data.m_bLabelViewRegistered = true;
        }
#endif
        return GameModeEnterResult::Succeeded;
    }

    void MaterialGalleryRoutine::Tick(GameModeContext &ctx, MaterialGalleryData &data, float deltaTime)
    {
        ++data.m_TickCount;
        // 撮影の視点の切り替えは、このフレームのカメラを渡す前に置き直す
        if (const Game::Scenes::SceneDescriptor *scene = Game::Scenes::FindScene(Game::Scenes::kMaterialGallerySceneId))
        {
            data.m_Rig.ApplyRequestedView(data.m_pViewSwitch, *scene);
        }
        const bool bSuspended = data.m_pPauseMenu != nullptr && data.m_pPauseMenu->IsOpen();
        data.m_Rig.Tick(ctx, deltaTime, bSuspended);
        UpdateLabelCamera(data);

        const bool bMaterialsReady = data.m_PendingMaterialCount == 0;
        if (bMaterialsReady && !data.m_bMaterialsReadyLogged)
        {
            LOG_INFO_F("MATERIAL_GALLERY_MATERIALS_READY materials=%u tick=%u",
                       static_cast<uint32_t>(data.m_PendingMaterials.size()), data.m_TickCount);
            data.m_bMaterialsReadyLogged = true;
        }
        // 材質のテクスチャがそろい、台本を流し終えるまで、決定的な撮影を待たせる
        const bool bReady = bMaterialsReady && data.m_TickCount >= kReadyTickCount && !data.m_Rig.IsScriptRunning();
        if (bReady)
        {
            data.m_Rig.NotifySceneReady();
        }
        Game::Scenes::UpdateSceneAssembly(ctx.EngineRef, Game::Scenes::kMaterialGallerySceneId, bReady);
    }

    void MaterialGalleryRoutine::Leave(GameModeContext &ctx, MaterialGalleryData &data, GameModeExitReason)
    {
        Game::Scenes::EndSceneAssembly(ctx.EngineRef);
        CancelPendingMaterials(data);
#if defined(NORVES_ENABLE_IMGUI)
        if (data.m_bLabelViewRegistered)
        {
            NorvesLib::Modules::Gui::UnregisterImGuiView(&data.m_LabelView);
            NorvesLib::Modules::Gui::SetImGuiOverlayInCapture(false);
            data.m_bLabelViewRegistered = false;
        }
#endif
        data.m_Labels.clear();
        data.m_Occluders.clear();
        data.m_LabelCamera.bValid = false;
        data.m_Rig.Leave(ctx);
        // 床・台座・板・球・材質・カメラの実体は GameModeScope::Cleanup が返す。空は RenderWorld の設定なので戻す。
        ResetEnvironment(ctx);
        LOG_INFO("material-gallery を終了します");
    }

} // namespace Game::GameModes

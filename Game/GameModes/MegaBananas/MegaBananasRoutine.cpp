#include "GameModes/MegaBananas/MegaBananasMode.h"

#include "Core/Public/Asset/AssetSystem.h"
#include "Core/Public/Asset/CookedMeshFormat.h"
#include "Core/Public/Component/CameraComponent.h"
#include "Core/Public/Component/MegaGeometryComponent.h"
#include "Core/Public/Component/MeshComponent.h"
#include "Core/Public/Debug/DebugConfig.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/GameMode/GameModeScope.h"
#include "Core/Public/Input/InputRouter.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Logging/Logger.h"
#include "Core/Public/Math/Quaternion.h"
#include "Core/Public/Math/Vector3.h"
#include "Core/Public/Module/ModuleRegistry.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/RHI/ITexture.h"
#include "Core/Public/Rendering/MaterialTypes.h"
#include "Core/Public/Rendering/MegaGeometry/CookedMeshMegaMeshAdapter.h"
#include "Core/Public/Rendering/MegaGeometry/CookedMeshPageSource.h"
#include "Core/Public/Rendering/ProceduralMeshGenerator.h"
#include "Core/Public/Rendering/RenderGPUTimingTypes.h"
#include "Core/Public/Rendering/RenderResources.h"
#include "Core/Public/Rendering/RenderWorld.h"
#include "Core/Public/Rendering/SkyAtmosphere.h"
#include "Core/Public/Rendering/VolumetricFog.h"
#include "GameModes/Rendering3DTest/SkySunControl.h"
#include "Scenes/SceneCatalog.h"
#include "Scenes/SceneReadiness.h"

#if defined(NORVES_ENABLE_IMGUI)
#include "ImGuiModule/IImGuiView.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Container;
using namespace NorvesLib::Core::GameMode;
using namespace NorvesLib::Core::Rendering;
using namespace NorvesLib::Core::Rendering::MegaGeometry;
namespace Math = NorvesLib::Math;

namespace Game::GameModes
{

    /// バナナのメッシュ・ページの読み込み元・材質のテクスチャ。Enter で用意し、材質がそろったら Tick が MegaMesh にして置く
    struct BananaAssembly
    {
        TSharedPtr<Asset::CookedMeshData> Mesh;
        /// ページ（NVMESH v1.1）の読み込み元。ページを 2 つ以上持つメッシュだけが持つ（無ければ全て常駐で作る）
        TSharedPtr<IGeometryPageSource> PageSource;
        TSharedPtr<BananasPendingMaterial> Material;
        String LogicalPath;
        /// ローカル X（ローカル Y を長手とするバナナの太さの向き）の最小・最大。床に横たえた高さを求める
        float ThicknessMin = 0.0f;
        float ThicknessMax = 0.0f;
    };

    bool BananasInfoToggle::OnKey(const NorvesLib::Core::Input::KeyEvent &event)
    {
        if (event.Code == NorvesLib::Core::Input::KeyCode::H && event.Action == NorvesLib::Core::Input::InputAction::Pressed)
        {
            m_bRequested = true;
        }
        return false;
    }

#if NORVES_BUILD_DEVELOPMENT
    void MegaOcclusionLogTap::OnLog(const NorvesLib::Core::Logging::LogEntry &entry)
    {
        // 例: "MEGA_OCCLUSION frame=62 epoch_frame=30 pass1=90717 pass2_tested=91702 pass2_drawn=0 occluded=985"
        // 描いた数 = 1 パス目（前のフレームで見えたもの）+ 2 パス目で新しく見えたもの。"MEGA_OCCLUSION_OFF" は拾わない
        const char *line = std::strstr(entry.message.c_str(), "MEGA_OCCLUSION frame=");
        if (line == nullptr)
        {
            return;
        }
        const char *pass1Text = std::strstr(line, "pass1=");
        const char *drawnText = std::strstr(line, "pass2_drawn=");
        if (pass1Text == nullptr || drawnText == nullptr)
        {
            return;
        }
        const unsigned long pass1 = std::strtoul(pass1Text + std::strlen("pass1="), nullptr, 10);
        const unsigned long pass2Drawn = std::strtoul(drawnText + std::strlen("pass2_drawn="), nullptr, 10);
        m_DrawnClusters.store(static_cast<uint32_t>(pass1 + pass2Drawn), std::memory_order_relaxed);
        m_bValid.store(true, std::memory_order_release);
    }
#endif

    namespace
    {
        // 昼の太陽。カメラの後ろ右から差して、影が手前から奥の左へ伸びる向き
        constexpr float kSunAltitudeDegrees = 35.0f;
        constexpr float kSunAzimuthDegrees = -55.0f;
        // 晴天の昼の手動露出（絞り f/16・ISO 100 のままシャッター速度で合わせる）
        constexpr float kExposureEV100 = 15.0f;
        // SceneReady を出すまでの Tick の数（組み立ての完了後に、描画が最初の絵を作る余裕）
        constexpr uint32_t kReadyTickCount = 3;

        // 床: 一辺 160 m の平面。木の床のテクスチャ 1 枚を 1.5 m 四方に貼る
        constexpr float kFloorSize = 160.0f;
        constexpr float kFloorTileMeters = 1.5f;
        // 他のシーンのメッシュの ID（100〜、200〜、900）と重ならない値
        constexpr uint32_t kFloorMeshHandleId = 910;
        // 床の材質（Poly Haven の brown_planks_03。Assets/AssetSets/Rendering3DTestStressTextures.json が焼く）
        constexpr const char *kFloorAlbedoPath = "Assets/Textures/PolyHaven/brown_planks_03/brown_planks_03_diff_4k.jpg";
        constexpr const char *kFloorNormalPath = "Assets/Textures/PolyHaven/brown_planks_03/brown_planks_03_nor_dx_4k.jpg";
        constexpr const char *kFloorOrmPath = "Assets/Textures/PolyHaven/brown_planks_03/brown_planks_03_orm_4k";

        // バナナ（Scripts/GenerateSceneModels.py が作り、Assets/AssetSets/MegaBananasModels.json が焼く）
        constexpr const char *kBananaMeshPath = "Assets/Models/Generated/banana/banana.glb";
        constexpr const char *kFetchHint = "python Scripts/GenerateSceneModels.py --ids banana";
        // 生成した GLB の内蔵画像の順（GLB の textures と同じ並び）: 0 = ベースカラー、1 = AO・粗さ・金属（ORM）、2 = 法線
        constexpr const char *kBananaAlbedoSuffix = ".img0.png";
        constexpr const char *kBananaOrmSuffix = ".img1.png";
        constexpr const char *kBananaNormalSuffix = ".img2.png";

        // 配置: 床に横たえたバナナの 1 本ぶんの升目（長さ約 0.2 m・反りで横にも約 0.15 m 張り出す。わずかな拡大と位置のばらつきを含めても
        // ほとんど重ならない大きさ）
        constexpr float kCellMeters = 0.31f;
        // 山は敷き詰めより密に積む（ばらつきが小さいので、この間隔でも重ならない）
        constexpr float kPileCellMeters = 0.29f;
        // 山（ピラミッド）: 底の一辺 7 本から 1 段ごとに 1 本減らして 6 段。段の高さはバナナの太さ（約 0.055 m）より少しだけ低くする
        constexpr int kPileSide = 7;
        constexpr int kPileLayers = 6;
        constexpr float kPileLayerStep = 0.05f;
        constexpr uint32_t kPileBananas = 49 + 36 + 25 + 16 + 9 + 4;
        constexpr int kPileCount = 3;
        // 山の中心（床の x・z）。底の一辺は約 2 m で、互いに重ならない間隔
        constexpr float kPileCenters[kPileCount][2] = {{-2.3f, 2.5f}, {0.2f, 3.5f}, {2.7f, 2.3f}};
        // 山の周りに敷き詰めを空ける範囲（山の底の半分の幅 + 余白）
        constexpr float kPileClearHalfExtent = 1.2f;
        // 山に使う本数の上限（全体の本数の 1/4 まで）
        constexpr uint32_t kMaxPileShareDivisor = 4;
        // 敷き詰めの手前の端の z
        constexpr float kFieldNearZ = -4.0f;
        // 床へわずかに沈める深さ（接地の隙間を隠す）
        constexpr float kSinkMeters = 0.002f;
        // 向き・大きさ・位置のばらつき
        constexpr float kScaleMin = 0.92f;
        constexpr float kScaleMax = 1.08f;
        // 敷き詰めは升目の並びが見えないよう大きめに、山は積んだ形を保つよう小さめにずらす
        constexpr float kFieldJitterMeters = 0.04f;
        constexpr float kPileJitterMeters = 0.012f;
        constexpr float kPi = 3.14159265358979323846f;

        // 整数のハッシュ（配置の乱数。番号だけで決まり、実行ごとに同じ）
        uint32_t HashIndex(uint32_t value)
        {
            value ^= value >> 16;
            value *= 0x7feb352du;
            value ^= value >> 15;
            value *= 0x846ca68bu;
            value ^= value >> 16;
            return value;
        }

        float UnitFloat(uint32_t hash)
        {
            return static_cast<float>(hash & 0xFFFFFFu) / 16777216.0f;
        }

        /// 昼の物理空と太陽
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

        /// 材質のテクスチャを 1 枚、非同期で読む。結果は主スレッドで member へ入れ、全部そろったら onComplete を呼ぶ
        template <typename OnComplete>
        void LoadMaterialTexture(TextureResources &textures, const TSharedPtr<BananasPendingMaterial> &update,
                                 const String &path, TextureHandle MaterialCreateData::*member, bool bNormal,
                                 OnComplete onComplete)
        {
            TextureResources *pTextures = &textures;
            textures.LoadTextureAsync(
                path,
                [pTextures, update, path, member, bNormal, onComplete](TextureHandle handle)
                {
                    if (update->bCancelled)
                    {
                        // シーンを出た後に届いた結果。パスで読んだテクスチャはキャッシュが所有するので、何もしない
                        return;
                    }
                    if (!handle.IsValid())
                    {
                        LOG_WARNING_F("MEGA_BANANAS_TEXTURE_FAILED path=%s テクスチャを読めませんでした", path.c_str());
                    }
                    update->CreateData.*member = handle;
                    if (bNormal)
                    {
                        // クック済みの法線が BC5（2 チャンネル）なら印を付ける。無圧縮の RGBA8 なら付けない
                        const NorvesLib::RHI::ITexture *rhiTexture = handle.IsValid() ? pTextures->GetRHITexture(handle) : nullptr;
                        update->CreateData.bNormalTwoChannel =
                            rhiTexture != nullptr && rhiTexture->GetFormat() == NorvesLib::RHI::Format::BC5_UNORM;
                    }
                    if (--update->PendingTextureCount == 0)
                    {
                        onComplete();
                    }
                });
        }

        /// 床: 木の床のテクスチャを貼った平面。テクスチャは非同期で読み、そろうまでは茶色の無地で描く
        bool CreateFloor(GameModeContext &ctx, MegaBananasData &data)
        {
            auto &textures = ctx.RenderResourcesRef.Textures();
            auto &materials = ctx.RenderResourcesRef.Materials();

            auto pending = MakeShared<BananasPendingMaterial>();
            pending->CreateData.DebugName = "MegaBananasFloor";
            pending->CreateData.BaseColor[0] = 0.32f;
            pending->CreateData.BaseColor[1] = 0.2f;
            pending->CreateData.BaseColor[2] = 0.11f;
            pending->CreateData.Metallic = 0.0f;
            pending->CreateData.Roughness = 0.8f;
            pending->CreateData.HeightScale = 0.0f;
            pending->TargetMaterial = materials.Create(pending->CreateData);
            if (!pending->TargetMaterial.IsValid())
            {
                LOG_ERROR("mega-bananas: 床の材質を作れませんでした");
                return false;
            }
            ctx.ScopeRef.TrackMaterial(pending->TargetMaterial);
            data.m_pFloorMaterial = pending;

            NorvesLib::Core::Container::VariableArray<Mesh3DVertex> vertices;
            NorvesLib::Core::Container::VariableArray<uint32_t> indices;
            ProceduralMeshGenerator::GeneratePlane(kFloorSize, kFloorSize, 1, 1, vertices, indices);
            for (Mesh3DVertex &vertex : vertices)
            {
                // 世界の x・z を板 1 枚の実寸で割った値
                vertex.TexCoord[0] = vertex.Position[0] / kFloorTileMeters;
                vertex.TexCoord[1] = vertex.Position[2] / kFloorTileMeters;
            }
            const MeshDataHandle mesh{kFloorMeshHandleId};
            if (!ctx.RenderResourcesRef.Meshes().Register(mesh, vertices.data(),
                                                          static_cast<uint32_t>(vertices.size() * sizeof(Mesh3DVertex)),
                                                          indices.data(), static_cast<uint32_t>(indices.size())))
            {
                LOG_ERROR("mega-bananas: 床のメッシュを登録できませんでした");
                return false;
            }
            ctx.ScopeRef.TrackMesh(mesh);

            Entity *floor = ctx.ScopeRef.SpawnObject<Entity>();
            if (floor == nullptr)
            {
                return false;
            }
            floor->SetPosition(0.0f, 0.0f, 0.0f);
            auto *meshComponent = ctx.WorldRef.CreateComponent<Component::MeshComponent>(floor);
            if (meshComponent == nullptr)
            {
                return false;
            }
            meshComponent->SetMeshHandle(mesh);
            meshComponent->SetMaterial(0, pending->TargetMaterial);
            meshComponent->SetCastShadow(false);

            // 材質のテクスチャ（色・法線・ORM）。そろったら材質を更新する。読み込みに失敗した枠は無地のまま
            pending->PendingTextureCount = 3;
            MegaBananasData *pData = &data;
            const TSharedPtr<BananasPendingMaterial> pendingCopy = pending;
            MaterialResources *pMaterials = &materials;
            const auto onComplete = [pData, pendingCopy, pMaterials]()
            {
                pMaterials->Update(pendingCopy->TargetMaterial, pendingCopy->CreateData);
                pData->m_bFloorReady = true;
                LOG_INFO("MEGA_BANANAS_FLOOR_READY 床の材質のテクスチャを読み込みました");
            };
            LoadMaterialTexture(textures, pending, String(kFloorAlbedoPath), &MaterialCreateData::AlbedoTexture, false,
                                onComplete);
            LoadMaterialTexture(textures, pending, String(kFloorNormalPath), &MaterialCreateData::NormalTexture, true,
                                onComplete);
            LoadMaterialTexture(textures, pending, String(kFloorOrmPath), &MaterialCreateData::ORMTexture, false,
                                onComplete);
            return true;
        }

        /// バナナのクック済みメッシュを解決・解析し、材質のテクスチャの読み込みを始める。
        /// 資産が無い・壊れているときは理由を警告して false を返す（シーンは床だけで続ける）
        bool StartBananaLoad(GameModeContext &ctx, MegaBananasData &data)
        {
            if (!data.m_GetAssetSystem.IsBound())
            {
                LOG_WARNING("MEGA_BANANAS_ASSET_MISSING AssetSystem が無いためバナナを置きません");
                return false;
            }
            const TSharedPtr<const Asset::AssetSystem> assetSystem = data.m_GetAssetSystem.Invoke();
            if (!assetSystem)
            {
                LOG_WARNING("MEGA_BANANAS_ASSET_MISSING AssetSystem が無いためバナナを置きません");
                return false;
            }

            const Asset::AssetResolveResult resolved = assetSystem->ResolveAsset(kBananaMeshPath, Asset::AssetKind::Model);
            if (!resolved.UsedCooked())
            {
                LOG_WARNING_F("MEGA_BANANAS_ASSET_MISSING path=%s クック済みのバナナがありません。"
                              "%s で生成し、CookAssets で焼いてください",
                              kBananaMeshPath, kFetchHint);
                return false;
            }
            const String albedoPath = String(kBananaMeshPath) + kBananaAlbedoSuffix;
            const String ormPath = String(kBananaMeshPath) + kBananaOrmSuffix;
            const String normalPath = String(kBananaMeshPath) + kBananaNormalSuffix;
            for (const String *path : {&albedoPath, &ormPath, &normalPath})
            {
                if (!assetSystem->FindCookedVariant(AnsiString(path->c_str()), Asset::AssetKind::Texture).ShouldUseCooked())
                {
                    LOG_WARNING_F("MEGA_BANANAS_ASSET_MISSING path=%s バナナのクック済みのテクスチャがありません。"
                                  "CookAssets で焼いてください",
                                  path->c_str());
                    return false;
                }
            }
            Asset::CookedMeshParseResult parsed = Asset::ParseCookedMesh(resolved.Blob);
            if (!parsed.Succeeded() || parsed.Mesh.Clusters.empty())
            {
                LOG_WARNING_F("MEGA_BANANAS_ASSET_INVALID path=%s status=%u クック済みのバナナを解析できません",
                              kBananaMeshPath, static_cast<unsigned int>(parsed.Status));
                return false;
            }

            auto assembly = MakeShared<BananaAssembly>();
            assembly->LogicalPath = kBananaMeshPath;
            {
                Asset::AssetCookedRange pageRange;
                if (parsed.Mesh.Pages.size() >= 2 && Asset::AssetSystem::TryMakeCookedRange(resolved, pageRange))
                {
                    assembly->PageSource = MakeCookedMeshPageSource(
                        parsed.Mesh, assetSystem->GetCookedFileReader(), pageRange.Request, pageRange.BaseOffset);
                }
            }
            assembly->Mesh = MakeShared<Asset::CookedMeshData>(std::move(parsed.Mesh));

            // 床に横たえたときの高さを、頂点の太さの範囲から求める
            float minX = std::numeric_limits<float>::max();
            float maxX = std::numeric_limits<float>::lowest();
            for (const Asset::CookedMeshVertex &vertex : assembly->Mesh->Vertices)
            {
                minX = std::min(minX, vertex.Position.X);
                maxX = std::max(maxX, vertex.Position.X);
            }
            assembly->ThicknessMin = minX;
            assembly->ThicknessMax = maxX;

            LOG_INFO_F("MEGA_BANANAS_MESH path=%s vertices=%u indices=%u clusters=%u lod_levels=%u pages=%zu "
                       "thickness=[%.4f,%.4f]",
                       kBananaMeshPath, static_cast<unsigned int>(assembly->Mesh->Vertices.size()),
                       static_cast<unsigned int>(assembly->Mesh->Indices.size()),
                       static_cast<unsigned int>(assembly->Mesh->Clusters.size()),
                       static_cast<unsigned int>(assembly->Mesh->LODLevelCount), assembly->Mesh->Pages.size(),
                       static_cast<double>(minX), static_cast<double>(maxX));

            // 材質のテクスチャ。そろったら Tick が MegaMesh を作る
            auto pending = MakeShared<BananasPendingMaterial>();
            pending->CreateData.DebugName = "Banana";
            pending->PendingTextureCount = 3;
            assembly->Material = pending;
            auto &textures = ctx.RenderResourcesRef.Textures();
            const auto onComplete = []() {};
            LoadMaterialTexture(textures, pending, albedoPath, &MaterialCreateData::AlbedoTexture, false, onComplete);
            LoadMaterialTexture(textures, pending, ormPath, &MaterialCreateData::ORMTexture, false, onComplete);
            LoadMaterialTexture(textures, pending, normalPath, &MaterialCreateData::NormalTexture, true, onComplete);
            data.m_pAssembly = assembly;
            return true;
        }

        /// 1 本のバナナを床に横たえて置く。長手（ローカル Y）を水平にし、反りの面も水平にして、太さ（ローカル X）を縦にする
        void SpawnBanana(GameModeContext &ctx, MegaMeshHandle megaMesh, const BananaAssembly &assembly, float x,
                         float baseY, float z, float jitter, uint32_t seed)
        {
            const float yaw = UnitFloat(HashIndex(seed * 4u + 1u)) * 2.0f * kPi;
            const float scale = kScaleMin + (kScaleMax - kScaleMin) * UnitFloat(HashIndex(seed * 4u + 2u));
            const float jitterX = (UnitFloat(HashIndex(seed * 4u + 3u)) - 0.5f) * 2.0f * jitter;
            const float jitterZ = (UnitFloat(HashIndex(seed * 4u + 4u)) - 0.5f) * 2.0f * jitter;
            // 太さの向きが縦になるので、中心の高さ = 太さの半分（拡大後）。中心が原点でなくても、最下点が床に接する
            const float halfThickness = 0.5f * (assembly.ThicknessMax - assembly.ThicknessMin);
            const float centerOffset = 0.5f * (assembly.ThicknessMax + assembly.ThicknessMin);
            const float y = baseY + (halfThickness + centerOffset) * scale - kSinkMeters;

            const Math::Vector3 yAxis(0.0f, 1.0f, 0.0f);
            const Math::Vector3 zAxis(0.0f, 0.0f, 1.0f);
            const Math::Quaternion roll(zAxis, 0.5f * kPi);
            const Math::Quaternion turn(yAxis, yaw);
            const Math::Quaternion rotation = turn * roll;

            Entity *object = ctx.ScopeRef.SpawnObject<Entity>();
            if (object == nullptr)
            {
                return;
            }
            object->SetPosition(x + jitterX, y, z + jitterZ);
            object->SetScale(scale, scale, scale);
            object->SetRotation(rotation);
            auto *component = ctx.WorldRef.CreateComponent<Component::MegaGeometryComponent>(object);
            if (component == nullptr)
            {
                return;
            }
            component->SetMegaMeshHandle(megaMesh);
            component->SetCastShadow(true);
        }

        /// 最も細かい段（LOD 0）の三角形数
        uint32_t CountLod0Triangles(const Asset::CookedMeshData &mesh)
        {
            uint64_t indexCount = 0;
            for (const Asset::CookedMeshCluster &cluster : mesh.Clusters)
            {
                if (cluster.LODLevel == 0)
                {
                    indexCount += cluster.IndexCount;
                }
            }
            return static_cast<uint32_t>(indexCount / 3);
        }

        /// count 本を、手前に山・その周りに敷き詰めで置く。置いた本数を返す
        uint32_t PlaceBananas(GameModeContext &ctx, MegaMeshHandle megaMesh, const BananaAssembly &assembly,
                              uint32_t count, uint32_t &outPileCount)
        {
            uint32_t placed = 0;
            uint32_t seed = 0;
            const uint32_t pileBudget = std::min(kPileBananas * kPileCount, count / kMaxPileShareDivisor);

            // 山: 底から順に、各段を半升ずつずらして 1 段ずつ小さくする
            for (int pile = 0; pile < kPileCount && placed < pileBudget; ++pile)
            {
                for (int layer = 0; layer < kPileLayers && placed < pileBudget; ++layer)
                {
                    const int side = kPileSide - layer;
                    for (int row = 0; row < side && placed < pileBudget; ++row)
                    {
                        for (int column = 0; column < side && placed < pileBudget; ++column)
                        {
                            const float offsetX = (static_cast<float>(column) - 0.5f * static_cast<float>(side - 1)) * kPileCellMeters;
                            const float offsetZ = (static_cast<float>(row) - 0.5f * static_cast<float>(side - 1)) * kPileCellMeters;
                            SpawnBanana(ctx, megaMesh, assembly, kPileCenters[pile][0] + offsetX,
                                        kPileLayerStep * static_cast<float>(layer), kPileCenters[pile][1] + offsetZ,
                                        kPileJitterMeters, seed++);
                            ++placed;
                        }
                    }
                }
            }
            outPileCount = placed;

            // 敷き詰め: 手前の端から奥へ、横長の矩形を升目で埋める（山の下は空ける）
            const uint32_t fieldCount = count - placed;
            if (fieldCount > 0)
            {
                const uint32_t columns = static_cast<uint32_t>(std::ceil(std::sqrt(2.2f * static_cast<float>(count))));
                const float startX = -0.5f * static_cast<float>(columns - 1) * kCellMeters;
                uint32_t fieldPlaced = 0;
                for (uint32_t row = 0; fieldPlaced < fieldCount; ++row)
                {
                    const float z = kFieldNearZ + static_cast<float>(row) * kCellMeters;
                    for (uint32_t column = 0; column < columns && fieldPlaced < fieldCount; ++column)
                    {
                        const float x = startX + static_cast<float>(column) * kCellMeters;
                        bool bUnderPile = false;
                        for (int pile = 0; pile < kPileCount; ++pile)
                        {
                            if (std::fabs(x - kPileCenters[pile][0]) < kPileClearHalfExtent &&
                                std::fabs(z - kPileCenters[pile][1]) < kPileClearHalfExtent)
                            {
                                bUnderPile = true;
                                break;
                            }
                        }
                        if (bUnderPile)
                        {
                            continue;
                        }
                        SpawnBanana(ctx, megaMesh, assembly, x, 0.0f, z, kFieldJitterMeters, seed++);
                        ++fieldPlaced;
                        ++placed;
                    }
                }
            }
            return placed;
        }

        /// バナナの材質がそろったら MegaMesh を作り、モデルとして登録して、バナナを置く
        void FinishBananaLoad(GameModeContext &ctx, MegaBananasData &data)
        {
            BananaAssembly &assembly = *data.m_pAssembly;
            const MaterialCreateData &material = assembly.Material->CreateData;
            if (!material.AlbedoTexture.IsValid() || !material.NormalTexture.IsValid() || !material.ORMTexture.IsValid())
            {
                LOG_WARNING_F("MEGA_BANANAS_ASSET_INVALID path=%s バナナの材質のテクスチャを読めないため置きません"
                              "（albedo=%d normal=%d orm=%d）",
                              assembly.LogicalPath.c_str(), material.AlbedoTexture.IsValid() ? 1 : 0,
                              material.NormalTexture.IsValid() ? 1 : 0, material.ORMTexture.IsValid() ? 1 : 0);
                data.m_bBananasSkipped = true;
                data.m_Info.bAssetMissing = true;
                data.m_pAssembly.reset();
                return;
            }

            auto &megaGeometry = ctx.RenderResourcesRef.MegaGeometry();
            MegaMeshCreateInfo createInfo;
            ModelHandle modelHandle = ModelHandle::Invalid();
            MegaMeshHandle megaMesh;
            if (BuildMegaMeshCreateInfoFromCookedMesh(*assembly.Mesh, createInfo))
            {
                createInfo.PageSource = assembly.PageSource;
                createInfo.Material.AlbedoTexture = material.AlbedoTexture;
                createInfo.Material.NormalTexture = material.NormalTexture;
                createInfo.Material.ORMTexture = material.ORMTexture;
                createInfo.Material.bNormalTwoChannel = material.bNormalTwoChannel;
                createInfo.DebugName = "Banana";
                megaMesh = megaGeometry.CreateMegaMesh(createInfo);
                if (megaMesh.IsValid())
                {
                    modelHandle = megaGeometry.RegisterModel(megaMesh, "Banana", assembly.LogicalPath);
                    if (!modelHandle.IsValid())
                    {
                        megaGeometry.ReleaseMegaMesh(megaMesh);
                    }
                }
            }
            if (!modelHandle.IsValid())
            {
                LOG_ERROR_F("mega-bananas: バナナの MegaMesh を作れませんでした: %s", assembly.LogicalPath.c_str());
                data.m_bBananasSkipped = true;
                data.m_Info.bAssetMissing = true;
                data.m_pAssembly.reset();
                return;
            }
            // 同じバナナの全インスタンスが 1 つの Model を共有する。シーンを出るときにスコープが返す
            ctx.ScopeRef.TrackModel(modelHandle);

            uint32_t pileCount = 0;
            const uint32_t placed = PlaceBananas(ctx, megaMesh, assembly, data.m_BananaCount, pileCount);

            const uint32_t trianglesPerBanana = CountLod0Triangles(*assembly.Mesh);
            data.m_Info.BananaCount = placed;
            data.m_Info.PileCount = pileCount;
            data.m_Info.TrianglesPerBanana = trianglesPerBanana;
            data.m_Info.SourceTriangleTotal = static_cast<uint64_t>(trianglesPerBanana) * placed;
            data.m_Info.MeshClusterCount = static_cast<uint32_t>(assembly.Mesh->Clusters.size());
            data.m_Info.MeshLodLevelCount = assembly.Mesh->LODLevelCount;
            LOG_INFO_F("MEGA_BANANAS_PLACED count=%u pile=%u field=%u triangles_per_banana=%u source_triangles=%llu "
                       "clusters=%u lod_levels=%u",
                       placed, pileCount, placed - pileCount, trianglesPerBanana,
                       static_cast<unsigned long long>(data.m_Info.SourceTriangleTotal), data.m_Info.MeshClusterCount,
                       data.m_Info.MeshLodLevelCount);

            // 向きの検算: ローカル Y（長手）は水平、ローカル X（太さ）は縦になっているはず
            {
                const Math::Vector3 zAxis(0.0f, 0.0f, 1.0f);
                const Math::Vector3 yAxis(0.0f, 1.0f, 0.0f);
                const Math::Quaternion rotation = Math::Quaternion(yAxis, 0.7f) * Math::Quaternion(zAxis, 0.5f * kPi);
                const Math::Vector3 longAxis = rotation * Math::Vector3(0.0f, 1.0f, 0.0f);
                const Math::Vector3 thicknessAxis = rotation * Math::Vector3(1.0f, 0.0f, 0.0f);
                LOG_INFO_F("MEGA_BANANAS_ORIENTATION long_axis_y=%.3f thickness_axis_y=%.3f",
                           static_cast<double>(longAxis.y), static_cast<double>(thicknessAxis.y));
            }

            data.m_bBananasPlaced = true;
            data.m_pAssembly.reset();
        }

        /// H キーの要求を反映し、表示の出し入れに合わせて ImGui の重ねを撮影に載せる
        void UpdateInfoVisibility(MegaBananasData &data)
        {
            if (!data.m_InfoToggle.ConsumeToggleRequest())
            {
                return;
            }
            data.m_bInfoVisible = !data.m_bInfoVisible;
#if defined(NORVES_ENABLE_IMGUI)
            if (data.m_bInfoViewRegistered)
            {
                NorvesLib::Modules::Gui::SetImGuiOverlayInCapture(data.m_bInfoVisible);
            }
#endif
            LOG_INFO_F("MEGA_BANANAS_INFO visible=%d", data.m_bInfoVisible ? 1 : 0);
        }

        /// 開発ビルドだけ: 完了した GPU の区間から MegaGeometry の時間を読む。表示が出ている間だけ読む
        void UpdateGpuTime(GameModeContext &ctx, MegaBananasData &data)
        {
#if NORVES_BUILD_DEVELOPMENT
            if (!data.m_bInfoVisible)
            {
                return;
            }
            uint32_t drawnClusters = 0;
            if (data.m_OcclusionTap.TryGetDrawnClusters(drawnClusters))
            {
                data.m_Info.DrawnClusters = drawnClusters;
                data.m_Info.bDrawnClustersValid = true;
            }
            if (!ctx.EngineRef.GetRenderWorld().SupportsGPUTimings())
            {
                return;
            }
            VariableArray<RenderPassGPUTiming> timings;
            uint64_t droppedFrameCount = 0;
            if (!ctx.EngineRef.GetRenderWorld().TryConsumeCompletedGPUTimings(timings, droppedFrameCount))
            {
                return;
            }
            uint64_t latestFrame = 0;
            bool bUpdated = false;
            for (const RenderPassGPUTiming &timing : timings)
            {
                if (timing.bValid && timing.PassName == "MegaGeometry" && timing.FrameNumber >= latestFrame)
                {
                    latestFrame = timing.FrameNumber;
                    data.m_Info.MegaGeometryGpuMs = timing.DurationMs;
                    data.m_Info.bGpuTimeValid = true;
                    bUpdated = true;
                }
            }
            // 表示を出している間、最初の更新と、その後約 2 秒ごとに値をログへ残す（表示が読めない撮影の確認用）
            if (bUpdated && (data.m_LastGpuLogTick == 0 || data.m_TickCount - data.m_LastGpuLogTick >= 120))
            {
                data.m_LastGpuLogTick = data.m_TickCount;
                LOG_INFO_F("MEGA_BANANAS_GPU megageometry_ms=%.3f frame=%llu drawn_clusters=%u",
                           static_cast<double>(data.m_Info.MegaGeometryGpuMs), static_cast<unsigned long long>(latestFrame),
                           data.m_Info.bDrawnClustersValid ? data.m_Info.DrawnClusters : 0u);
            }
#else
            (void)ctx;
            (void)data;
#endif
        }
    } // namespace

    GameModeEnterResult MegaBananasRoutine::Enter(GameModeContext &ctx, MegaBananasData &data)
    {
        LOG_INFO_F("mega-bananas を開始します（本数 %u）", data.m_BananaCount);
        data.m_TickCount = 0;
        data.m_bFloorReady = false;
        data.m_bBananasPlaced = false;
        data.m_bBananasSkipped = false;
        data.m_Info = BananasInfo{};
        data.m_LastGpuLogTick = 0;
        Game::Scenes::BeginSceneAssembly(ctx.EngineRef);

        const Game::Scenes::SceneDescriptor *scene = Game::Scenes::FindScene(Game::Scenes::kMegaBananasSceneId);
        Game::Scenes::FreeCameraPose pose{};
        if (scene == nullptr || !Game::Scenes::ResolveFreeCameraPose(*scene, data.m_ViewName.c_str(), pose))
        {
            LOG_ERROR("mega-bananas: シーンの一覧に視点の定義がありません");
            Game::Scenes::EndSceneAssembly(ctx.EngineRef);
            return GameModeEnterResult::Failed;
        }

        ApplyEnvironment(ctx);
        if (!CreateFloor(ctx, data))
        {
            if (data.m_pFloorMaterial)
            {
                data.m_pFloorMaterial->bCancelled = true;
                data.m_pFloorMaterial.reset();
            }
            ResetEnvironment(ctx);
            Game::Scenes::EndSceneAssembly(ctx.EngineRef);
            return GameModeEnterResult::Failed;
        }
        data.m_Rig.ConfigurePath(scene->CameraPath, data.m_CameraPathTime);
        if (!data.m_Rig.Enter(ctx, pose, data.m_CameraScriptPath.c_str()))
        {
            LOG_ERROR("mega-bananas: カメラを作れませんでした");
            data.m_pFloorMaterial->bCancelled = true;
            data.m_pFloorMaterial.reset();
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

        if (!StartBananaLoad(ctx, data))
        {
            data.m_bBananasSkipped = true;
            data.m_Info.bAssetMissing = true;
        }

        // H キーの情報表示
        ctx.EngineRef.GetInputRouter().RegisterController(&data.m_InfoToggle,
                                                          NorvesLib::Core::Input::InputRouter::PriorityGame);
        data.m_bInfoToggleRegistered = true;
        data.m_bInfoVisible = data.m_bShowInfoAtStart;
#if NORVES_BUILD_DEVELOPMENT
        // 描いたクラスタ数をログ行から読む（開発ビルドの情報表示用）
        NorvesLib::Core::Logging::Logger::GetInstance().AddSink(&data.m_OcclusionTap);
        data.m_bOcclusionTapRegistered = true;
#endif
#if defined(NORVES_ENABLE_IMGUI)
        if (Module::GetModuleRegistry().FindModule(Identity("NorvesImGuiModule")) != nullptr)
        {
            NorvesLib::Modules::Gui::RegisterImGuiView(&data.m_InfoView);
            NorvesLib::Modules::Gui::SetImGuiOverlayInCapture(data.m_bInfoVisible);
            data.m_bInfoViewRegistered = true;
        }
#endif
        return GameModeEnterResult::Succeeded;
    }

    void MegaBananasRoutine::Tick(GameModeContext &ctx, MegaBananasData &data, float deltaTime)
    {
        ++data.m_TickCount;
        const bool bSuspended = data.m_pPauseMenu != nullptr && data.m_pPauseMenu->IsOpen();
        data.m_Rig.Tick(ctx, deltaTime, bSuspended);

        if (!bSuspended)
        {
            UpdateInfoVisibility(data);
        }
        else
        {
            data.m_InfoToggle.ConsumeToggleRequest();
        }
        UpdateGpuTime(ctx, data);

        if (data.m_pAssembly && data.m_pAssembly->Material->PendingTextureCount == 0)
        {
            FinishBananaLoad(ctx, data);
        }

        const bool bBananasDone = data.m_bBananasPlaced || data.m_bBananasSkipped;
        // 台本を流し終えるまで、決定的な撮影を待たせる（撮影は台本の後のカメラで撮る）
        Game::Scenes::UpdateSceneAssembly(ctx.EngineRef, Game::Scenes::kMegaBananasSceneId,
                                          data.m_TickCount >= kReadyTickCount && bBananasDone && data.m_bFloorReady &&
                                              !data.m_Rig.IsScriptRunning());
    }

    void MegaBananasRoutine::Leave(GameModeContext &ctx, MegaBananasData &data, GameModeExitReason)
    {
        Game::Scenes::EndSceneAssembly(ctx.EngineRef);
        // 描画スレッドは、影・MegaGeometry のパスの間、モデルの GPU データを生のポインタで読む。数千本のバナナは 1 フレームの
        // 描画が長く、Cleanup がモデルを返す瞬間に描画中のフレームが残っていると、解放済みのデータを読んでしまう。
        // Cleanup の前にそれまでのフレームの描画を完了させておく。
        ctx.EngineRef.GetRenderWorld().WaitForRender();
        // 届いていない読み込みの結果は捨てる
        if (data.m_pFloorMaterial)
        {
            data.m_pFloorMaterial->bCancelled = true;
            data.m_pFloorMaterial.reset();
        }
        if (data.m_pAssembly)
        {
            data.m_pAssembly->Material->bCancelled = true;
            data.m_pAssembly.reset();
        }
#if defined(NORVES_ENABLE_IMGUI)
        if (data.m_bInfoViewRegistered)
        {
            NorvesLib::Modules::Gui::UnregisterImGuiView(&data.m_InfoView);
            NorvesLib::Modules::Gui::SetImGuiOverlayInCapture(false);
            data.m_bInfoViewRegistered = false;
        }
#endif
#if NORVES_BUILD_DEVELOPMENT
        if (data.m_bOcclusionTapRegistered)
        {
            NorvesLib::Core::Logging::Logger::GetInstance().RemoveSink(&data.m_OcclusionTap);
            data.m_bOcclusionTapRegistered = false;
        }
#endif
        if (data.m_bInfoToggleRegistered)
        {
            ctx.EngineRef.GetInputRouter().UnregisterController(&data.m_InfoToggle);
            data.m_bInfoToggleRegistered = false;
        }
        data.m_InfoToggle.OnInputReset();
        data.m_bInfoVisible = false;
        data.m_Rig.Leave(ctx);
        // 床・バナナのモデルと実体・材質・メッシュ・カメラの実体は GameModeScope::Cleanup が返す。空は RenderWorld の設定なので戻す。
        ResetEnvironment(ctx);
        LOG_INFO("mega-bananas を終了します");
    }

} // namespace Game::GameModes

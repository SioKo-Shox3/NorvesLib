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
        /// 山の高さを調べるための、頂点を間引いた点（バナナのローカル座標・倍率 1）
        VariableArray<Math::Vector3> SamplePoints;
        /// 同じ用途の、より密な点（置く高さの確定と格子への書き込み用）
        VariableArray<Math::Vector3> DensePoints;
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

        // 床: 一辺 1600 m の平面。木の床のテクスチャ 1 枚を 1.5 m 四方に貼る。縁を地平線の手前に見せないよう、
        // カメラの遠方面（1000 m）に届く広さにする
        constexpr float kFloorSize = 1600.0f;
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

        // 敷き詰め: 互いに kPoissonRadius 以上離れた点（ポアソン円盤）に置く。バナナは長さ約 0.2 m・反りで横にも張り出し、
        // 中心から 0.125 m ほどの円に収まるので、この間隔なら向きと大きさをばらしても重ならない。升目の並びは出ない
        constexpr float kPoissonRadius = 0.25f;
        // 矩形の縁から内側へ、この距離をかけて密度を 0 から 1 へ上げる（縁が直線に見えないようにする）
        constexpr float kFieldTaperMeters = 3.5f;
        // 縁の位置をゆらす幅と、そのゆらぎの空間の周期（縁が 1 本の直線に見えないようにする）
        constexpr float kFieldEdgeWobbleMeters = 5.0f;
        constexpr float kFieldEdgeNoiseCellMeters = 6.0f;
        // 敷き詰めの矩形の奥行きに対する幅の比
        constexpr float kFieldAspect = 2.2f;
        // 本数から矩形の面積を見積もるときの 1 平方メートルあたりの本数（足りなければ広げて作り直す）
        constexpr float kFieldDensityGuess = 9.0f;
        constexpr int kFieldMaxRetries = 16;
        // 敷き詰めの手前の端の z（カメラの後ろまで敷き、手前の縁の間引きを画面に出さない）
        constexpr float kFieldNearZ = -16.0f;
        // 山（塚）: 中心から正規分布で落とし、置いたバナナの形の上に載せる。物理は使わず、高さの格子へ
        // 「バナナの頂点の最大の高さ」を書き込み、次のバナナは自分の下面が格子に初めて触れる高さに置く
        constexpr uint32_t kPileBananas = 260;
        constexpr int kPileCount = 3;
        // 山の中心（床の x・z）。塚の裾の半径は約 0.85 m で、互いに重ならない間隔
        constexpr float kPileCenters[kPileCount][2] = {{-2.3f, 2.5f}, {0.2f, 3.5f}, {2.7f, 2.3f}};
        // 落とす位置の標準偏差と、落とす位置の最大半径（標準偏差の倍数）
        constexpr float kPileSigmaMeters = 0.30f;
        constexpr float kPileMaxSigmas = 2.4f;
        // 1 本ごとに試す落とし位置の数。最も低く収まる位置を採る（くぼみが埋まり、裾が広がりすぎない程度）
        constexpr int kPileCandidates = 3;
        // 高さの格子: 山の中心から片側 kPileGridHalfMeters の範囲を kPileGridCellMeters 刻み
        constexpr float kPileGridHalfMeters = 1.4f;
        constexpr float kPileGridCellMeters = 0.005f;
        // 山の周りに敷き詰めを空ける半径（塚の裾 + 余白）
        constexpr float kPileExcludeRadius = 1.0f;
        // 山に使う本数の上限（全体の本数の 1/4 まで）
        constexpr uint32_t kMaxPileShareDivisor = 4;
        // 形の高さを調べるために使うバナナの頂点の数の上限（間引いて使う）。落とし位置の比較は粗い点（速い）で、
        // 置く高さの確定と格子への書き込みは密な点（細い茎の先も拾う）で行う
        constexpr uint32_t kMaxSamplePoints = 6000;
        constexpr uint32_t kMaxDensePoints = 24000;
        // 床へわずかに沈める深さ（接地の隙間を隠す）。山の上段も同じ量だけ下の形へ沈める
        constexpr float kSinkMeters = 0.002f;
        // 向き・大きさのばらつき
        constexpr float kScaleMin = 0.92f;
        constexpr float kScaleMax = 1.08f;
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
            {
                const size_t vertexCount = assembly->Mesh->Vertices.size();
                const size_t coarseStride = std::max<size_t>(1, vertexCount / kMaxSamplePoints);
                const size_t denseStride = std::max<size_t>(1, vertexCount / kMaxDensePoints);
                for (size_t index = 0; index < vertexCount; ++index)
                {
                    const Asset::CookedMeshVertex &vertex = assembly->Mesh->Vertices[index];
                    const Math::Vector3 point(vertex.Position.X, vertex.Position.Y, vertex.Position.Z);
                    if (index % coarseStride == 0)
                    {
                        assembly->SamplePoints.push_back(point);
                    }
                    if (index % denseStride == 0)
                    {
                        assembly->DensePoints.push_back(point);
                    }
                }
            }

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

        /// 床に横たえたバナナの回転。長手（ローカル Y）を水平にし、反りの面も水平にして、太さ（ローカル X）を縦にする
        Math::Quaternion MakeBananaRotation(float yaw)
        {
            const Math::Vector3 yAxis(0.0f, 1.0f, 0.0f);
            const Math::Vector3 zAxis(0.0f, 0.0f, 1.0f);
            const Math::Quaternion roll(zAxis, 0.5f * kPi);
            const Math::Quaternion turn(yAxis, yaw);
            // 描画側は行ベクトル規約なので、先に倒して（roll）から水平に回す（turn）には roll * turn と書く。
            // 逆順だと長手が全個体で (1,0,0) に固定され、乱数の角度が反りを上下へ回して床へ食い込む
            return roll * turn;
        }

        /// バナナのローカルの点を、倍率と向きを掛けたワールドの差分（位置は含まない）へ変える。
        /// 描画側は行ベクトル規約で、回転は共役（逆回転）として点に作用する
        void OrientSamples(const VariableArray<Math::Vector3> &local, float yaw, float scale, VariableArray<Math::Vector3> &out)
        {
            const Math::Quaternion rotation = MakeBananaRotation(yaw);
            const Math::Quaternion inverse(-rotation.x, -rotation.y, -rotation.z, rotation.w);
            // 点ごとに四元数を掛けると遅いので、基底を 1 回回して 3×3 の列にする
            const Math::Vector3 columnX = inverse * Math::Vector3(scale, 0.0f, 0.0f);
            const Math::Vector3 columnY = inverse * Math::Vector3(0.0f, scale, 0.0f);
            const Math::Vector3 columnZ = inverse * Math::Vector3(0.0f, 0.0f, scale);
            out.resize(local.size());
            for (size_t index = 0; index < local.size(); ++index)
            {
                const Math::Vector3 &point = local[index];
                out[index] = Math::Vector3(columnX.x * point.x + columnY.x * point.y + columnZ.x * point.z,
                                           columnX.y * point.x + columnY.y * point.y + columnZ.y * point.z,
                                           columnX.z * point.x + columnY.z * point.y + columnZ.z * point.z);
            }
        }

        /// 1 本のバナナを置く。位置はバナナの原点（中心）の世界座標
        void SpawnBanana(GameModeContext &ctx, MegaMeshHandle megaMesh, float x, float y, float z, float yaw, float scale)
        {
            Entity *object = ctx.ScopeRef.SpawnObject<Entity>();
            if (object == nullptr)
            {
                return;
            }
            object->SetPosition(x, y, z);
            object->SetScale(scale, scale, scale);
            object->SetRotation(MakeBananaRotation(yaw));
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

        /// 疑似乱数。番号だけで決まり、実行ごとに同じ並びになる
        struct PlacementRandom
        {
            uint32_t Counter = 0;

            float Next()
            {
                return UnitFloat(HashIndex(Counter++));
            }
        };

        /// 2 次元の値ノイズ（0〜1。格子点の乱数を滑らかに補間する）
        float ValueNoise(float x, float z, float cellMeters)
        {
            const float gridX = x / cellMeters;
            const float gridZ = z / cellMeters;
            const float floorX = std::floor(gridX);
            const float floorZ = std::floor(gridZ);
            const float fractionX = gridX - floorX;
            const float fractionZ = gridZ - floorZ;
            const auto lattice = [](float ix, float iz)
            {
                return UnitFloat(HashIndex(static_cast<uint32_t>(static_cast<int32_t>(ix)) * 73856093u ^
                                           static_cast<uint32_t>(static_cast<int32_t>(iz)) * 19349663u ^ 0x9e3779b9u));
            };
            const float smoothX = fractionX * fractionX * (3.0f - 2.0f * fractionX);
            const float smoothZ = fractionZ * fractionZ * (3.0f - 2.0f * fractionZ);
            const float near0 = lattice(floorX, floorZ) * (1.0f - smoothX) + lattice(floorX + 1.0f, floorZ) * smoothX;
            const float near1 =
                lattice(floorX, floorZ + 1.0f) * (1.0f - smoothX) + lattice(floorX + 1.0f, floorZ + 1.0f) * smoothX;
            return near0 * (1.0f - smoothZ) + near1 * smoothZ;
        }

        struct FieldPoint
        {
            float X = 0.0f;
            float Z = 0.0f;
        };

        /// 矩形の中に、互いに radius 以上離れた点を敷く（Bridson 法のポアソン円盤）
        void GeneratePoissonDisk(float minX, float minZ, float width, float depth, float radius, PlacementRandom &random,
                                 VariableArray<FieldPoint> &outPoints)
        {
            outPoints.clear();
            const float cell = radius / 1.41421356f;
            const int gridWidth = static_cast<int>(std::ceil(width / cell)) + 1;
            const int gridDepth = static_cast<int>(std::ceil(depth / cell)) + 1;
            VariableArray<int32_t> grid(static_cast<size_t>(gridWidth) * static_cast<size_t>(gridDepth), -1);
            VariableArray<uint32_t> active;

            const auto isFree = [&](float x, float z)
            {
                const int cellX = static_cast<int>((x - minX) / cell);
                const int cellZ = static_cast<int>((z - minZ) / cell);
                for (int dz = -2; dz <= 2; ++dz)
                {
                    const int gz = cellZ + dz;
                    if (gz < 0 || gz >= gridDepth)
                    {
                        continue;
                    }
                    for (int dx = -2; dx <= 2; ++dx)
                    {
                        const int gx = cellX + dx;
                        if (gx < 0 || gx >= gridWidth)
                        {
                            continue;
                        }
                        const int32_t other = grid[static_cast<size_t>(gz) * static_cast<size_t>(gridWidth) + static_cast<size_t>(gx)];
                        if (other < 0)
                        {
                            continue;
                        }
                        const float diffX = outPoints[static_cast<size_t>(other)].X - x;
                        const float diffZ = outPoints[static_cast<size_t>(other)].Z - z;
                        if (diffX * diffX + diffZ * diffZ < radius * radius)
                        {
                            return false;
                        }
                    }
                }
                return true;
            };
            const auto insert = [&](float x, float z)
            {
                const uint32_t index = static_cast<uint32_t>(outPoints.size());
                outPoints.push_back(FieldPoint{x, z});
                const int cellX = static_cast<int>((x - minX) / cell);
                const int cellZ = static_cast<int>((z - minZ) / cell);
                grid[static_cast<size_t>(cellZ) * static_cast<size_t>(gridWidth) + static_cast<size_t>(cellX)] =
                    static_cast<int32_t>(index);
                active.push_back(index);
            };

            insert(minX + width * random.Next(), minZ + depth * random.Next());
            while (!active.empty())
            {
                const size_t pick = std::min(active.size() - 1, static_cast<size_t>(random.Next() * static_cast<float>(active.size())));
                const FieldPoint origin = outPoints[active[pick]];
                bool bFound = false;
                for (int attempt = 0; attempt < 24 && !bFound; ++attempt)
                {
                    const float angle = 2.0f * kPi * random.Next();
                    const float distance = radius * (1.0f + random.Next());
                    const float x = origin.X + std::cos(angle) * distance;
                    const float z = origin.Z + std::sin(angle) * distance;
                    if (x < minX || x >= minX + width || z < minZ || z >= minZ + depth || !isFree(x, z))
                    {
                        continue;
                    }
                    insert(x, z);
                    bFound = true;
                }
                if (!bFound)
                {
                    active[pick] = active.back();
                    active.pop_back();
                }
            }
        }

        /// 敷き詰める fieldCount 個の位置を作る。山の周りは空け、矩形の縁は密度を落とし、足りなければ矩形を広げて作り直す
        void BuildFieldPoints(uint32_t fieldCount, PlacementRandom &random, VariableArray<FieldPoint> &outPoints)
        {
            float area = static_cast<float>(fieldCount) / kFieldDensityGuess + 15.0f;
            VariableArray<FieldPoint> candidates;
            for (int retry = 0; retry <= kFieldMaxRetries; ++retry)
            {
                const float width = std::sqrt(area * kFieldAspect);
                const float depth = area / width;
                const float minX = -0.5f * width;
                GeneratePoissonDisk(minX, kFieldNearZ, width, depth, kPoissonRadius, random, candidates);

                outPoints.clear();
                for (const FieldPoint &point : candidates)
                {
                    bool bUnderPile = false;
                    for (int pile = 0; pile < kPileCount; ++pile)
                    {
                        const float diffX = point.X - kPileCenters[pile][0];
                        const float diffZ = point.Z - kPileCenters[pile][1];
                        if (diffX * diffX + diffZ * diffZ < kPileExcludeRadius * kPileExcludeRadius)
                        {
                            bUnderPile = true;
                            break;
                        }
                    }
                    if (bUnderPile)
                    {
                        continue;
                    }
                    // 縁から内側へ向けて、確率で間引く（滑らかなステップ）
                    const float edge = std::min(std::min(point.X - minX, minX + width - point.X),
                                                std::min(point.Z - kFieldNearZ, kFieldNearZ + depth - point.Z));
                    // 縁の位置を低周波のノイズでゆらし、境目が 1 本の直線に見えないようにする
                    const float wobble = kFieldEdgeWobbleMeters * ValueNoise(point.X, point.Z, kFieldEdgeNoiseCellMeters);
                    const float t = std::clamp((edge - wobble) / kFieldTaperMeters, 0.0f, 1.0f);
                    if (random.Next() < t * t * (3.0f - 2.0f * t))
                    {
                        outPoints.push_back(point);
                    }
                }
                if (outPoints.size() >= fieldCount)
                {
                    break;
                }
                const float shortage = static_cast<float>(fieldCount) / static_cast<float>(std::max<size_t>(1, outPoints.size()));
                area *= std::max(1.03f, shortage * 1.04f);
            }

            // 余りは乱数でシャッフルして先頭から使い、残りを捨てる（密度が一様なまま本数を合わせる）
            for (size_t index = outPoints.size(); index > 1; --index)
            {
                const size_t other = std::min(index - 1, static_cast<size_t>(random.Next() * static_cast<float>(index)));
                std::swap(outPoints[index - 1], outPoints[other]);
            }
            if (outPoints.size() > fieldCount)
            {
                outPoints.resize(fieldCount);
            }
        }

        /// 山の範囲の高さの格子。置いたバナナの頂点の最大の高さを書き込み、床は 0
        struct PileHeightField
        {
            float OriginX = 0.0f;
            float OriginZ = 0.0f;
            int Size = 0;
            VariableArray<float> Heights;

            PileHeightField(float centerX, float centerZ)
            {
                Size = static_cast<int>(2.0f * kPileGridHalfMeters / kPileGridCellMeters);
                OriginX = centerX - kPileGridHalfMeters;
                OriginZ = centerZ - kPileGridHalfMeters;
                Heights.assign(static_cast<size_t>(Size) * static_cast<size_t>(Size), 0.0f);
            }

            /// 範囲の中の格子の番号を返せたら true
            bool CellIndex(float x, float z, size_t &outIndex) const
            {
                const int cellX = static_cast<int>(std::floor((x - OriginX) / kPileGridCellMeters));
                const int cellZ = static_cast<int>(std::floor((z - OriginZ) / kPileGridCellMeters));
                if (cellX < 0 || cellX >= Size || cellZ < 0 || cellZ >= Size)
                {
                    return false;
                }
                outIndex = static_cast<size_t>(cellZ) * static_cast<size_t>(Size) + static_cast<size_t>(cellX);
                return true;
            }

            /// 範囲外は 0（床）
            float Sample(float x, float z) const
            {
                size_t index = 0;
                return CellIndex(x, z, index) ? Heights[index] : 0.0f;
            }

            void Raise(float x, float z, float height)
            {
                size_t index = 0;
                if (CellIndex(x, z, index) && height > Heights[index])
                {
                    Heights[index] = height;
                }
            }
        };

        /// 山を 1 つ作る。中心から正規分布で落とし、下の形に初めて触れる高さに置く
        void PlacePile(GameModeContext &ctx, MegaMeshHandle megaMesh, const BananaAssembly &assembly, int pileIndex,
                       float centerX, float centerZ, uint32_t count, PlacementRandom &random)
        {
            PileHeightField field(centerX, centerZ);
            VariableArray<Math::Vector3> oriented;
            float peakHeight = 0.0f;
            for (uint32_t n = 0; n < count; ++n)
            {
                float bestRequired = std::numeric_limits<float>::max();
                float bestX = 0.0f;
                float bestZ = 0.0f;
                float bestYaw = 0.0f;
                float bestScale = 1.0f;
                for (int candidate = 0; candidate < kPileCandidates; ++candidate)
                {
                    const float radius = std::min(kPileSigmaMeters * std::sqrt(-2.0f * std::log(std::max(random.Next(), 1.0e-6f))),
                                                  kPileSigmaMeters * kPileMaxSigmas);
                    const float angle = 2.0f * kPi * random.Next();
                    const float x = centerX + std::cos(angle) * radius;
                    const float z = centerZ + std::sin(angle) * radius;
                    const float yaw = random.Next() * 2.0f * kPi;
                    const float scale = kScaleMin + (kScaleMax - kScaleMin) * random.Next();
                    OrientSamples(assembly.SamplePoints, yaw, scale, oriented);
                    // 自分の最下面が、下の形（と床）のどの点にも食い込まない最小の原点の高さ
                    float required = 0.0f;
                    for (const Math::Vector3 &offset : oriented)
                    {
                        required = std::max(required, field.Sample(x + offset.x, z + offset.z) - offset.y);
                    }
                    if (required < bestRequired)
                    {
                        bestRequired = required;
                        bestX = x;
                        bestZ = z;
                        bestYaw = yaw;
                        bestScale = scale;
                    }
                }

                // 選んだ位置の高さを、密な点で確かめ直す（粗い点が拾わなかった下の形の尖りへ食い込まない）
                OrientSamples(assembly.DensePoints, bestYaw, bestScale, oriented);
                for (const Math::Vector3 &offset : oriented)
                {
                    bestRequired = std::max(bestRequired, field.Sample(bestX + offset.x, bestZ + offset.z) - offset.y);
                }
                const float y = bestRequired - kSinkMeters;
                for (const Math::Vector3 &offset : oriented)
                {
                    field.Raise(bestX + offset.x, bestZ + offset.z, y + offset.y);
                    peakHeight = std::max(peakHeight, y + offset.y);
                }
                SpawnBanana(ctx, megaMesh, bestX, y, bestZ, bestYaw, bestScale);
#if NORVES_BUILD_DEVELOPMENT
                // 置いた姿勢（外部の検算が頂点から接触の隙間を数えるための記録）
                LOG_INFO_F("MEGA_BANANAS_PILE_ITEM pile=%d index=%u x=%.5f y=%.5f z=%.5f yaw=%.6f scale=%.6f", pileIndex, n,
                           static_cast<double>(bestX), static_cast<double>(y), static_cast<double>(bestZ),
                           static_cast<double>(bestYaw), static_cast<double>(bestScale));
#endif
            }
            LOG_INFO_F("MEGA_BANANAS_PILE_SHAPE pile=%d count=%u peak_height=%.4f", pileIndex, count,
                       static_cast<double>(peakHeight));
        }

        /// count 本を、手前に山・その周りに敷き詰めで置く。置いた本数を返す
        uint32_t PlaceBananas(GameModeContext &ctx, MegaMeshHandle megaMesh, const BananaAssembly &assembly, uint32_t count,
                              uint32_t &outPileCount)
        {
            PlacementRandom random;
            uint32_t placed = 0;
            const uint32_t pileBudget = std::min(kPileBananas * static_cast<uint32_t>(kPileCount), count / kMaxPileShareDivisor);
            if (assembly.SamplePoints.empty() || assembly.DensePoints.empty())
            {
                outPileCount = 0;
                return 0;
            }

            for (int pile = 0; pile < kPileCount; ++pile)
            {
                const uint32_t pileCount =
                    pileBudget / kPileCount + (static_cast<uint32_t>(pile) < pileBudget % kPileCount ? 1u : 0u);
                PlacePile(ctx, megaMesh, assembly, pile, kPileCenters[pile][0], kPileCenters[pile][1], pileCount, random);
                placed += pileCount;
            }
            outPileCount = placed;

            // 敷き詰め: 床に横たえる。太さの向きが縦になるので、中心の高さ = 最下点が床に接する高さ（拡大後）
            const uint32_t fieldCount = count - placed;
            if (fieldCount > 0)
            {
                VariableArray<FieldPoint> points;
                BuildFieldPoints(fieldCount, random, points);
                const float halfThickness = 0.5f * (assembly.ThicknessMax - assembly.ThicknessMin);
                const float centerOffset = 0.5f * (assembly.ThicknessMax + assembly.ThicknessMin);
                for (const FieldPoint &point : points)
                {
                    const float yaw = random.Next() * 2.0f * kPi;
                    const float scale = kScaleMin + (kScaleMax - kScaleMin) * random.Next();
                    const float y = (halfThickness + centerOffset) * scale - kSinkMeters;
                    SpawnBanana(ctx, megaMesh, point.X, y, point.Z, yaw, scale);
                    ++placed;
                }
                if (points.size() < fieldCount)
                {
                    LOG_WARNING_F("mega-bananas: 敷き詰めの位置が足りませんでした（要求 %u・実際 %zu）", fieldCount, points.size());
                }
            }
            return placed;
        }

#if NORVES_BUILD_DEVELOPMENT
        /// 根の球（BVH の根の節。段 0 のインスタンス判定が使う）・メッシュ全体の球・根のクラスタの球の最大を、
        /// ローカル空間の半径でログへ出す。インスタンスの拡大率はかけていない
        void LogBananaBoundingSpheres(const MegaMeshCreateInfo &createInfo)
        {
            float rootNodeRadius = 0.0f;
            if (!createInfo.GroupBVH.empty())
            {
                rootNodeRadius = createInfo.GroupBVH[0].BoundsRadius;
            }
            uint32_t topLevel = 0;
            for (const MeshCluster &cluster : createInfo.Clusters)
            {
                topLevel = std::max(topLevel, cluster.LODLevel);
            }
            float rootClusterRadius = 0.0f;
            for (const MeshCluster &cluster : createInfo.Clusters)
            {
                if (cluster.LODLevel == topLevel)
                {
                    rootClusterRadius = std::max(rootClusterRadius, cluster.Bounds.Radius);
                }
            }
            LOG_INFO_F("MEGA_BANANAS_SPHERES root_node_radius=%.4f mesh_radius=%.4f root_cluster_max_radius=%.4f "
                       "lod_sphere_radius=%.4f bvh_nodes=%u",
                       rootNodeRadius, createInfo.TotalBounds.Radius, rootClusterRadius, createInfo.LODBounds.Radius,
                       static_cast<uint32_t>(createInfo.GroupBVH.size()));
        }
#endif

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
#if NORVES_BUILD_DEVELOPMENT
                LogBananaBoundingSpheres(createInfo);
#endif
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

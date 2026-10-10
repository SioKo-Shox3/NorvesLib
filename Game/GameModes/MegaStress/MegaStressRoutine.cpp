#include "GameModes/MegaStress/MegaStressMode.h"

#include "Core/Public/Asset/AssetSystem.h"
#include "Core/Public/Asset/CookedMeshFormat.h"
#include "Core/Public/Component/CameraComponent.h"
#include "Core/Public/Component/MegaGeometryComponent.h"
#include "Core/Public/Component/MeshComponent.h"
#include "Core/Public/Debug/DebugConfig.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/GameMode/GameModeScope.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Math/Quaternion.h"
#include "Core/Public/Math/Vector3.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/RHI/ITexture.h"
#include "Core/Public/Rendering/MaterialTypes.h"
#include "Core/Public/Rendering/MegaGeometry/CookedMeshMegaMeshAdapter.h"
#include "Core/Public/Rendering/MegaGeometry/CookedMeshPageSource.h"
#include "Core/Public/Rendering/MegaGeometry/StartupBigSphereSpec.h"
#include "Core/Public/Rendering/ProceduralMeshGenerator.h"
#include "Core/Public/Rendering/RenderResources.h"
#include "Core/Public/Rendering/RenderWorld.h"
#include "Core/Public/Rendering/SkyAtmosphere.h"
#include "Core/Public/Rendering/VolumetricFog.h"
#include "GameModes/Rendering3DTest/SkySunControl.h"
#include "Scenes/SceneCatalog.h"
#include "Scenes/SceneReadiness.h"
#include "Text/JsonDocument.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Container;
using namespace NorvesLib::Core::GameMode;
using namespace NorvesLib::Core::Rendering;
using namespace NorvesLib::Core::Rendering::MegaGeometry;
namespace Math = NorvesLib::Math;

namespace Game::GameModes
{

    /// メッシュの一覧の 1 件（Assets/AssetSets/MegaStressMeshes.json の meshes[] と同じ内容）
    struct StressMeshSpec
    {
        String Id;
        /// クック済みメッシュの論理パス
        String MeshPath;
        /// 材質のテクスチャの論理パス（クック済みのアルベド・法線・ORM）
        String Albedo;
        String Normal;
        String Orm;
        /// 材質のテクスチャを仮想テクスチャで読むか（読めなければ全常駐で読み直す）
        bool bVirtualTexture = false;
        /// 起動画面の大きな球（石畳の高さマップで変位した球）。法線マップに頂点の間隔を渡す
        bool bDisplacedSphere = false;
        /// 置くときの高さ[m]。メッシュの高さをこの値に拡大・縮小する
        float TargetHeight = 1.0f;
        /// 種類の選ばれやすさ（重みの比）
        float Weight = 1.0f;
    };

    /// 1 種類のメッシュの読み込み。材質のテクスチャがそろったら MegaMesh にする
    struct StressMeshLoad
    {
        StressMeshSpec Spec;
        TSharedPtr<Asset::CookedMeshData> Mesh;
        /// ページ（NVMESH v1.1）の読み込み元。ページを 2 つ以上持つメッシュだけが持つ
        TSharedPtr<IGeometryPageSource> PageSource;
        TSharedPtr<StressPendingMaterial> Material;
        /// メッシュ全体の高さ方向（ローカル Y）の最小・最大
        float MinY = 0.0f;
        float MaxY = 0.0f;
        // ---- 作った後 ----
        MegaMeshHandle MegaMesh;
        bool bReady = false;
        /// 床に据える高さ（拡大後に最下点が床に接する Entity の y）と、置くときの拡大率
        float BaseScale = 1.0f;
        float BaseY = 0.0f;
        uint32_t PlacedCount = 0;
    };

    struct StressAssembly
    {
        VariableArray<StressMeshLoad> Loads;
    };

    /// 毎フレーム動かす 1 つの Entity。置いた位置・向きを基準に、円運動とヨーの回転を重ねる
    struct StressMover
    {
        Entity *pEntity = nullptr;
        float BaseX = 0.0f;
        float BaseY = 0.0f;
        float BaseZ = 0.0f;
        float BaseYaw = 0.0f;
        /// 円運動の半径[m]・角速度[rad/s]・初期位相[rad]、ヨーの角速度[rad/s]
        float Radius = 0.0f;
        float Omega = 0.0f;
        float Phase = 0.0f;
        float YawOmega = 0.0f;
    };

    struct StressMovers
    {
        VariableArray<StressMover> Items;
    };

    namespace
    {
        // 昼の太陽。カメラの後ろ右から差す向き
        constexpr float kSunAltitudeDegrees = 40.0f;
        constexpr float kSunAzimuthDegrees = -35.0f;
        // 晴天の昼の手動露出（絞り f/16・ISO 100 のままシャッター速度で合わせる）
        constexpr float kExposureEV100 = 15.0f;
        // SceneReady を出すまでの Tick の数（組み立ての完了後に、描画が最初の絵を作る余裕）
        constexpr uint32_t kReadyTickCount = 3;

        // 床: 一辺 1600 m の無地の平面（カメラの遠方面に届く広さ）。他のシーンのメッシュの ID（100〜、200〜、900、910）と重ならない値
        constexpr float kFloorSize = 1600.0f;
        constexpr uint32_t kFloorMeshHandleId = 920;

        // メッシュの一覧（リポジトリのルートからの相対パス）
        constexpr const char *kMeshListRelativePath = "Assets/AssetSets/MegaStressMeshes.json";
        constexpr size_t kMaxMeshListBytes = 1u << 20;

        // 配置: 升目（1 辺 kCellMeters）ごとに 1 つ、升目の中でずらして置く。升目の並びは横に長い（列数 : 行数 = 2 : 1）。
        // 手前の端（最初の行）の z はカメラの前方 kFieldNearZ
        constexpr float kCellMeters = 5.0f;
        constexpr float kCellJitterMeters = 1.0f;
        constexpr float kFieldNearZ = 8.0f;
        constexpr float kPi = 3.14159265358979323846f;

        // 動く Entity の動き: 半径 kMoverRadiusMin〜Max[m] の円運動（角速度 kMoverOmegaMin〜Max[rad/s]）とヨーの回転
        constexpr float kMoverRadiusMin = 1.0f;
        constexpr float kMoverRadiusMax = 2.0f;
        constexpr float kMoverOmegaMin = 0.8f;
        constexpr float kMoverOmegaMax = 1.6f;
        constexpr float kMoverYawOmegaMax = 1.5f;

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

        /// 床: 無地の平面。メッシュの Entity が据わる高さ 0 の面
        bool CreateFloor(GameModeContext &ctx)
        {
            MaterialCreateData materialData;
            materialData.DebugName = "MegaStressFloor";
            materialData.BaseColor[0] = 0.09f;
            materialData.BaseColor[1] = 0.08f;
            materialData.BaseColor[2] = 0.065f;
            materialData.Metallic = 0.0f;
            materialData.Roughness = 0.9f;
            const MaterialHandle material = ctx.RenderResourcesRef.Materials().Create(materialData);
            if (!material.IsValid())
            {
                LOG_ERROR("mega-stress: 床の材質を作れませんでした");
                return false;
            }
            ctx.ScopeRef.TrackMaterial(material);

            VariableArray<Mesh3DVertex> vertices;
            VariableArray<uint32_t> indices;
            ProceduralMeshGenerator::GeneratePlane(kFloorSize, kFloorSize, 1, 1, vertices, indices);
            const MeshDataHandle mesh{kFloorMeshHandleId};
            if (!ctx.RenderResourcesRef.Meshes().Register(mesh, vertices.data(),
                                                          static_cast<uint32_t>(vertices.size() * sizeof(Mesh3DVertex)),
                                                          indices.data(), static_cast<uint32_t>(indices.size())))
            {
                LOG_ERROR("mega-stress: 床のメッシュを登録できませんでした");
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
            meshComponent->SetMaterial(0, material);
            meshComponent->SetCastShadow(false);
            return true;
        }

        /// ファイルを丸ごと読む。開けない・大きすぎるときは false
        bool ReadFileBytes(const String &path, VariableArray<uint8_t> &outBytes)
        {
            std::ifstream stream(path.c_str(), std::ios::binary | std::ios::ate);
            if (!stream)
            {
                return false;
            }
            const std::streamoff size = stream.tellg();
            if (size <= 0 || static_cast<size_t>(size) > kMaxMeshListBytes)
            {
                return false;
            }
            outBytes.resize(static_cast<size_t>(size));
            stream.seekg(0, std::ios::beg);
            stream.read(reinterpret_cast<char *>(outBytes.data()), size);
            return static_cast<bool>(stream);
        }

        /// メッシュの一覧の JSON を読んで、1 件ずつ spec にする。形が違う件は警告して読み飛ばす
        bool LoadMeshList(VariableArray<StressMeshSpec> &outSpecs)
        {
            String listPath = kMeshListRelativePath;
#if defined(NORVES_SOURCE_ASSET_DIR)
            // 作業ディレクトリは実行ファイルのディレクトリのことがあるので、リポジトリの Assets を基準に探す
            listPath = String(NORVES_SOURCE_ASSET_DIR) + "/AssetSets/MegaStressMeshes.json";
#endif
            VariableArray<uint8_t> bytes;
            if (!ReadFileBytes(listPath, bytes))
            {
                LOG_WARNING_F("MEGA_STRESS_LIST_MISSING path=%s メッシュの一覧を読めません", listPath.c_str());
                return false;
            }
            JsonDocument document;
            String error;
            if (!JsonDocument::TryParseUtf8(Span<const uint8_t>(bytes.data(), bytes.size()), document, &error))
            {
                LOG_WARNING_F("MEGA_STRESS_LIST_INVALID path=%s %s", listPath.c_str(), error.c_str());
                return false;
            }
            const JsonValue root = document.GetRoot();
            const JsonValue meshes = root.IsObject() ? root.FindMember("meshes") : JsonValue();
            if (!root.IsObject() || root.FindMember("version").AsNumber() != 1.0 || !meshes.IsArray())
            {
                LOG_WARNING_F("MEGA_STRESS_LIST_INVALID path=%s version が 1 でない、または meshes が配列でありません",
                              listPath.c_str());
                return false;
            }
            for (size_t index = 0; index < meshes.GetArraySize(); ++index)
            {
                const JsonValue item = meshes.GetArrayElement(index);
                StressMeshSpec spec;
                if (item.IsObject())
                {
                    spec.Id = item.FindMember("id").AsString();
                    spec.MeshPath = item.FindMember("mesh").AsString();
                    spec.Albedo = item.FindMember("albedo").AsString();
                    spec.Normal = item.FindMember("normal").AsString();
                    spec.Orm = item.FindMember("orm").AsString();
                    spec.bVirtualTexture = item.FindMember("virtual_texture").AsBool(false);
                    spec.bDisplacedSphere = item.FindMember("displaced_sphere").AsBool(false);
                    spec.TargetHeight = static_cast<float>(item.FindMember("target_height_m").AsNumber(0.0));
                    spec.Weight = static_cast<float>(item.FindMember("weight").AsNumber(1.0));
                }
                if (spec.Id.empty() || spec.MeshPath.empty() || spec.Albedo.empty() || spec.Normal.empty() ||
                    spec.Orm.empty() || !(spec.TargetHeight > 0.0f) || !std::isfinite(spec.TargetHeight) ||
                    !(spec.Weight > 0.0f) || !std::isfinite(spec.Weight))
                {
                    LOG_WARNING_F("MEGA_STRESS_LIST_ENTRY_INVALID index=%zu 項目の形が正しくないため読み飛ばします", index);
                    continue;
                }
                outSpecs.push_back(std::move(spec));
            }
            return !outSpecs.empty();
        }

        /// 材質のテクスチャを 1 枚、非同期で読む。仮想テクスチャを指定したときは先にそれを試し、
        /// 作れない・常駐しないときは全常駐で読み直す。結果は主スレッドで届く
        void LoadMaterialTexture(TextureResources &textures, const TSharedPtr<StressPendingMaterial> &update,
                                 const String &path, TextureHandle MaterialCreateData::*member, bool bNormal,
                                 bool bTryVirtualTexture)
        {
            TextureResources *pTextures = &textures;
            const auto onLoaded = [pTextures, update, path, member, bNormal](TextureHandle handle)
            {
                uint32_t virtualIndex = 0;
                const bool bVirtual = handle.IsValid() && pTextures->TryGetVirtualTextureIndex(handle, virtualIndex);
                if (update->bCancelled)
                {
                    // シーンを出た後に届いた結果。材質はもう無いので、材質ごとに作った仮想テクスチャだけ返す
                    // （パスで読んだテクスチャはキャッシュが所有する）
                    if (bVirtual)
                    {
                        pTextures->ReleaseTexture(handle);
                    }
                    return;
                }
                if (!handle.IsValid())
                {
                    LOG_WARNING_F("MEGA_STRESS_TEXTURE_FAILED path=%s テクスチャを読めませんでした", path.c_str());
                }
                update->CreateData.*member = handle;
                if (bVirtual)
                {
                    update->VirtualTextures.push_back(handle);
                }
                if (bNormal)
                {
                    // クック済みの法線が BC5（2 チャンネル）なら印を付ける。無圧縮の RGBA8 なら付けない
                    const NorvesLib::RHI::ITexture *rhiTexture = handle.IsValid() ? pTextures->GetRHITexture(handle) : nullptr;
                    update->CreateData.bNormalTwoChannel =
                        rhiTexture != nullptr && rhiTexture->GetFormat() == NorvesLib::RHI::Format::BC5_UNORM;
                }
                --update->PendingTextureCount;
            };

            if (bTryVirtualTexture && textures.SupportsVirtualTexture())
            {
                const bool bStarted = textures.CreateVirtualTextureAsync(
                    path,
                    [pTextures, update, path, onLoaded](TextureHandle handle)
                    {
                        if (handle.IsValid() || update->bCancelled)
                        {
                            onLoaded(handle);
                            return;
                        }
                        LOG_WARNING_F("MEGA_STRESS_VT_FALLBACK path=%s 仮想テクスチャのミップテイルが常駐しないため、"
                                      "全常駐で読み直します",
                                      path.c_str());
                        pTextures->LoadTextureAsync(path, onLoaded);
                    });
                if (bStarted)
                {
                    return;
                }
            }
            textures.LoadTextureAsync(path, onLoaded);
        }

        /// メッシュ 1 種類のクック済みメッシュを解決・解析し、材質のテクスチャの読み込みを始める。
        /// クック済みが無い・壊れているときは理由を警告して false を返す（その種類を読み飛ばす）
        bool StartMeshLoad(GameModeContext &ctx, const TSharedPtr<const Asset::AssetSystem> &assetSystem,
                           const StressMeshSpec &spec, StressAssembly &assembly)
        {
            const Asset::AssetResolveResult resolved = assetSystem->ResolveAsset(spec.MeshPath, Asset::AssetKind::Model);
            if (!resolved.UsedCooked())
            {
                LOG_WARNING_F("MEGA_STRESS_ASSET_SKIPPED id=%s path=%s クック済みのメッシュがありません。"
                              "CookAssets で焼くと置けます",
                              spec.Id.c_str(), spec.MeshPath.c_str());
                return false;
            }
            for (const String *path : {&spec.Albedo, &spec.Normal, &spec.Orm})
            {
                if (!assetSystem->FindCookedVariant(AnsiString(path->c_str()), Asset::AssetKind::Texture).ShouldUseCooked())
                {
                    LOG_WARNING_F("MEGA_STRESS_ASSET_SKIPPED id=%s path=%s クック済みのテクスチャがありません。"
                                  "CookAssets で焼くと置けます",
                                  spec.Id.c_str(), path->c_str());
                    return false;
                }
            }
            Asset::CookedMeshParseResult parsed = Asset::ParseCookedMesh(resolved.Blob);
            if (!parsed.Succeeded() || parsed.Mesh.Clusters.empty() || parsed.Mesh.Vertices.empty())
            {
                LOG_WARNING_F("MEGA_STRESS_ASSET_SKIPPED id=%s path=%s status=%u クック済みのメッシュを解析できません",
                              spec.Id.c_str(), spec.MeshPath.c_str(), static_cast<unsigned int>(parsed.Status));
                return false;
            }

            StressMeshLoad load;
            load.Spec = spec;
            {
                Asset::AssetCookedRange pageRange;
                if (parsed.Mesh.Pages.size() >= 2 && Asset::AssetSystem::TryMakeCookedRange(resolved, pageRange))
                {
                    load.PageSource = MakeCookedMeshPageSource(parsed.Mesh, assetSystem->GetCookedFileReader(),
                                                               pageRange.Request, pageRange.BaseOffset);
                }
            }
            load.Mesh = MakeShared<Asset::CookedMeshData>(std::move(parsed.Mesh));
            float minY = std::numeric_limits<float>::max();
            float maxY = std::numeric_limits<float>::lowest();
            for (const Asset::CookedMeshVertex &vertex : load.Mesh->Vertices)
            {
                minY = std::min(minY, vertex.Position.Y);
                maxY = std::max(maxY, vertex.Position.Y);
            }
            if (!(maxY - minY > 1.0e-4f))
            {
                LOG_WARNING_F("MEGA_STRESS_ASSET_SKIPPED id=%s path=%s メッシュの高さが 0 のため置けません",
                              spec.Id.c_str(), spec.MeshPath.c_str());
                return false;
            }
            load.MinY = minY;
            load.MaxY = maxY;
            load.BaseScale = spec.TargetHeight / (maxY - minY);
            load.BaseY = -minY * load.BaseScale;

            LOG_INFO_F("MEGA_STRESS_MESH id=%s path=%s vertices=%u indices=%u clusters=%u lod_levels=%u pages=%zu "
                       "height=%.3f scale=%.4f",
                       spec.Id.c_str(), spec.MeshPath.c_str(), static_cast<unsigned int>(load.Mesh->Vertices.size()),
                       static_cast<unsigned int>(load.Mesh->Indices.size()),
                       static_cast<unsigned int>(load.Mesh->Clusters.size()),
                       static_cast<unsigned int>(load.Mesh->LODLevelCount), load.Mesh->Pages.size(),
                       static_cast<double>(maxY - minY), static_cast<double>(load.BaseScale));

            auto pending = MakeShared<StressPendingMaterial>();
            pending->CreateData.DebugName = spec.Id.c_str();
            pending->PendingTextureCount = 3;
            load.Material = pending;
            auto &textures = ctx.RenderResourcesRef.Textures();
            LoadMaterialTexture(textures, pending, spec.Albedo, &MaterialCreateData::AlbedoTexture, false,
                                spec.bVirtualTexture);
            LoadMaterialTexture(textures, pending, spec.Normal, &MaterialCreateData::NormalTexture, true,
                                spec.bVirtualTexture);
            LoadMaterialTexture(textures, pending, spec.Orm, &MaterialCreateData::ORMTexture, false, spec.bVirtualTexture);
            assembly.Loads.push_back(std::move(load));
            return true;
        }

        /// メッシュの一覧を読んで、読めた種類の読み込みを始める。始めた数を返す
        uint32_t StartMeshLoads(GameModeContext &ctx, MegaStressData &data)
        {
            if (!data.m_GetAssetSystem.IsBound())
            {
                LOG_WARNING("MEGA_STRESS_ASSET_SKIPPED AssetSystem が無いためメッシュを置きません");
                return 0;
            }
            const TSharedPtr<const Asset::AssetSystem> assetSystem = data.m_GetAssetSystem.Invoke();
            if (!assetSystem)
            {
                LOG_WARNING("MEGA_STRESS_ASSET_SKIPPED AssetSystem が無いためメッシュを置きません");
                return 0;
            }
            VariableArray<StressMeshSpec> specs;
            if (!LoadMeshList(specs))
            {
                return 0;
            }
            auto assembly = MakeShared<StressAssembly>();
            for (const StressMeshSpec &spec : specs)
            {
                StartMeshLoad(ctx, assetSystem, spec, *assembly);
            }
            const uint32_t started = static_cast<uint32_t>(assembly->Loads.size());
            LOG_INFO_F("MEGA_STRESS_LIST listed=%zu loading=%u", specs.size(), started);
            if (started > 0)
            {
                data.m_pAssembly = assembly;
            }
            return started;
        }

        /// 材質がそろった 1 種類の MegaMesh を作り、モデルとして登録する。作れなければ false
        bool CreateMegaMeshForLoad(GameModeContext &ctx, StressMeshLoad &load)
        {
            const MaterialCreateData &material = load.Material->CreateData;
            if (!material.AlbedoTexture.IsValid() || !material.NormalTexture.IsValid() || !material.ORMTexture.IsValid())
            {
                LOG_WARNING_F("MEGA_STRESS_ASSET_SKIPPED id=%s 材質のテクスチャを読めないため置きません"
                              "（albedo=%d normal=%d orm=%d）",
                              load.Spec.Id.c_str(), material.AlbedoTexture.IsValid() ? 1 : 0,
                              material.NormalTexture.IsValid() ? 1 : 0, material.ORMTexture.IsValid() ? 1 : 0);
                return false;
            }
            // MegaMesh は材質のテクスチャを所有しない。材質ごとに作った仮想テクスチャは、シーンの範囲で追跡して
            // Leave 後にモデルと一緒に返す
            for (const TextureHandle texture : load.Material->VirtualTextures)
            {
                ctx.ScopeRef.TrackTexture(texture);
            }
            load.Material->VirtualTextures.clear();

            auto &megaGeometry = ctx.RenderResourcesRef.MegaGeometry();
            MegaMeshCreateInfo createInfo;
            if (!BuildMegaMeshCreateInfoFromCookedMesh(*load.Mesh, createInfo))
            {
                LOG_WARNING_F("MEGA_STRESS_ASSET_SKIPPED id=%s MegaMesh の入力を作れませんでした", load.Spec.Id.c_str());
                return false;
            }
            createInfo.PageSource = load.PageSource;
            createInfo.Material.AlbedoTexture = material.AlbedoTexture;
            createInfo.Material.NormalTexture = material.NormalTexture;
            createInfo.Material.ORMTexture = material.ORMTexture;
            createInfo.Material.bNormalTwoChannel = material.bNormalTwoChannel;
            if (load.Spec.bDisplacedSphere)
            {
                createInfo.Material.DisplacementUVSpacing = StartupBigSphere::DisplacementUVSpacing();
            }
            createInfo.DebugName = load.Spec.Id.c_str();
            const MegaMeshHandle megaMesh = megaGeometry.CreateMegaMesh(createInfo);
            if (!megaMesh.IsValid())
            {
                LOG_WARNING_F("MEGA_STRESS_ASSET_SKIPPED id=%s MegaMesh を作れませんでした", load.Spec.Id.c_str());
                return false;
            }
            const ModelHandle modelHandle = megaGeometry.RegisterModel(megaMesh, load.Spec.Id.c_str(), load.Spec.MeshPath);
            if (!modelHandle.IsValid())
            {
                megaGeometry.ReleaseMegaMesh(megaMesh);
                LOG_WARNING_F("MEGA_STRESS_ASSET_SKIPPED id=%s モデルとして登録できませんでした", load.Spec.Id.c_str());
                return false;
            }
            // 同じメッシュの全 Entity が 1 つの Model を共有する。シーンを出るときにスコープが返す
            ctx.ScopeRef.TrackModel(modelHandle);
            load.MegaMesh = megaMesh;
            load.bReady = true;
            return true;
        }

        /// 1 つの Entity を置く。置けなければ nullptr
        Entity *SpawnStressEntity(GameModeContext &ctx, const StressMeshLoad &load, float x, float z, float yaw)
        {
            Entity *object = ctx.ScopeRef.SpawnObject<Entity>();
            if (object == nullptr)
            {
                return nullptr;
            }
            object->SetPosition(x, load.BaseY, z);
            object->SetScale(load.BaseScale, load.BaseScale, load.BaseScale);
            object->SetRotation(Math::Quaternion(Math::Vector3(0.0f, 1.0f, 0.0f), yaw));
            auto *component = ctx.WorldRef.CreateComponent<Component::MegaGeometryComponent>(object);
            if (component == nullptr)
            {
                return nullptr;
            }
            component->SetMegaMeshHandle(load.MegaMesh);
            component->SetCastShadow(true);
            return object;
        }

        /// 動かす Entity の動きを番号から決める（実行ごとに同じ）
        StressMover MakeMover(Entity *entity, uint32_t index, const StressMeshLoad &load, float x, float z, float yaw)
        {
            const uint32_t hash = HashIndex(index * 2246822519u + 0x2545f491u);
            StressMover mover;
            mover.pEntity = entity;
            mover.BaseX = x;
            mover.BaseY = load.BaseY;
            mover.BaseZ = z;
            mover.BaseYaw = yaw;
            mover.Radius = kMoverRadiusMin + UnitFloat(hash) * (kMoverRadiusMax - kMoverRadiusMin);
            mover.Omega = kMoverOmegaMin + UnitFloat(HashIndex(hash ^ 0x85ebca6bu)) * (kMoverOmegaMax - kMoverOmegaMin);
            mover.Phase = UnitFloat(HashIndex(hash ^ 0xc2b2ae35u)) * 2.0f * kPi;
            mover.YawOmega = (UnitFloat(HashIndex(hash ^ 0x27d4eb2fu)) * 2.0f - 1.0f) * kMoverYawOmegaMax;
            return mover;
        }

        /// 時刻 time[秒] の位置と向きを Entity へ入れる
        void ApplyMover(const StressMover &mover, float time)
        {
            const float angle = mover.Omega * time + mover.Phase;
            mover.pEntity->SetPosition(mover.BaseX + mover.Radius * std::sin(angle), mover.BaseY,
                                       mover.BaseZ + mover.Radius * std::cos(angle));
            mover.pEntity->SetRotation(
                Math::Quaternion(Math::Vector3(0.0f, 1.0f, 0.0f), mover.BaseYaw + mover.YawOmega * time));
        }

        /// 読めた種類から重みで 1 つ選ぶ（番号だけで決まる）。選べなければ -1
        int PickKind(const VariableArray<StressMeshLoad> &loads, float totalWeight, uint32_t entityIndex)
        {
            float target = UnitFloat(HashIndex(entityIndex * 2654435761u + 0x51ed270bu)) * totalWeight;
            int last = -1;
            for (size_t index = 0; index < loads.size(); ++index)
            {
                if (!loads[index].bReady)
                {
                    continue;
                }
                last = static_cast<int>(index);
                target -= loads[index].Spec.Weight;
                if (target < 0.0f)
                {
                    return last;
                }
            }
            return last;
        }

        /// 全種類の材質がそろったら MegaMesh を作り、Entity を置く
        void FinishAssembly(GameModeContext &ctx, MegaStressData &data)
        {
            StressAssembly &assembly = *data.m_pAssembly;
            float totalWeight = 0.0f;
            uint32_t resourceCount = 0;
            for (StressMeshLoad &load : assembly.Loads)
            {
                if (CreateMegaMeshForLoad(ctx, load))
                {
                    totalWeight += load.Spec.Weight;
                    ++resourceCount;
                }
            }

            auto movers = MakeShared<StressMovers>();
            const uint32_t movingThreshold = static_cast<uint32_t>(data.m_MovingPercent * 10000.0f + 0.5f);
            uint32_t placed = 0;
            uint32_t columns = 0;
            uint32_t rows = 0;
            if (resourceCount > 0)
            {
                columns = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(data.m_EntityCount) * 2.0f)));
                rows = (data.m_EntityCount + columns - 1) / columns;
                for (uint32_t index = 0; index < data.m_EntityCount; ++index)
                {
                    const int kind = PickKind(assembly.Loads, totalWeight, index);
                    if (kind < 0)
                    {
                        break;
                    }
                    const uint32_t column = index % columns;
                    const uint32_t row = index / columns;
                    const uint32_t hash = HashIndex(index * 747796405u + 2891336453u);
                    const float jitterX = (UnitFloat(hash) - 0.5f) * 2.0f * kCellJitterMeters;
                    const float jitterZ = (UnitFloat(HashIndex(hash)) - 0.5f) * 2.0f * kCellJitterMeters;
                    const float yaw = UnitFloat(HashIndex(hash ^ 0x9e3779b9u)) * 2.0f * kPi;
                    const float x = (static_cast<float>(column) - 0.5f * static_cast<float>(columns - 1)) * kCellMeters + jitterX;
                    const float z = kFieldNearZ + static_cast<float>(row) * kCellMeters + jitterZ;
                    StressMeshLoad &load = assembly.Loads[static_cast<size_t>(kind)];
                    Entity *entity = SpawnStressEntity(ctx, load, x, z, yaw);
                    if (entity != nullptr)
                    {
                        ++load.PlacedCount;
                        ++placed;
                        // 動かす Entity は番号のハッシュで決める（100 万分率の閾値。実行ごとに同じ）
                        if (HashIndex(index * 3266489917u + 0x165667b1u) % 1000000u < movingThreshold)
                        {
                            movers->Items.push_back(MakeMover(entity, index, load, x, z, yaw));
                        }
                    }
                }
            }

            // 区間は材質とプールの塊が同じ Entity の集まりで、メッシュごとに 1 つ（Entity を 1 つ以上置いた種類の数）
            uint32_t sectionCount = 0;
            for (const StressMeshLoad &load : assembly.Loads)
            {
                if (load.PlacedCount > 0)
                {
                    ++sectionCount;
                    LOG_INFO_F("MEGA_STRESS_KIND id=%s count=%u", load.Spec.Id.c_str(), load.PlacedCount);
                }
            }
            LOG_INFO_F("MEGA_STRESS_PLACED entities=%u requested=%u resources=%u sections=%u columns=%u rows=%u", placed,
                       data.m_EntityCount, resourceCount, sectionCount, columns, rows);
            LOG_INFO_F("MEGA_STRESS_MOVING movers=%zu percent=%.3f start_time=%.3f", movers->Items.size(),
                       static_cast<double>(data.m_MovingPercent), static_cast<double>(data.m_MovingStartTime));
            if (!movers->Items.empty())
            {
                // 置いた直後の 1 フレーム目から動いた位置で描く
                for (const StressMover &mover : movers->Items)
                {
                    ApplyMover(mover, data.m_MovingStartTime);
                }
                data.m_pMovers = movers;
            }
            data.m_bEntitiesPlaced = true;
            data.m_pAssembly.reset();
        }

        /// 読み込み中の全種類の材質がそろったか
        bool AreMaterialsReady(const StressAssembly &assembly)
        {
            for (const StressMeshLoad &load : assembly.Loads)
            {
                if (load.Material->PendingTextureCount != 0)
                {
                    return false;
                }
            }
            return true;
        }
    } // namespace

    GameModeEnterResult MegaStressRoutine::Enter(GameModeContext &ctx, MegaStressData &data)
    {
        LOG_INFO_F("mega-stress を開始します（Entity 数 %u）", data.m_EntityCount);
        data.m_TickCount = 0;
        data.m_bEntitiesPlaced = false;
        data.m_pMovers.reset();
        data.m_MovingElapsed = 0.0f;
        Game::Scenes::BeginSceneAssembly(ctx.EngineRef);

        const Game::Scenes::SceneDescriptor *scene = Game::Scenes::FindScene(Game::Scenes::kMegaStressSceneId);
        Game::Scenes::FreeCameraPose pose{};
        if (scene == nullptr || !Game::Scenes::ResolveFreeCameraPose(*scene, data.m_ViewName.c_str(), pose))
        {
            LOG_ERROR("mega-stress: シーンの一覧に視点の定義がありません");
            Game::Scenes::EndSceneAssembly(ctx.EngineRef);
            return GameModeEnterResult::Failed;
        }

        ApplyEnvironment(ctx);
        if (!CreateFloor(ctx))
        {
            ResetEnvironment(ctx);
            Game::Scenes::EndSceneAssembly(ctx.EngineRef);
            return GameModeEnterResult::Failed;
        }
        data.m_Rig.ConfigurePath(scene->CameraPath, data.m_CameraPathTime);
        if (!data.m_Rig.Enter(ctx, pose, data.m_CameraScriptPath.c_str()))
        {
            LOG_ERROR("mega-stress: カメラを作れませんでした");
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

        if (StartMeshLoads(ctx, data) == 0)
        {
            // 置けるメッシュが 1 つも無い。床だけでシーンの組み立てを終える
            data.m_bEntitiesPlaced = true;
            LOG_WARNING("MEGA_STRESS_PLACED entities=0 resources=0 sections=0 置けるメッシュがありません");
        }
        return GameModeEnterResult::Succeeded;
    }

    void MegaStressRoutine::Tick(GameModeContext &ctx, MegaStressData &data, float deltaTime)
    {
        ++data.m_TickCount;
        const bool bSuspended = data.m_pPauseMenu != nullptr && data.m_pPauseMenu->IsOpen();
        data.m_Rig.Tick(ctx, deltaTime, bSuspended);

        if (data.m_pAssembly && AreMaterialsReady(*data.m_pAssembly))
        {
            FinishAssembly(ctx, data);
        }

        // 動く Entity を毎フレーム動かす（ポーズ中は止める）
        if (data.m_pMovers && !bSuspended)
        {
            data.m_MovingElapsed += deltaTime;
            const float time = data.m_MovingStartTime + data.m_MovingElapsed;
            for (const StressMover &mover : data.m_pMovers->Items)
            {
                ApplyMover(mover, time);
            }
        }

        // 台本を流し終えるまで、決定的な撮影を待たせる（撮影は台本の後のカメラで撮る）
        Game::Scenes::UpdateSceneAssembly(ctx.EngineRef, Game::Scenes::kMegaStressSceneId,
                                          data.m_TickCount >= kReadyTickCount && data.m_bEntitiesPlaced &&
                                              !data.m_Rig.IsScriptRunning());
    }

    void MegaStressRoutine::Leave(GameModeContext &ctx, MegaStressData &data, GameModeExitReason)
    {
        Game::Scenes::EndSceneAssembly(ctx.EngineRef);
        // 描画スレッドは、影・MegaGeometry のパスの間、モデルの GPU データを生のポインタで読む。Cleanup がモデルを返す
        // 瞬間に描画中のフレームが残っていると解放済みのデータを読むので、先にそれまでのフレームの描画を完了させる。
        ctx.EngineRef.GetRenderWorld().WaitForRender();
        // Entity の実体はスコープが返すので、動かす一覧は先に手放す
        data.m_pMovers.reset();
        // 届いていない読み込みの結果は捨てる
        if (data.m_pAssembly)
        {
            for (StressMeshLoad &load : data.m_pAssembly->Loads)
            {
                load.Material->bCancelled = true;
                // すでに届いた仮想テクスチャは、まだ追跡していないので自分で返す
                for (const TextureHandle texture : load.Material->VirtualTextures)
                {
                    ctx.RenderResourcesRef.Textures().ReleaseTexture(texture);
                }
                load.Material->VirtualTextures.clear();
            }
            data.m_pAssembly.reset();
        }
        data.m_Rig.Leave(ctx);
        // 床・Entity・モデル・材質・メッシュ・カメラの実体は GameModeScope::Cleanup が返す。空は RenderWorld の設定なので戻す。
        ResetEnvironment(ctx);
        LOG_INFO("mega-stress を終了します");
    }

} // namespace Game::GameModes

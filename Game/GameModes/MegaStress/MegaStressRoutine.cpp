#include "GameModes/MegaStress/MegaStressMode.h"

#include "Core/Public/Asset/AssetSystem.h"
#include "Core/Public/Asset/CookedMeshFormat.h"
#include "Core/Public/Component/CameraComponent.h"
#include "Core/Public/Component/MegaGeometryComponent.h"
#include "Core/Public/Component/MegaGeometryScatterComponent.h"
#include "Core/Public/Component/MeshComponent.h"
#include "Core/Public/Debug/DebugConfig.h"
#include "Core/Public/Ecs/EcsWorld.h"
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
#include "GameModes/MegaBananas/BananaLayout.h"
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
        /// 原点から最も遠い頂点までの水平の距離（拡大後）。向きによらず Entity の足元がこの円に収まる
        float FootprintRadius = 0.0f;
        /// 面の法線の和の水平成分と表面積の比（閉じた岩・底だけ開いた岩は 0 に近く、背面の無い崖のスキャンは大きい）
        float OpenSideRatio = 0.0f;
        /// 横に開いた殻（背面の無いスキャン）のとき true。表の向き（ローカルの XZ で +Z から測った角[rad]）を FrontYaw に持つ
        bool bOpenSide = false;
        float FrontYaw = 0.0f;
        uint32_t PlacedCount = 0;
    };

    struct StressAssembly
    {
        VariableArray<StressMeshLoad> Loads;
        /// 同時に置くバナナ（--banana-count を指定したときだけ）。Loads とは別に持ち、Entity の種類には選ばない
        TSharedPtr<StressMeshLoad> Banana;
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

    /// 置いた 1 つの Entity の記録。区画を読み込み直すときに、同じ種類・位置・向きで置き直す
    struct StressPlaced
    {
        /// StressAssembly::Loads の添字
        uint32_t Kind = 0;
        float X = 0.0f;
        float Z = 0.0f;
        float Yaw = 0.0f;
        /// 動く Entity のときの StressMovers::Items の添字。動かないなら -1
        int32_t MoverSlot = -1;
        /// 読み込み中の Entity。解放中は nullptr（区画の散布にまとめた動かない Entity は、いつも nullptr）
        Entity *pEntity = nullptr;
    };

    /// 読み込み・解放の単位の区画
    struct StressStreamCell
    {
        /// 区画の中の StressPlaced の添字
        VariableArray<uint32_t> Members;
        float CenterX = 0.0f;
        float CenterZ = 0.0f;
        bool bLoaded = true;
        /// 区画の動かない Entity をまとめた散布の Entity（--stress-batch-cells のときだけ。解放中は nullptr）と、その行の数
        Entity *pBatch = nullptr;
        uint32_t BatchRows = 0;
    };

    struct StressStream
    {
        /// 種類ごとの MegaMesh（置き直しに使う。FinishAssembly の後も持ち続ける）
        TSharedPtr<StressAssembly> Assembly;
        /// 区画の動かない Entity を、区画ごとの散布 1 つにまとめる（--stress-batch-cells）
        bool bBatch = false;
        /// 散布のメッシュの表（StressAssembly::Loads の添字の順。置けない種類は無効のハンドル）
        VariableArray<MegaMeshHandle> MeshTable;
        /// 読み込み中の区画の散布の数
        uint32_t LiveBatches = 0;
        VariableArray<StressPlaced> Placed;
        VariableArray<StressStreamCell> Cells;
        /// 解放中の区画（解放した順。先頭から読み込み直す）
        VariableArray<uint32_t> Unloaded;
        uint32_t WindowCells = 0;
        /// 読み込み中の Entity の数
        uint32_t LiveEntities = 0;
        /// 置き終えた直後の World の Entity の数（周期の後に戻ることの基準）
        size_t BaselineWorldObjects = 0;
        /// 置き終えた直後の材質・メッシュ・モデル（MegaMesh）の数（周期の後に戻ることの基準）
        size_t BaselineMaterials = 0;
        size_t BaselineMeshes = 0;
        size_t BaselineModels = 0;
        /// 周期の経過時間[秒]
        float Elapsed = 0.0f;
        uint32_t Steps = 0;
        uint32_t Cycles = 0;
        /// 周期を回す回数の上限（0 なら止めない）
        uint32_t MaxCycles = 0;
        /// 解放した区画を読み戻している最中か（窓に達したら真、全部戻ったら偽）
        bool bReloading = false;
        /// 解放した区画が全部戻った直後（次の Tick で数の一致を確かめる）
        bool bCheckPending = false;
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

        // 横に開いた殻とみなす OpenSideRatio の下限。閉じた岩・底だけ開いた岩は 0.035 未満、背面の無い崖のスキャンは 0.48〜0.59。
        // 開いた殻の動かない Entity は、開いた側が視点（どの視点もフィールドの手前の原点付近から +Z を見る）に向かないよう、
        // 表を原点の方へ向け、向きのばらつきは ±kOpenSideYawSpread[rad] に収める
        constexpr float kOpenSideRatioMin = 0.2f;
        constexpr float kOpenSideYawSpread = 30.0f * kPi / 180.0f;

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
                                 bool bTryVirtualTexture,
                                 TextureColorEncoding colorEncoding = TextureColorEncoding::Default)
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
                    [pTextures, update, path, onLoaded, colorEncoding](TextureHandle handle)
                    {
                        if (handle.IsValid() || update->bCancelled)
                        {
                            onLoaded(handle);
                            return;
                        }
                        LOG_WARNING_F("MEGA_STRESS_VT_FALLBACK path=%s 仮想テクスチャのミップテイルが常駐しないため、"
                                      "全常駐で読み直します",
                                      path.c_str());
                        pTextures->LoadTextureAsync(path, onLoaded, colorEncoding);
                    },
                    colorEncoding);
                if (bStarted)
                {
                    return;
                }
            }
            textures.LoadTextureAsync(path, onLoaded, colorEncoding);
        }

        /// 常駐の粗い段の三角形の、面積を掛けた法線の和から、横に開いた殻かどうかと表の向きを決める。
        /// 閉じたメッシュでは和が 0 になり、開いた殻では開いた側の逆（表の側）を向く。
        /// 三角形の向きは巻き方でなく頂点の法線の平均で決める（巻き方の規約によらない）
        void MeasureOpenSide(StressMeshLoad &load)
        {
            const Asset::CookedMeshData &mesh = *load.Mesh;
            const size_t begin = mesh.FallbackIndexOffset;
            const size_t end = begin + mesh.FallbackIndexCount;
            if (mesh.FallbackIndexCount < 3 || end > mesh.Indices.size())
            {
                return;
            }
            double sumX = 0.0;
            double sumZ = 0.0;
            double totalArea = 0.0;
            for (size_t i = begin; i + 2 < end; i += 3)
            {
                const uint32_t ia = mesh.Indices[i];
                const uint32_t ib = mesh.Indices[i + 1];
                const uint32_t ic = mesh.Indices[i + 2];
                if (ia >= mesh.Vertices.size() || ib >= mesh.Vertices.size() || ic >= mesh.Vertices.size())
                {
                    continue;
                }
                const Asset::CookedMeshVertex &a = mesh.Vertices[ia];
                const Asset::CookedMeshVertex &b = mesh.Vertices[ib];
                const Asset::CookedMeshVertex &c = mesh.Vertices[ic];
                const double e1x = b.Position.X - a.Position.X;
                const double e1y = b.Position.Y - a.Position.Y;
                const double e1z = b.Position.Z - a.Position.Z;
                const double e2x = c.Position.X - a.Position.X;
                const double e2y = c.Position.Y - a.Position.Y;
                const double e2z = c.Position.Z - a.Position.Z;
                const double cx = e1y * e2z - e1z * e2y;
                const double cy = e1z * e2x - e1x * e2z;
                const double cz = e1x * e2y - e1y * e2x;
                const double area = std::sqrt(cx * cx + cy * cy + cz * cz);
                const double nx = static_cast<double>(a.Normal.X) + b.Normal.X + c.Normal.X;
                const double ny = static_cast<double>(a.Normal.Y) + b.Normal.Y + c.Normal.Y;
                const double nz = static_cast<double>(a.Normal.Z) + b.Normal.Z + c.Normal.Z;
                const double nLength = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (!(area > 0.0) || !(nLength > 0.0))
                {
                    continue;
                }
                sumX += nx / nLength * area;
                sumZ += nz / nLength * area;
                totalArea += area;
            }
            if (!(totalArea > 0.0))
            {
                return;
            }
            load.OpenSideRatio = static_cast<float>(std::sqrt(sumX * sumX + sumZ * sumZ) / totalArea);
            load.bOpenSide = load.OpenSideRatio >= kOpenSideRatioMin;
            load.FrontYaw = static_cast<float>(std::atan2(sumX, sumZ));
        }

        /// 動かない Entity の向き。開いた殻は表を原点の方へ向け、置いた番号の向き yaw をばらつきに使う。それ以外は yaw のまま。
        /// World は行ベクトルの規約なので、Y 軸まわりの回転 θ はローカルの XZ の向き φ をワールドの φ − θ へ写す
        float FacingYaw(const StressMeshLoad &load, float x, float z, float yaw)
        {
            if (!load.bOpenSide)
            {
                return yaw;
            }
            const float towardOrigin = std::atan2(-x, -z);
            const float spread = (yaw / (2.0f * kPi) - 0.5f) * 2.0f * kOpenSideYawSpread;
            return load.FrontYaw - towardOrigin + spread;
        }

        /// メッシュ 1 種類のクック済みメッシュを解決・解析し、材質のテクスチャの読み込みを始める。
        /// クック済みが無い・壊れているときは理由を警告して false を返す（その種類を読み飛ばす）
        bool StartMeshLoad(GameModeContext &ctx, const TSharedPtr<const Asset::AssetSystem> &assetSystem,
                           const StressMeshSpec &spec, StressMeshLoad &load)
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
            float maxRadiusSquared = 0.0f;
            for (const Asset::CookedMeshVertex &vertex : load.Mesh->Vertices)
            {
                minY = std::min(minY, vertex.Position.Y);
                maxY = std::max(maxY, vertex.Position.Y);
                maxRadiusSquared =
                    std::max(maxRadiusSquared, vertex.Position.X * vertex.Position.X + vertex.Position.Z * vertex.Position.Z);
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
            load.FootprintRadius = std::sqrt(maxRadiusSquared) * load.BaseScale;

            LOG_INFO_F("MEGA_STRESS_MESH id=%s path=%s vertices=%u indices=%u clusters=%u lod_levels=%u pages=%zu "
                       "height=%.3f scale=%.4f",
                       spec.Id.c_str(), spec.MeshPath.c_str(), static_cast<unsigned int>(load.Mesh->Vertices.size()),
                       static_cast<unsigned int>(load.Mesh->Indices.size()),
                       static_cast<unsigned int>(load.Mesh->Clusters.size()),
                       static_cast<unsigned int>(load.Mesh->LODLevelCount), load.Mesh->Pages.size(),
                       static_cast<double>(maxY - minY), static_cast<double>(load.BaseScale));
            MeasureOpenSide(load);
            LOG_INFO_F("MEGA_STRESS_OPEN_SIDE id=%s ratio=%.3f open=%d front_yaw_deg=%.1f", spec.Id.c_str(),
                       static_cast<double>(load.OpenSideRatio), load.bOpenSide ? 1 : 0,
                       static_cast<double>(load.FrontYaw * 180.0f / kPi));

            auto pending = MakeShared<StressPendingMaterial>();
            pending->CreateData.DebugName = spec.Id.c_str();
            pending->PendingTextureCount = 3;
            load.Material = pending;
            auto &textures = ctx.RenderResourcesRef.Textures();
            // glTF の模型の基本色は sRGB で符号化されているので、sRGB の形式のまま読む。変位させた球は起動画面の
            // 球と同じ材質のままにする
            LoadMaterialTexture(textures, pending, spec.Albedo, &MaterialCreateData::AlbedoTexture, false,
                                spec.bVirtualTexture,
                                spec.bDisplacedSphere ? TextureColorEncoding::Default : TextureColorEncoding::Srgb);
            LoadMaterialTexture(textures, pending, spec.Normal, &MaterialCreateData::NormalTexture, true,
                                spec.bVirtualTexture);
            LoadMaterialTexture(textures, pending, spec.Orm, &MaterialCreateData::ORMTexture, false, spec.bVirtualTexture);
            return true;
        }

        /// 同時に置くバナナの読み込みを始める。資産が無い・壊れているときは警告して false を返す（バナナ抜きで続ける）
        bool StartBananaLoad(GameModeContext &ctx, const TSharedPtr<const Asset::AssetSystem> &assetSystem,
                             StressAssembly &assembly)
        {
            StressMeshSpec spec;
            spec.Id = "banana";
            spec.MeshPath = kBananaMeshPath;
            spec.Albedo = String(kBananaMeshPath) + kBananaAlbedoSuffix;
            spec.Normal = String(kBananaMeshPath) + kBananaNormalSuffix;
            spec.Orm = String(kBananaMeshPath) + kBananaOrmSuffix;
            auto load = MakeShared<StressMeshLoad>();
            if (!StartMeshLoad(ctx, assetSystem, spec, *load))
            {
                LOG_WARNING_F("MEGA_STRESS_BANANAS_SKIPPED path=%s バナナを置けません。%s で生成し、CookAssets で焼いてください",
                              kBananaMeshPath, kBananaFetchHint);
                return false;
            }
            assembly.Banana = load;
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
                StressMeshLoad load;
                if (StartMeshLoad(ctx, assetSystem, spec, load))
                {
                    assembly->Loads.push_back(std::move(load));
                }
            }
            const uint32_t started = static_cast<uint32_t>(assembly->Loads.size());
            const bool bBanana = data.m_BananaCount > 0 && StartBananaLoad(ctx, assetSystem, *assembly);
            LOG_INFO_F("MEGA_STRESS_LIST listed=%zu loading=%u banana=%d", specs.size(), started, bBanana ? 1 : 0);
            if (started > 0 || bBanana)
            {
                data.m_pAssembly = assembly;
            }
            return started + (bBanana ? 1u : 0u);
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

        /// 同時に置くバナナの敷き詰めから、置いた Entity の足元（動く Entity は動く範囲）を空けるマスク。
        /// Entity は升目（一辺 kCellMeters）ごとに 1 つで、升目の中心から kCellJitterMeters 以内にあるので、
        /// 問う位置の周りの升目だけを調べる
        class StressFootprintMask final : public IBananaFieldMask
        {
        public:
            /// バナナの中心から先端までの長さ（約 0.125 m）に余白を足した、足元の円の外へ空ける距離
            static constexpr float kBananaClearanceMeters = 0.15f;

            void Reset(uint32_t columns, uint32_t rows)
            {
                m_Columns = columns;
                m_Rows = rows;
                m_SlotItem.assign(static_cast<size_t>(columns) * rows, -1);
                m_Items.clear();
                m_MaxRadius = 0.0f;
            }

            /// 升目 (column, row) に置いた Entity の足元を足す。足した順の番号（置いた順）で Grow から引く
            void Add(uint32_t column, uint32_t row, float x, float z, float footprintRadius)
            {
                m_SlotItem[static_cast<size_t>(row) * m_Columns + column] = static_cast<int32_t>(m_Items.size());
                m_Items.push_back(Item{x, z, footprintRadius + kBananaClearanceMeters});
                m_MaxRadius = std::max(m_MaxRadius, m_Items.back().Radius);
            }

            /// ordinal 番目に足した足元の半径を extra だけ広げる（動く Entity の円運動の半径）
            void Grow(size_t ordinal, float extra)
            {
                if (ordinal < m_Items.size())
                {
                    m_Items[ordinal].Radius += extra;
                    m_MaxRadius = std::max(m_MaxRadius, m_Items[ordinal].Radius);
                }
            }

            bool IsBlocked(float x, float z) const override
            {
                if (m_Items.empty())
                {
                    return false;
                }
                const float columnCoord = x / kCellMeters + 0.5f * static_cast<float>(m_Columns - 1);
                const float rowCoord = (z - kFieldNearZ) / kCellMeters;
                const int reach = static_cast<int>(std::ceil((m_MaxRadius + kCellJitterMeters) / kCellMeters));
                const int centerColumn = static_cast<int>(std::lround(columnCoord));
                const int centerRow = static_cast<int>(std::lround(rowCoord));
                for (int row = centerRow - reach; row <= centerRow + reach; ++row)
                {
                    if (row < 0 || row >= static_cast<int>(m_Rows))
                    {
                        continue;
                    }
                    for (int column = centerColumn - reach; column <= centerColumn + reach; ++column)
                    {
                        if (column < 0 || column >= static_cast<int>(m_Columns))
                        {
                            continue;
                        }
                        const int32_t item = m_SlotItem[static_cast<size_t>(row) * m_Columns + static_cast<size_t>(column)];
                        if (item < 0)
                        {
                            continue;
                        }
                        const Item &footprint = m_Items[static_cast<size_t>(item)];
                        const float dx = x - footprint.X;
                        const float dz = z - footprint.Z;
                        if (dx * dx + dz * dz < footprint.Radius * footprint.Radius)
                        {
                            return true;
                        }
                    }
                }
                return false;
            }

        private:
            struct Item
            {
                float X = 0.0f;
                float Z = 0.0f;
                float Radius = 0.0f;
            };

            uint32_t m_Columns = 0;
            uint32_t m_Rows = 0;
            /// 升目ごとの m_Items の添字（置いていない升目は -1）
            VariableArray<int32_t> m_SlotItem;
            VariableArray<Item> m_Items;
            float m_MaxRadius = 0.0f;
        };

        /// バナナの散布（Object 1 つと軽い行）を置く。置いた本数を返す（置けなければ 0）
        uint32_t PlaceStressBananas(GameModeContext &ctx, const MegaStressData &data, const StressMeshLoad &banana,
                                    const StressFootprintMask &mask, uint32_t &outPileCount)
        {
            BananaShape shape;
            BuildBananaShape(*banana.Mesh, shape);
            VariableArray<Component::MegaScatterInstance> instances;
            instances.reserve(data.m_BananaCount);
            const uint32_t laid = PlaceBananas(instances, shape, data.m_BananaCount, &mask, outPileCount);
            uint32_t placed = 0;
            uint32_t scatterRows = 0;
            uint32_t scatterRanges = 0;
            Entity *owner = laid > 0 ? ctx.ScopeRef.SpawnObject<Entity>() : nullptr;
            auto *scatter =
                owner != nullptr ? ctx.WorldRef.CreateComponent<Component::MegaGeometryScatterComponent>(owner) : nullptr;
            if (scatter != nullptr)
            {
                scatter->SetMegaMeshHandle(banana.MegaMesh);
                scatter->SetCastShadow(true);
                if (scatter->SetInstances(Span<const Component::MegaScatterInstance>(instances.data(), instances.size())))
                {
                    placed = laid;
                    scatterRows = scatter->GetInstanceCount();
                    scatterRanges = scatter->GetRangeCount();
                }
            }
            if (placed == 0)
            {
                LOG_ERROR("mega-stress: バナナの散布を作れませんでした");
                if (owner != nullptr)
                {
                    ctx.ScopeRef.Untrack(owner);
                    ctx.WorldRef.RemoveObject(owner);
                }
                outPileCount = 0;
            }
            LOG_INFO_F("MEGA_STRESS_BANANAS count=%u requested=%u pile=%u field=%u scatter_rows=%u scatter_ranges=%u", placed,
                       data.m_BananaCount, outPileCount, placed - outPileCount, scatterRows, scatterRanges);
            return placed;
        }

        uint32_t SpawnCellBatch(GameModeContext &ctx, StressStream &stream, uint32_t cellIndex);

        /// 置いた Entity を区画に分ける（升目の範囲を一辺 data.m_StreamCellMeters[m] の区画で区切る）。
        /// 区画の位置はフィールドの手前の左端を原点にした升目で決まり、Entity の置き方（番号 → 位置）は変えない
        void BuildStream(GameModeContext &ctx, const MegaStressData &data, StressStream &stream, uint32_t columns,
                         uint32_t rows)
        {
            const float cell = data.m_StreamCellMeters;
            const float originX = -0.5f * static_cast<float>(columns - 1) * kCellMeters - kCellJitterMeters;
            const float originZ = kFieldNearZ - kCellJitterMeters;
            const float extentX = static_cast<float>(columns - 1) * kCellMeters + 2.0f * kCellJitterMeters;
            const float extentZ = static_cast<float>(rows - 1) * kCellMeters + 2.0f * kCellJitterMeters;
            const uint32_t cellsX = static_cast<uint32_t>(std::floor(extentX / cell)) + 1;
            const uint32_t cellsZ = static_cast<uint32_t>(std::floor(extentZ / cell)) + 1;
            VariableArray<int32_t> slotOfCell(static_cast<size_t>(cellsX) * cellsZ, -1);
            for (uint32_t i = 0; i < static_cast<uint32_t>(stream.Placed.size()); ++i)
            {
                const StressPlaced &record = stream.Placed[i];
                const uint32_t cx = std::min(cellsX - 1, static_cast<uint32_t>(std::max(0.0f, (record.X - originX) / cell)));
                const uint32_t cz = std::min(cellsZ - 1, static_cast<uint32_t>(std::max(0.0f, (record.Z - originZ) / cell)));
                int32_t &slot = slotOfCell[static_cast<size_t>(cz) * cellsX + cx];
                if (slot < 0)
                {
                    slot = static_cast<int32_t>(stream.Cells.size());
                    StressStreamCell added;
                    added.CenterX = originX + (static_cast<float>(cx) + 0.5f) * cell;
                    added.CenterZ = originZ + (static_cast<float>(cz) + 0.5f) * cell;
                    stream.Cells.push_back(std::move(added));
                }
                stream.Cells[static_cast<size_t>(slot)].Members.push_back(i);
            }
            stream.WindowCells = std::min<uint32_t>(kStreamWindowCells, static_cast<uint32_t>(stream.Cells.size()));
            stream.MaxCycles = data.m_StreamMaxCycles;
            stream.LiveEntities = static_cast<uint32_t>(stream.Placed.size());
            // 区画の動かない Entity を区画ごとの散布にまとめて置く（基準の数は置いた後に取る）
            uint32_t batchRows = 0;
            if (stream.bBatch)
            {
                for (uint32_t cellIndex = 0; cellIndex < static_cast<uint32_t>(stream.Cells.size()); ++cellIndex)
                {
                    batchRows += SpawnCellBatch(ctx, stream, cellIndex);
                }
            }
            stream.BaselineWorldObjects = ctx.WorldRef.GetObjectCount();
            stream.BaselineMaterials = ctx.RenderResourcesRef.Materials().GetCount();
            stream.BaselineMeshes = ctx.RenderResourcesRef.Meshes().GetCount();
            stream.BaselineModels = ctx.RenderResourcesRef.MegaGeometry().GetModelCount();
            LOG_INFO_F("MEGA_STRESS_STREAM_SETUP cell_m=%.1f period=%.3f cells=%zu window=%u entities=%u batch=%d batches=%u "
                       "batch_rows=%u world_objects=%zu materials=%zu meshes=%zu models=%zu",
                       static_cast<double>(cell), static_cast<double>(data.m_StreamPeriod), stream.Cells.size(),
                       stream.WindowCells, stream.LiveEntities, stream.bBatch ? 1 : 0, stream.LiveBatches, batchRows,
                       stream.BaselineWorldObjects, stream.BaselineMaterials, stream.BaselineMeshes, stream.BaselineModels);
        }

        /// 区画 1 つの動かない Entity を、散布 1 つ（Entity 1 つと軽い行）にまとめて置く。置いた行の数を返す。
        /// 行の作成は仕事の列が予算で範囲ごとに進める（範囲の全行がそろってから描く）
        uint32_t SpawnCellBatch(GameModeContext &ctx, StressStream &stream, uint32_t cellIndex)
        {
            StressStreamCell &cell = stream.Cells[cellIndex];
            VariableArray<Component::MegaScatterInstance> instances;
            instances.reserve(cell.Members.size());
            for (const uint32_t member : cell.Members)
            {
                const StressPlaced &record = stream.Placed[member];
                if (record.MoverSlot >= 0)
                {
                    continue;
                }
                // 1 つずつ置くときの Entity の変換（SpawnStressEntity）と同じ値を、散布の入力の局所の変換にする（散布の Entity は原点）
                const StressMeshLoad &load = stream.Assembly->Loads[record.Kind];
                Component::MegaScatterInstance instance;
                instance.Position = Math::Vector3(record.X, load.BaseY, record.Z);
                instance.Rotation = Math::Quaternion(Math::Vector3(0.0f, 1.0f, 0.0f), record.Yaw);
                instance.Scale = Math::Vector3(load.BaseScale, load.BaseScale, load.BaseScale);
                instance.MeshIndex = record.Kind;
                instances.push_back(instance);
            }
            if (instances.empty())
            {
                return 0;
            }
            Entity *owner = ctx.ScopeRef.SpawnObject<Entity>();
            if (owner == nullptr)
            {
                return 0;
            }
            auto *scatter = ctx.WorldRef.CreateComponent<Component::MegaGeometryScatterComponent>(owner);
            if (scatter == nullptr)
            {
                ctx.ScopeRef.Untrack(owner);
                ctx.WorldRef.RemoveObject(owner);
                return 0;
            }
            scatter->SetMegaMeshHandles(Span<const MegaMeshHandle>(stream.MeshTable.data(), stream.MeshTable.size()));
            scatter->SetCastShadow(true);
            if (!scatter->SetInstances(Span<const Component::MegaScatterInstance>(instances.data(), instances.size())))
            {
                LOG_WARNING_F("mega-stress: 区画 %u の散布を作れませんでした", cellIndex);
                ctx.ScopeRef.Untrack(owner);
                ctx.WorldRef.RemoveObject(owner);
                return 0;
            }
            cell.pBatch = owner;
            cell.BatchRows = static_cast<uint32_t>(instances.size());
            ++stream.LiveBatches;
            return cell.BatchRows;
        }

        /// 区画 1 つの Entity を全部解放する。動く Entity の参照は外す
        uint32_t UnloadCell(GameModeContext &ctx, MegaStressData &data, StressStream &stream, uint32_t cellIndex)
        {
            StressStreamCell &cell = stream.Cells[cellIndex];
            uint32_t released = 0;
            // 区画の散布は Entity ごと外す（proxy はすぐ取り下げ、行の削除は仕事の列が予算で進める）
            if (cell.pBatch != nullptr)
            {
                ctx.ScopeRef.Untrack(cell.pBatch);
                ctx.WorldRef.RemoveObject(cell.pBatch);
                cell.pBatch = nullptr;
                released += cell.BatchRows;
                --stream.LiveBatches;
            }
            for (const uint32_t member : cell.Members)
            {
                StressPlaced &record = stream.Placed[member];
                if (record.pEntity == nullptr)
                {
                    continue;
                }
                if (record.MoverSlot >= 0 && data.m_pMovers)
                {
                    data.m_pMovers->Items[static_cast<size_t>(record.MoverSlot)].pEntity = nullptr;
                }
                ctx.ScopeRef.Untrack(record.pEntity);
                ctx.WorldRef.RemoveObject(record.pEntity);
                record.pEntity = nullptr;
                ++released;
            }
            cell.bLoaded = false;
            stream.LiveEntities -= released;
            return released;
        }

        /// 区画 1 つの Entity を同じ種類・位置・向きで置き直す
        uint32_t LoadCell(GameModeContext &ctx, MegaStressData &data, StressStream &stream, uint32_t cellIndex)
        {
            StressStreamCell &cell = stream.Cells[cellIndex];
            uint32_t created = 0;
            const float time = data.m_MovingStartTime + data.m_MovingElapsed;
            if (stream.bBatch && cell.pBatch == nullptr)
            {
                created += SpawnCellBatch(ctx, stream, cellIndex);
            }
            for (const uint32_t member : cell.Members)
            {
                StressPlaced &record = stream.Placed[member];
                // 散布にまとめた動かない Entity は、1 つずつは置かない
                if (record.pEntity != nullptr || (stream.bBatch && record.MoverSlot < 0))
                {
                    continue;
                }
                record.pEntity = SpawnStressEntity(ctx, stream.Assembly->Loads[record.Kind], record.X, record.Z, record.Yaw);
                if (record.pEntity == nullptr)
                {
                    continue;
                }
                if (record.MoverSlot >= 0 && data.m_pMovers)
                {
                    StressMover &mover = data.m_pMovers->Items[static_cast<size_t>(record.MoverSlot)];
                    mover.pEntity = record.pEntity;
                    ApplyMover(mover, time);
                }
                ++created;
            }
            cell.bLoaded = true;
            stream.LiveEntities += created;
            return created;
        }

        /// 周期ごとの 1 手。解放中の区画が窓に満たなければ、カメラに最も近い読み込み中の区画を解放し、
        /// 窓に達したら解放した順に 1 つ読み込み直す
        void StreamStep(GameModeContext &ctx, MegaStressData &data, StressStream &stream)
        {
            ++stream.Steps;
            if (!stream.bReloading)
            {
                float cameraX = 0.0f;
                float cameraZ = 0.0f;
                if (const Component::CameraComponent *camera = data.m_Rig.GetCamera())
                {
                    if (const Entity *owner = camera->GetOwner())
                    {
                        cameraX = owner->GetPosition().x;
                        cameraZ = owner->GetPosition().z;
                    }
                }
                int64_t best = -1;
                float bestDistance = std::numeric_limits<float>::max();
                for (size_t index = 0; index < stream.Cells.size(); ++index)
                {
                    const StressStreamCell &cell = stream.Cells[index];
                    if (!cell.bLoaded)
                    {
                        continue;
                    }
                    const float dx = cell.CenterX - cameraX;
                    const float dz = cell.CenterZ - cameraZ;
                    const float distance = dx * dx + dz * dz;
                    if (distance < bestDistance)
                    {
                        bestDistance = distance;
                        best = static_cast<int64_t>(index);
                    }
                }
                if (best < 0)
                {
                    return;
                }
                const uint32_t cellIndex = static_cast<uint32_t>(best);
                const uint32_t released = UnloadCell(ctx, data, stream, cellIndex);
                stream.Unloaded.push_back(cellIndex);
                stream.bReloading = stream.Unloaded.size() >= stream.WindowCells;
                LOG_INFO_F("MEGA_STRESS_STREAM_STEP step=%u action=unload cell=%u entities=%u live=%u batches=%u", stream.Steps,
                           cellIndex, released, stream.LiveEntities, stream.LiveBatches);
                return;
            }
            const uint32_t cellIndex = stream.Unloaded.front();
            stream.Unloaded.erase(stream.Unloaded.begin());
            const uint32_t created = LoadCell(ctx, data, stream, cellIndex);
            LOG_INFO_F("MEGA_STRESS_STREAM_STEP step=%u action=load cell=%u entities=%u live=%u batches=%u", stream.Steps,
                       cellIndex, created, stream.LiveEntities, stream.LiveBatches);
            if (stream.Unloaded.empty())
            {
                stream.bReloading = false;
                stream.bCheckPending = true;
            }
        }

        /// 解放した区画が全部戻った後に、Entity の数が置き終えた直後と同じかを確かめる
        void CheckStreamCycle(GameModeContext &ctx, StressStream &stream)
        {
            stream.bCheckPending = false;
            ++stream.Cycles;
            const size_t worldObjects = ctx.WorldRef.GetObjectCount();
            const size_t materials = ctx.RenderResourcesRef.Materials().GetCount();
            const size_t meshes = ctx.RenderResourcesRef.Meshes().GetCount();
            const size_t models = ctx.RenderResourcesRef.MegaGeometry().GetModelCount();
            const bool bPass = stream.LiveEntities == static_cast<uint32_t>(stream.Placed.size()) &&
                               worldObjects == stream.BaselineWorldObjects && materials == stream.BaselineMaterials &&
                               meshes == stream.BaselineMeshes && models == stream.BaselineModels;
            LOG_INFO_F("MEGA_STRESS_STREAM_CYCLE cycle=%u steps=%u live=%u expected=%zu batches=%u world_objects=%zu "
                       "baseline=%zu materials=%zu/%zu meshes=%zu/%zu models=%zu/%zu result=%s",
                       stream.Cycles, stream.Steps, stream.LiveEntities, stream.Placed.size(), stream.LiveBatches, worldObjects,
                       stream.BaselineWorldObjects, materials, stream.BaselineMaterials, meshes, stream.BaselineMeshes,
                       models, stream.BaselineModels, bPass ? "pass" : "fail");
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
            // 同時に置くバナナ。置くときは、Entity の足元をバナナの敷き詰めから空けるマスクを置いた順に作る
            const bool bBananas = assembly.Banana && CreateMegaMeshForLoad(ctx, *assembly.Banana);
            StressFootprintMask footprints;

            auto movers = MakeShared<StressMovers>();
            // 置いた Entity のうち動かす候補（割合が 0 より大きいときだけ溜め、置き終わってから正確な個数を選ぶ）
            StressMovers candidates;
            // 区画の読み込み・解放をするときだけ、置いた Entity を記録する（候補の順番 = 記録の順番）
            const bool bStreaming = data.m_StreamCellMeters > 0.0f;
            // 区画の散布は区画を指定したときだけ。動かない Entity は 1 つずつ置かず、区画を作った後に区画ごとの散布で置く
            const bool bBatch = bStreaming && data.m_bBatchCells;
            if (data.m_bBatchCells && !bStreaming)
            {
                LOG_WARNING("mega-stress: --stress-batch-cells は --stress-stream-cell と一緒に指定してください"
                            "（1 つずつの Entity で置きます）");
            }
            auto stream = MakeShared<StressStream>();
            uint32_t placed = 0;
            uint32_t columns = 0;
            uint32_t rows = 0;
            if (resourceCount > 0)
            {
                columns = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(data.m_EntityCount) * 2.0f)));
                rows = (data.m_EntityCount + columns - 1) / columns;
                footprints.Reset(columns, rows);
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
                    // 置く向き（開いた殻は表を原点へ向ける）。動く Entity は置いた直後から番号の向き yaw を基準に回す
                    const float placedYaw = FacingYaw(load, x, z, yaw);
                    Entity *entity = bBatch ? nullptr : SpawnStressEntity(ctx, load, x, z, placedYaw);
                    if (entity != nullptr || bBatch)
                    {
                        ++load.PlacedCount;
                        ++placed;
                        if (bBananas)
                        {
                            footprints.Add(column, row, x, z, load.FootprintRadius);
                        }
                        if (bStreaming)
                        {
                            StressPlaced record;
                            record.Kind = static_cast<uint32_t>(kind);
                            record.X = x;
                            record.Z = z;
                            record.Yaw = placedYaw;
                            record.pEntity = entity;
                            stream->Placed.push_back(record);
                        }
                        if (data.m_MovingPercent > 0.0f)
                        {
                            candidates.Items.push_back(MakeMover(entity, index, load, x, z, yaw));
                        }
                    }
                }
            }

            // 動かす個数は置けた数 × 割合を四捨五入した値に揃え、どれを動かすかは番号のハッシュの小さい順に決める（実行ごとに同じ）
            if (!candidates.Items.empty())
            {
                struct MoverKey
                {
                    uint32_t Hash;
                    uint32_t Ordinal;
                };
                VariableArray<MoverKey> keys;
                for (size_t ordinal = 0; ordinal < candidates.Items.size(); ++ordinal)
                {
                    keys.push_back({HashIndex(static_cast<uint32_t>(ordinal) * 3266489917u + 0x165667b1u),
                                    static_cast<uint32_t>(ordinal)});
                }
                std::sort(keys.begin(), keys.end(), [](const MoverKey &a, const MoverKey &b) {
                    return a.Hash != b.Hash ? a.Hash < b.Hash : a.Ordinal < b.Ordinal;
                });
                const double rawCount = static_cast<double>(candidates.Items.size()) * static_cast<double>(data.m_MovingPercent) / 100.0;
                const size_t moverCount = std::min(candidates.Items.size(), static_cast<size_t>(rawCount + 0.5));
                // 選んだ分を置いた順に並べ直す
                std::sort(keys.begin(), keys.begin() + static_cast<std::ptrdiff_t>(moverCount),
                          [](const MoverKey &a, const MoverKey &b) { return a.Ordinal < b.Ordinal; });
                for (size_t i = 0; i < moverCount; ++i)
                {
                    movers->Items.push_back(candidates.Items[keys[i].Ordinal]);
                    // 動く Entity の周りは、円運動で通る範囲までバナナを空ける（候補の順 = 置いた順）
                    footprints.Grow(keys[i].Ordinal, candidates.Items[keys[i].Ordinal].Radius);
                    if (bStreaming)
                    {
                        stream->Placed[keys[i].Ordinal].MoverSlot = static_cast<int32_t>(i);
                    }
                }
            }
            // 区画の散布のときは、動く Entity だけを 1 つずつ置く（置いた順 = 記録の順）
            if (bBatch)
            {
                for (StressPlaced &record : stream->Placed)
                {
                    if (record.MoverSlot < 0)
                    {
                        continue;
                    }
                    record.pEntity = SpawnStressEntity(ctx, assembly.Loads[record.Kind], record.X, record.Z, record.Yaw);
                    movers->Items[static_cast<size_t>(record.MoverSlot)].pEntity = record.pEntity;
                }
            }

            uint32_t bananaCount = 0;
            uint32_t bananaPileCount = 0;
            if (bBananas)
            {
                bananaCount = PlaceStressBananas(ctx, data, *assembly.Banana, footprints, bananaPileCount);
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
            LOG_INFO_F("MEGA_STRESS_PLACED entities=%u requested=%u resources=%u sections=%u columns=%u rows=%u batch_cells=%d "
                       "bananas=%u bananas_requested=%u",
                       placed, data.m_EntityCount, resourceCount, sectionCount, columns, rows, bBatch ? 1 : 0, bananaCount,
                       data.m_BananaCount);
            LOG_INFO_F("MEGA_STRESS_MOVING movers=%zu percent=%.3f start_time=%.3f", movers->Items.size(),
                       static_cast<double>(data.m_MovingPercent), static_cast<double>(data.m_MovingStartTime));
            if (!movers->Items.empty())
            {
                // 置いた直後の 1 フレーム目から動いた位置で描く
                for (const StressMover &mover : movers->Items)
                {
                    if (mover.pEntity != nullptr)
                    {
                        ApplyMover(mover, data.m_MovingStartTime);
                    }
                }
                data.m_pMovers = movers;
            }
            if (bStreaming && placed > 0)
            {
                stream->Assembly = data.m_pAssembly;
                stream->bBatch = bBatch;
                for (const StressMeshLoad &load : assembly.Loads)
                {
                    stream->MeshTable.push_back(load.bReady ? load.MegaMesh : MegaMeshHandle::Invalid());
                }
                BuildStream(ctx, data, *stream, columns, rows);
                data.m_pStream = stream;
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
            return !assembly.Banana || assembly.Banana->Material->PendingTextureCount == 0;
        }
    } // namespace

    GameModeEnterResult MegaStressRoutine::Enter(GameModeContext &ctx, MegaStressData &data)
    {
        LOG_INFO_F("mega-stress を開始します（Entity 数 %u・バナナ %u 本）", data.m_EntityCount, data.m_BananaCount);
        data.m_TickCount = 0;
        data.m_bEntitiesPlaced = false;
        data.m_bMassWorkSettled = false;
        data.m_pMovers.reset();
        data.m_pStream.reset();
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
        data.m_Rig.ConfigurePath(scene->CameraPath, data.m_CameraPathTime, data.m_bCameraPathPlay);
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
            LOG_WARNING("MEGA_STRESS_PLACED entities=0 resources=0 sections=0 bananas=0 置けるメッシュがありません");
        }
        return GameModeEnterResult::Succeeded;
    }

    void MegaStressRoutine::Tick(GameModeContext &ctx, MegaStressData &data, float deltaTime)
    {
        ++data.m_TickCount;
        // 撮影の視点の切り替えは、このフレームのカメラを渡す前に置き直す
        if (const Game::Scenes::SceneDescriptor *scene = Game::Scenes::FindScene(Game::Scenes::kMegaStressSceneId))
        {
            data.m_Rig.ApplyRequestedView(data.m_pViewSwitch, *scene);
        }
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
                if (mover.pEntity != nullptr)
                {
                    ApplyMover(mover, time);
                }
            }
        }

        // 区画の読み込み・解放（ポーズ中は止める）。周期ごとに 1 手、1 フレームに 1 手まで
        if (data.m_pStream && !bSuspended)
        {
            StressStream &stream = *data.m_pStream;
            if (stream.bCheckPending)
            {
                CheckStreamCycle(ctx, stream);
            }
            // 上限まで回したら止める（cycle smoke が、Entity の数が落ち着くのを待てるように）
            const bool bFinished = stream.MaxCycles > 0 && stream.Cycles >= stream.MaxCycles;
            stream.Elapsed += deltaTime;
            if (!bFinished && stream.Elapsed >= data.m_StreamPeriod)
            {
                stream.Elapsed = 0.0f;
                StreamStep(ctx, data, stream);
            }
        }

        // 散布の行の作成が全範囲の公開まで進むのを待つ（一度そろえば、その後の区画の出し入れでは待たない）
        if (data.m_bEntitiesPlaced && !data.m_bMassWorkSettled && !ctx.WorldRef.GetEcsWorld().GetMassWork().HasPendingWork())
        {
            data.m_bMassWorkSettled = true;
        }

        // 台本を流し終えるまで、決定的な撮影を待たせる（撮影は台本の後のカメラで撮る）
        const bool bReady = data.m_TickCount >= kReadyTickCount && data.m_bEntitiesPlaced && data.m_bMassWorkSettled &&
                            !data.m_Rig.IsScriptRunning();
        if (bReady)
        {
            data.m_Rig.NotifySceneReady();
        }
        Game::Scenes::UpdateSceneAssembly(ctx.EngineRef, Game::Scenes::kMegaStressSceneId, bReady);
    }

    void MegaStressRoutine::Leave(GameModeContext &ctx, MegaStressData &data, GameModeExitReason)
    {
        Game::Scenes::EndSceneAssembly(ctx.EngineRef);
        // 描画スレッドは、影・MegaGeometry のパスの間、モデルの GPU データを生のポインタで読む。Cleanup がモデルを返す
        // 瞬間に描画中のフレームが残っていると解放済みのデータを読むので、先にそれまでのフレームの描画を完了させる。
        ctx.EngineRef.GetRenderWorld().WaitForRender();
        // Entity の実体はスコープが返すので、動かす一覧は先に手放す
        data.m_pMovers.reset();
        data.m_pStream.reset();
        // 届いていない読み込みの結果は捨てる
        if (data.m_pAssembly)
        {
            const auto cancel = [&ctx](StressMeshLoad &load)
            {
                load.Material->bCancelled = true;
                // すでに届いた仮想テクスチャは、まだ追跡していないので自分で返す
                for (const TextureHandle texture : load.Material->VirtualTextures)
                {
                    ctx.RenderResourcesRef.Textures().ReleaseTexture(texture);
                }
                load.Material->VirtualTextures.clear();
            };
            for (StressMeshLoad &load : data.m_pAssembly->Loads)
            {
                cancel(load);
            }
            if (data.m_pAssembly->Banana)
            {
                cancel(*data.m_pAssembly->Banana);
            }
            data.m_pAssembly.reset();
        }
        data.m_Rig.Leave(ctx);
        // 床・Entity・モデル・材質・メッシュ・カメラの実体は GameModeScope::Cleanup が返す。空は RenderWorld の設定なので戻す。
        ResetEnvironment(ctx);
        LOG_INFO("mega-stress を終了します");
    }

} // namespace Game::GameModes

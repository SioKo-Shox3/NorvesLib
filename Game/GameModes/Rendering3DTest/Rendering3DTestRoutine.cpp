#include "Rendering3DTestRoutine.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Component/BoardComponent.h"
#include "Core/Public/Component/TextComponent.h"
#include "Core/Public/Component/BillboardComponent.h"
#include "Core/Public/Component/ImpostorComponent.h"
#include "Core/Public/Component/MeshComponent.h"
#include "Core/Public/Component/MegaGeometryComponent.h"
#include "Core/Public/Component/LightComponent.h"
#include "Core/Public/Component/PointLightComponent.h"
#include "Core/Public/Component/CameraComponent.h"
#include "Core/Public/Component/SpringArmComponent.h"
#include "Core/Public/Rendering/RenderWorld.h"
#include "Core/Public/Rendering/RenderResourceContexts.h"
#include "Core/Public/Rendering/RenderResources.h"
#include "Core/Public/Rendering/CanvasView.h"
#include "Core/Public/Input/InputSystem.h"
#include "Core/Public/Input/InputState.h"
#include "Core/Public/Input/InputRouter.h"
#include "Core/Public/Rendering/ProceduralMeshGenerator.h"
#include "Core/Public/Rendering/ImpostorBake.h"
#include "Core/Public/Rendering/SceneProxy.h"
#include "Core/Public/Rendering/SceneView.h"
#include "Core/Public/Rendering/RenderingCoordinator.h"
#include "Core/Public/GameMode/GameModeScope.h"
#include "Core/Public/Debug/DebugConfig.h"
#include "Core/Public/Rendering/MegaGeometryPass.h"
#include "Core/Public/Resource/GLTFAnalyzer.h"
#include "Core/Public/Particle/ParticleSystem.h"
#include "Core/Public/Module/ModuleRegistry.h"
#include "GameModes/Rendering3DTest/M9WorldAcceptance.h"
#include "GameModes/Rendering3DTest/M9WorldSkeletal.h"

#if defined(NORVES_GAME_AUDIO)
#include "Audio/IAudioModule.h"
#endif

#include "Core/Public/Math/Matrix4x4.h"
#include "Core/Public/Math/Quaternion.h"
#include "Core/Public/Math/Vector3.h"
#include "GameModes/Rendering3DTest/Rendering3DTestDebugDraw.h"

// ImGui 有効時のみ、方向ライト編集 view を併走させる SubRoutine を引き込む。
// OFF 時はヘッダごとガードアウトされ空 TU となり push もガードアウトされる(挙動不変)。
#if defined(NORVES_ENABLE_IMGUI)
#include "Core/Public/GameMode/IGameModeController.h"  // RequestPushSubRoutine の完全定義
#include "GameModes/Rendering3DTest/DirectionalLightEditSubRoutine.h"
#endif
#include "GameModes/Rendering3DTest/SkySunControl.h"
#include "Core/Public/Rendering/VolumetricFog.h"
#include "Core/Public/Asset/AssetFileReader.h"
#include "Core/Public/Asset/AssetSystem.h"
#include "Core/Public/Asset/CookedMeshFormat.h"
#include "Core/Public/Rendering/MegaGeometry/CookedMeshMegaMeshAdapter.h"
#include "Core/Public/Rendering/MegaGeometry/StartupBigSphereSpec.h"
#include "Core/Public/RHI/ITexture.h"
#include "Core/Public/Thread/JobSystem.h"
#include "Core/Public/Thread/Task.h"

#include "stb_image.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <utility>

using namespace NorvesLib::Core::Container;
using namespace NorvesLib::Core::GameMode;
using namespace NorvesLib::Core::Rendering;
using namespace NorvesLib::Core::Rendering::MegaGeometry;
using namespace NorvesLib::Core::Engine;
using namespace NorvesLib::Core;
namespace Math = NorvesLib::Math;

namespace Game::GameModes
{
    namespace
    {
        // 大きな球の高ポリのMegaGeometryの影・光線に使う段（320×160 の格子、約10万三角形、頂点の間隔約2 cm）。
        // 変位した石の盛り上がり（石1つが約10〜15 cm）を形として持ち、影・光線も同じ凹凸の形を使う。
        constexpr uint32_t kBigSphereShadowLODLevel = 2u;
        // 大きな球の石畳の視差の深さ（高さマップを読めず変位しないときだけPOMで使う）。地面と同じ値。
        constexpr float kBigSphereHeightScale = 0.03f;
        // 大きな球の変位の深さ（高さ0の点を球面から内側へ動かす距離）。石畳の法線マップの傾きが高さマップの
        // 勾配×深さと最小二乗で一致する深さ（1枚約2.09 mで横2.95 cm・縦3.07 cm）にし、作り直した法線と法線マップの
        // 傾きの大きさをそろえる。今のPOM（高さの尺度0.03×1枚2.09 m＝真上から見て6.3 cm、オフセットを抑える
        // 近似のため45°で4.4 cm・60°で3.1 cm相当）の見た目の範囲にも入る。
        constexpr float kBigSphereDisplacementDepth = MegaGeometry::StartupBigSphere::kDisplacementDepth;
        // 高さマップのミップを作り始める段（4096画素なら512画素。LOD0の頂点の間隔は約12画素でミップ3.58）
        constexpr uint32_t kBigSphereHeightFieldFirstMip = MegaGeometry::StartupBigSphere::kHeightFieldFirstMip;
        // LOD0の格子（経度×緯度）。頂点の間隔は約6.1 mmで、変位の凹凸（石1つ約10〜15 cm、目地の幅約1〜3 cm）を形に持つ。
        // 近接視点ではLOD0が選ばれ、1280×640（約164万三角形）では三角形が約1.3画素と細かすぎて MegaGeometryPass が
        // 中央値 6〜7 ms になり、1フレームのGPUの時間が予算16.6 msに近づくため1段下げる。
        // クック済みの球（Tools/AssetCook の --generate displaced-sphere）と同じ値を使う（StartupBigSphereSpec.h が共有）。
        constexpr uint32_t kBigSphereSegments = MegaGeometry::StartupBigSphere::kSegments;
        constexpr uint32_t kBigSphereRings = MegaGeometry::StartupBigSphere::kRings;
        constexpr const char *kBigSphereHeightMapRelativePath = MegaGeometry::StartupBigSphere::kHeightMapRelativePath;

        // 地面（60 m 四方、y=-1）。石畳のテクスチャは 2 m ごとに繰り返す。
        constexpr float kGroundSize = 60.0f;
        constexpr float kGroundTileSize = 2.0f;
        // 地面の石畳の視差の深さ（石畳の1枚2 mに対する割合。仮の球より1タイルが小さいため、凹凸の深さが
        // 同じ程度になるよう小さくする。高ポリの球も同じ値を使う）
        constexpr float kGroundHeightScale = 0.03f;

        // 地面の見本の区画: 幅4 mの帯を x=-14〜14 に並べ、中央の帯（大きな球の下）と外側は石畳のまま、
        // ほかの帯へ Poly Haven（CC0）の材質を1つずつ置いて、同じ光の下で近くから遠くまで見比べる。
        // テクスチャは Scripts/FetchPolyHavenTextures.ps1 が Assets/Textures/PolyHaven/<id>/ へ落とす
        // （git の管理外。素材の並びはスクリプトと同じ）。ファイルが無い帯は石畳で描く。
        struct GroundSwatchSpec
        {
            const char *AssetId;     // Poly Haven の資産ID（フォルダ名・ファイル名の接頭辞）
            float CenterX;           // 帯の中心のx（m）
            float TileMeters;        // テクスチャ1枚の実寸（m、Poly Haven の寸法）
            float HeightDepthMeters; // 視差オクルージョンの深さ（m）。0なら高さマップを読まない
        };
        constexpr float kGroundSwatchWidth = 4.0f;
        constexpr float kGroundSwatchHalfSpan = 14.0f; // 帯を並べる範囲の半分（7本 × 4 m）
        constexpr GroundSwatchSpec kGroundSwatches[] = {
            {"snow_02", -12.0f, 2.0f, 0.02f},             // 雪
            {"brown_mud_leaves_01", -8.0f, 1.3f, 0.02f},  // 泥と落ち葉
            {"forrest_ground_01", -4.0f, 2.0f, 0.03f},    // 森の地面（材質見本の球の下）
            {"marble_01", 4.0f, 1.5f, 0.0f},              // 大理石（磨いた平らな床なので視差なし。点光源・岩の下）
            {"asphalt_02", 8.0f, 3.0f, 0.005f},           // アスファルト
            {"sand_01", 12.0f, 1.5f, 0.01f},              // 砂
        };
        constexpr uint32_t kGroundSwatchCount = static_cast<uint32_t>(sizeof(kGroundSwatches) / sizeof(kGroundSwatches[0]));

        // 見本の材質のテクスチャの読み込みパス（"Assets/" から始まる）。suffix は diff・nor_dx・rough・ao・disp。
        String MakeGroundSwatchTexturePath(const GroundSwatchSpec &spec, const char *suffix)
        {
            return String("Assets/Textures/PolyHaven/") + spec.AssetId + "/" + spec.AssetId + "_" + suffix + "_4k.jpg";
        }

        // 材質1つ分のテクスチャの名前。"Assets/" から始まる論理パスで、クック済みもばらの元画像もこの名前で引ける。
        struct MaterialTexturePaths
        {
            String Albedo;
            String Normal;
            String Orm;       // クック済みの ORM（AO・粗さ・メタリックを1枚に詰めたもの）。ばらの元画像は無い
            String Metallic;  // ばらの元画像。空なら使わない（ORM が無いときの別々の枠）
            String Roughness; // 同上
            String AO;        // 同上
            String Height;    // 空なら視差の高さを読まない
        };

        // クック済みのマニフェストにこの論理パスの項目があるか。クック済みを使わないとき（未設定）は常に false。
        bool IsTextureCooked(const Rendering3DTestData &data, const String &path)
        {
            return !path.empty() && data.m_IsTextureCooked.IsBound() && data.m_IsTextureCooked.Invoke(path);
        }

        // 材質の枠の種類。読み込めた結果によって、後始末が変わる。
        enum class MaterialSlotKind
        {
            Plain,  // 読み込めたハンドルをそのまま入れる（VT にしない）
            Albedo, // クック済みなら VT（sparse）で作る。作れない・常駐しなければ全常駐で読み直す
            Normal, // VT を試す。読み込めた形式が BC5（2チャンネル）なら bNormalTwoChannel を立てる
            Orm,    // VT を試す。読み込めなければ、粗さ・AO・メタリックの別々の元画像を読む枠へ戻る
            Height, // VT を試す（視差の高さ）
        };

        // 枠の種類が VT の対象か（アルベド・法線・ORM・高さ。ばらの粗さ・AO・メタリックは対象外）
        bool IsVirtualTextureSlotKind(MaterialSlotKind kind)
        {
            return kind != MaterialSlotKind::Plain;
        }

        // 材質1つ分の枠を1つ非同期で読む。結果は update->CreateData へ入れ、update->PendingTextureCount が 0 に
        // なったとき onComplete を呼ぶ。ORM が読めなかったときは、読み終わる前に別々の元画像の枠を数に足すので、
        // 数が途中で 0 になることはない（コールバックはメインスレッドで呼ばれる）。
        // bTryVirtualTexture のとき（アルベド・法線・ORM・高さの枠）は、先に VT で作り、ミップテイルが常駐したら材質へ入れる。
        // VT を作れない、または常駐しなかったときは、同じ枠を全常駐で読み直す。
        template <typename OnComplete>
        void LoadMaterialSlot(TextureResources &textures,
                              const TSharedPtr<PendingMaterialUpdate> &update,
                              const MaterialTexturePaths &paths,
                              const String &path,
                              TextureHandle MaterialCreateData::*member,
                              MaterialSlotKind kind,
                              bool bTryVirtualTexture,
                              OnComplete onComplete)
        {
            auto onLoaded = [&textures, update, paths, path, member, kind, onComplete](TextureHandle handle)
            {
                update->CreateData.*member = handle;
                if (kind == MaterialSlotKind::Normal)
                {
                    // クック済みの法線は BC5。ばらの元画像へ戻ったときは RGBA8 なので立てない。
                    const NorvesLib::RHI::ITexture *rhiTexture = handle.IsValid() ? textures.GetRHITexture(handle) : nullptr;
                    update->CreateData.bNormalTwoChannel =
                        rhiTexture != nullptr && rhiTexture->GetFormat() == NorvesLib::RHI::Format::BC5_UNORM;
                }
                else if (kind == MaterialSlotKind::Orm && !handle.IsValid())
                {
                    NORVES_LOG_WARNING("Rendering3DTest",
                                       "MATERIAL_ORM_FALLBACK path=%s クック済みの ORM を読めないため、"
                                       "粗さ・AO・メタリックのばらの元画像を読みます",
                                       path.c_str());
                    struct LooseSlot
                    {
                        const String *Path;
                        TextureHandle MaterialCreateData::*Member;
                    };
                    const LooseSlot looseSlots[] = {
                        {&paths.Metallic, &MaterialCreateData::MetallicTexture},
                        {&paths.Roughness, &MaterialCreateData::RoughnessTexture},
                        {&paths.AO, &MaterialCreateData::AOTexture},
                    };
                    for (const LooseSlot &looseSlot : looseSlots)
                    {
                        if (!looseSlot.Path->empty())
                        {
                            ++update->PendingTextureCount;
                            LoadMaterialSlot(textures, update, paths, *looseSlot.Path, looseSlot.Member,
                                             MaterialSlotKind::Plain, false, onComplete);
                        }
                    }
                }

                if (--update->PendingTextureCount == 0)
                {
                    onComplete();
                }
            };

            if (bTryVirtualTexture)
            {
                const bool bStarted = textures.CreateVirtualTextureAsync(
                    path,
                    [&textures, update, paths, path, member, kind, onComplete, onLoaded](TextureHandle handle)
                    {
                        if (handle.IsValid())
                        {
                            onLoaded(handle);
                            return;
                        }
                        NORVES_LOG_WARNING("Rendering3DTest",
                                           "VT_FALLBACK path=%s VTのミップテイルが常駐しないため、全常駐で読み直します",
                                           path.c_str());
                        LoadMaterialSlot(textures, update, paths, path, member, kind, false, onComplete);
                    });
                if (bStarted)
                {
                    return;
                }
                NORVES_LOG_WARNING("Rendering3DTest", "VT_FALLBACK path=%s VTを作れないため、全常駐で読みます", path.c_str());
            }
            textures.LoadTextureAsync(path, onLoaded);
        }

        // 材質1つ分のテクスチャを非同期で読み、そろったら onComplete を呼ぶ。マニフェストにクック済みの ORM が
        // あればまずそれを読み（AO・粗さ・メタリックの3枠の代わり。読めなければ別々のばらの元画像へ戻る）、
        // 無ければ最初から別々のばらの元画像を読む。法線は、読めた形式が BC5 のときだけ2チャンネルの印を付ける。
        // アルベド・法線・高さは、クック済みもばらも同じ名前で読む
        // （クック済みを使えないものはエンジンがばらの元画像を無圧縮で読み、TEXTURE_COOKED_MISSING を警告する）。
        template <typename OnComplete>
        void RequestMaterialTextures(const Rendering3DTestData &data,
                                     TextureResources &textures,
                                     const TSharedPtr<PendingMaterialUpdate> &update,
                                     const MaterialTexturePaths &paths,
                                     OnComplete onComplete)
        {
            struct Slot
            {
                const String *Path = nullptr;
                TextureHandle MaterialCreateData::*Member = nullptr;
                MaterialSlotKind Kind = MaterialSlotKind::Plain;
            };
            Slot slots[8];
            uint32_t slotCount = 0;
            auto addSlot = [&slots, &slotCount](const String &path,
                                                TextureHandle MaterialCreateData::*member,
                                                MaterialSlotKind kind = MaterialSlotKind::Plain)
            {
                if (!path.empty())
                {
                    slots[slotCount].Path = &path;
                    slots[slotCount].Member = member;
                    slots[slotCount].Kind = kind;
                    ++slotCount;
                }
            };

            addSlot(paths.Albedo, &MaterialCreateData::AlbedoTexture, MaterialSlotKind::Albedo);
            addSlot(paths.Normal, &MaterialCreateData::NormalTexture, MaterialSlotKind::Normal);
            update->CreateData.bNormalTwoChannel = false;
            if (IsTextureCooked(data, paths.Orm))
            {
                addSlot(paths.Orm, &MaterialCreateData::ORMTexture, MaterialSlotKind::Orm);
            }
            else
            {
                addSlot(paths.Metallic, &MaterialCreateData::MetallicTexture);
                addSlot(paths.Roughness, &MaterialCreateData::RoughnessTexture);
                addSlot(paths.AO, &MaterialCreateData::AOTexture);
            }
            addSlot(paths.Height, &MaterialCreateData::HeightTexture, MaterialSlotKind::Height);

            update->PendingTextureCount = slotCount;
            for (uint32_t slotIndex = 0; slotIndex < slotCount; ++slotIndex)
            {
                const bool bTryVirtualTexture = IsVirtualTextureSlotKind(slots[slotIndex].Kind) &&
                                                data.m_bVirtualTexture && textures.SupportsVirtualTexture() &&
                                                IsTextureCooked(data, *slots[slotIndex].Path);
                LoadMaterialSlot(textures, update, paths, *slots[slotIndex].Path, slots[slotIndex].Member,
                                 slots[slotIndex].Kind, bTryVirtualTexture, onComplete);
            }
        }

        // 見本の材質が使うテクスチャがすべてディスクにあるか（無ければ、その帯は石畳で描く）。
        // クック済みの色が引けるなら、ばらの元画像が無くても使える。
        bool AreGroundSwatchTexturesPresent(const Rendering3DTestData &data, const GroundSwatchSpec &spec)
        {
            if (IsTextureCooked(data, MakeGroundSwatchTexturePath(spec, "diff")))
            {
                return true;
            }

            const AnsiString assetRoot = Asset::AssetFileReader::GetCompiledDefaultAssetRoot();
            const char *suffixes[] = {"diff", "nor_dx", "rough", "ao", "disp"};
            const uint32_t suffixCount = spec.HeightDepthMeters > 0.0f ? 5u : 4u;
            for (uint32_t suffixIndex = 0; suffixIndex < suffixCount; ++suffixIndex)
            {
                const String relativePath = String("Textures/PolyHaven/") + spec.AssetId + "/" + spec.AssetId + "_" +
                                            suffixes[suffixIndex] + "_4k.jpg";
                const String fullPath = assetRoot.empty() ? String("Assets/") + relativePath
                                                          : String(assetRoot.c_str()) + "/" + relativePath;
                std::error_code errorCode;
                if (!std::filesystem::exists(std::filesystem::path(fullPath.c_str()), errorCode) || errorCode)
                {
                    return false;
                }
            }
            return true;
        }

        // テクスチャの負荷モード（--stress-textures）: 地面（60 m 四方）の外側の奥（+z）へ、負荷用の材質を貼った
        // 12 m 四方の板を 6 列 × 4 行の格子（間隔 14 m）に並べる。4K の色・法線・ORM が 24 材質分（全常駐なら約 1.6 GB）で、
        // VT のプールの目標より多くなる上限（--vram-budget-mb）で、VT が目標の中でタイルを入れ替えて描けることを確かめる。
        // テクスチャは Scripts/FetchPolyHavenTextures.ps1 -StressSet が落とし、CookAssets が焼く（git の管理外）。
        // 並びはスクリプトと Assets/AssetSets/Rendering3DTestStressTextures.json と同じ。
        // 起動画面そのものは変えない（このモードのときだけ、カメラの軸を格子の中心へ移す）。
        struct StressMaterialSpec
        {
            const char *AssetId; // Poly Haven の資産ID（フォルダ名・ファイル名の接頭辞）
            float TileMeters;    // テクスチャ1枚の実寸（m）
        };
        constexpr StressMaterialSpec kStressMaterials[] = {
            {"aerial_rocks_02", 2.0f},     {"coast_sand_rocks_02", 2.0f},     {"dry_ground_rocks", 2.0f},
            {"gravel_ground_01", 2.0f},    {"forest_ground_04", 2.0f},        {"rock_04", 2.0f},
            {"castle_brick_01", 2.0f},     {"concrete_wall_003", 2.0f},       {"quarry_wall", 3.0f},
            {"mossy_stone_wall", 2.0f},    {"brown_planks_03", 1.5f},         {"dark_planks", 1.5f},
            {"herringbone_parquet", 1.5f}, {"bark_brown_01", 2.0f},           {"cobblestone_floor_01", 2.0f},
            {"brick_floor", 2.0f},         {"concrete_floor_worn_001", 3.0f}, {"terracotta_floor_tiles", 2.0f},
            {"corrugated_iron", 2.0f},     {"rusty_metal_02", 2.0f},          {"metal_plate", 2.0f},
            {"clay_roof_tiles", 2.0f},     {"roof_slates_02", 2.0f},          {"grey_roof_tiles", 2.0f},
        };
        constexpr uint32_t kStressMaterialCount =
            static_cast<uint32_t>(sizeof(kStressMaterials) / sizeof(kStressMaterials[0]));
        constexpr uint32_t kStressGridColumns = 6u;
        constexpr float kStressPanelSize = 12.0f;
        constexpr float kStressPanelPitch = 14.0f;
        // 格子の中心（地面の端 z=30 から 10 m 空け、4行ぶんの奥行きの半分だけ奥）
        constexpr float kStressGridCenterX = 0.0f;
        constexpr float kStressGridCenterZ = 68.0f;

        // i 番目の板の中心（x・z、m）
        void GetStressPanelCenter(uint32_t index, float &outX, float &outZ)
        {
            const uint32_t rowCount = (kStressMaterialCount + kStressGridColumns - 1u) / kStressGridColumns;
            outX = kStressGridCenterX +
                   (static_cast<float>(index % kStressGridColumns) - 0.5f * static_cast<float>(kStressGridColumns - 1u)) *
                       kStressPanelPitch;
            outZ = kStressGridCenterZ +
                   (static_cast<float>(index / kStressGridColumns) - 0.5f * static_cast<float>(rowCount - 1u)) *
                       kStressPanelPitch;
        }

        // 負荷用の材質のテクスチャの読み込みパスの組み立てと存在の確認に、見本の区画と同じ関数を使うための表の項目
        GroundSwatchSpec MakeStressTextureSpec(const StressMaterialSpec &spec)
        {
            return GroundSwatchSpec{spec.AssetId, 0.0f, spec.TileMeters, 0.0f};
        }

        double ElapsedMilliseconds(std::chrono::steady_clock::time_point startTime)
        {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startTime).count();
        }

        // 大きな球の高ポリのMegaGeometry（緯度経度の格子で LOD0 は 1024×512、約105万三角形。8×8セルのクラスタと、
        // 縦横半分ずつ粗くした5段のLOD）の頂点とクラスタを作り、石畳の高さマップ（16ビットのグレーのPNG）で
        // 石の盛り上がりと目地の窪みを形にする。UVは横3回・縦1.5回の繰り返しで、1枚が約2.1 m四方。
        // 別スレッドで走る。高さマップを読めなければ変位しない球を作り、球を作れなければ outData を空にする。
        void BuildBigSphereMegaData(ProceduralMegaSphereData &outData)
        {
            const auto buildStartTime = std::chrono::steady_clock::now();

            const AnsiString assetRoot = Asset::AssetFileReader::GetCompiledDefaultAssetRoot();
            const String heightMapPath = assetRoot.empty()
                                             ? String("Assets/") + kBigSphereHeightMapRelativePath
                                             : String(assetRoot.c_str()) + "/" + kBigSphereHeightMapRelativePath;
            int width = 0;
            int height = 0;
            int channels = 0;
            stbi_us *pixels = stbi_load_16(heightMapPath.c_str(), &width, &height, &channels, 1);
            const double decodeMs = ElapsedMilliseconds(buildStartTime);

            const auto heightFieldStartTime = std::chrono::steady_clock::now();
            ProceduralMegaSphereHeightField heightField;
            const bool bHeightFieldOk =
                pixels && width > 0 && width == height &&
                BuildProceduralMegaSphereHeightField(pixels, static_cast<uint32_t>(width), kBigSphereHeightFieldFirstMip,
                                                     heightField);
            stbi_image_free(pixels);
            const double heightFieldMs = ElapsedMilliseconds(heightFieldStartTime);
            if (!bHeightFieldOk)
            {
                NORVES_LOG_WARNING("Rendering3DTest",
                                   "大きな球の高さマップを読めませんでした（変位なしの球にします）: %s (%dx%d)",
                                   heightMapPath.c_str(),
                                   width,
                                   height);
            }

            const auto meshStartTime = std::chrono::steady_clock::now();
            ProceduralMegaSphereSettings sphereSettings{};
            sphereSettings.Radius = MegaGeometry::StartupBigSphere::kRadius;
            sphereSettings.Segments = kBigSphereSegments;
            sphereSettings.Rings = kBigSphereRings;
            sphereSettings.TexCoordRepeatU = MegaGeometry::StartupBigSphere::kTexCoordRepeatU;
            sphereSettings.TexCoordRepeatV = MegaGeometry::StartupBigSphere::kTexCoordRepeatV;
            if (bHeightFieldOk)
            {
                sphereSettings.HeightField = &heightField;
                sphereSettings.DisplacementDepth = kBigSphereDisplacementDepth;
            }
            if (!BuildProceduralMegaSphere(sphereSettings, outData))
            {
                outData = ProceduralMegaSphereData{};
                NORVES_LOG_ERROR("Rendering3DTest", "大きな球の高ポリのMegaGeometryを作れませんでした（通常のメッシュの球のまま）");
                return;
            }
            const double meshMs = ElapsedMilliseconds(meshStartTime);

            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=big_sphere_cluster_lod_build build_ms=%.1f heightmap_decode_ms=%.1f heightfield_ms=%.1f "
                            "mesh_ms=%.1f vertices=%u triangles=%u clusters=%u levels=%u",
                            ElapsedMilliseconds(buildStartTime),
                            decodeMs,
                            heightFieldMs,
                            meshMs,
                            static_cast<uint32_t>(outData.Vertices.size()),
                            static_cast<uint32_t>(outData.Indices.size() / 3u),
                            static_cast<uint32_t>(outData.Clusters.size()),
                            static_cast<uint32_t>(outData.LevelTriangleCounts.size()));
            NORVES_LOG_INFO("Rendering3DTest",
                            "big_sphere_displacement displaced=%d depth_m=%.4f max_depth_m=%.4f pole_fade_sin=%.2f..%.2f "
                            "uv_spacing=%.6f normal_fallbacks=%u seam_pole_mismatches=%u max_normal_tilt_deg_full=%.1f "
                            "max_normal_tilt_deg_pole_fade=%.1f heightmap=%dx%d channels=%d",
                            bHeightFieldOk ? 1 : 0,
                            bHeightFieldOk ? static_cast<double>(kBigSphereDisplacementDepth) : 0.0,
                            static_cast<double>(outData.MaxDisplacementDepth),
                            static_cast<double>(sphereSettings.PoleFadeStartSin),
                            static_cast<double>(sphereSettings.PoleFadeEndSin),
                            static_cast<double>(outData.DisplacementUVSpacing),
                            outData.NormalFallbackCount,
                            CountProceduralMegaSphereSeamMismatches(sphereSettings, outData),
                            static_cast<double>(outData.MaxNormalTiltFullDegrees),
                            static_cast<double>(outData.MaxNormalTiltPoleFadeDegrees),
                            width,
                            height,
                            channels);
            for (uint32_t level = 0; level < outData.LevelTriangleCounts.size(); ++level)
            {
                NORVES_LOG_INFO("Rendering3DTest",
                                "big_sphere_lod level=%u triangles=%u clusters=%u avg_triangles_per_cluster=%.1f "
                                "lod_error_m=%.3g displacement_error_m=%.3g",
                                level,
                                outData.LevelTriangleCounts[level],
                                outData.LevelClusterCounts[level],
                                static_cast<double>(outData.LevelTriangleCounts[level]) /
                                    static_cast<double>(outData.LevelClusterCounts[level]),
                                static_cast<double>(outData.LevelErrors[level]),
                                static_cast<double>(outData.LevelDisplacementErrors[level]));
            }
        }

        // 起動画面の岩・小屋のメッシュの論理パス。岩は既定のモデルのときだけクック済みを使う
        // （--rendering3dtest-model で別のパスを指したときは、そのパスの glTF を従来の経路で読む）。
        constexpr const char *kStartupBoulderMeshPath = "Assets/Models/boulder_01_4k.gltf/boulder_01_4k.gltf";
        constexpr const char *kStartupCottageMeshPath = "Assets/Models/Cottage_Clean/Cottage_Clean.gltf";

        // 岩・小屋の材質のテクスチャの論理パス（クック済みの BC・ORM。Scripts/CookAssets.ps1 が一覧
        // Assets/AssetSets/Rendering3DTestStartupModels.json から焼く）。glTF の経路が読むばらの元画像の名前とは
        // 別にしてある（同じ名前だと glTF の経路が BC5 の法線・BC7 の ORM を RGBA8 前提で読んで壊れる）。
        MaterialTexturePaths MakeStartupBoulderTexturePaths()
        {
            MaterialTexturePaths paths;
            paths.Albedo = "Assets/Models/boulder_01_4k.gltf/cooked/boulder_01_albedo";
            paths.Normal = "Assets/Models/boulder_01_4k.gltf/cooked/boulder_01_normal";
            paths.Orm = "Assets/Models/boulder_01_4k.gltf/cooked/boulder_01_orm";
            return paths;
        }

        MaterialTexturePaths MakeStartupCottageTexturePaths()
        {
            MaterialTexturePaths paths;
            paths.Albedo = "Assets/Models/Cottage_Clean/cooked/Cottage_Clean_albedo";
            paths.Normal = "Assets/Models/Cottage_Clean/cooked/Cottage_Clean_normal";
            paths.Orm = "Assets/Models/Cottage_Clean/cooked/Cottage_Clean_orm";
            return paths;
        }

        // 起動画面の地面の外周（石畳の帯）に並べる高ポリのスキャン資産（Poly Haven、CC0）。
        // Scripts/FetchPolyHavenModels.ps1 が Assets/Models/PolyHaven/<id>/ へ落とし、
        // Assets/AssetSets/Rendering3DTestStartupScanProps.json が NVMESH v1・BC・VT へ焼く（並びは両方と同じにする）。
        // 既定の視点は原点から約 10 m 引いて +Z 側から見る。見本の帯（|x| <= 14）・球・岩・小屋（x ±6.2、z -25.4〜-10.6）を
        // 隠さないよう、帯の外の石畳（|x| > 14）の奥寄り（z -10 より奥）へ置く。
        // クック済みの資産が無いときは glTF の経路へ戻さず、置かずに警告する。
        struct StartupScanPropSpec
        {
            const char *AssetId;
            float X;
            float Z;
            float YawDegrees;
            float Scale;
        };
        constexpr StartupScanPropSpec kStartupScanProps[] = {
            {"coast_land_rocks_03", -20.0f, -16.0f, 25.0f, 1.0f},
            {"coast_rocks_05", 17.5f, -13.0f, -30.0f, 1.0f},
            {"sand_rocks_small_01", 22.0f, -21.0f, 70.0f, 1.0f},
        };
        constexpr uint32_t kStartupScanPropCount = static_cast<uint32_t>(sizeof(kStartupScanProps) / sizeof(kStartupScanProps[0]));
        // 地面（Y=-1）へわずかに埋める深さ（m）
        constexpr float kStartupScanPropSink = 0.03f;

        String MakeStartupScanPropMeshPath(const StartupScanPropSpec &spec)
        {
            return String("Assets/Models/PolyHaven/") + spec.AssetId + "/" + spec.AssetId + "_4k.gltf";
        }

        MaterialTexturePaths MakeStartupScanPropTexturePaths(const StartupScanPropSpec &spec)
        {
            const String cookedPrefix = String("Assets/Models/PolyHaven/") + spec.AssetId + "/cooked/" + spec.AssetId;
            MaterialTexturePaths paths;
            paths.Albedo = cookedPrefix + "_albedo";
            paths.Normal = cookedPrefix + "_normal";
            paths.Orm = cookedPrefix + "_orm";
            return paths;
        }

        // 起動画面の岩・小屋のクック済みメッシュ（NVMESH）を解決・解析し、材質のテクスチャ（VT）の読み込みを始める。
        // メッシュのクック済みが無い・解析できないときは何も始めずに false を返す（呼び出し側が glTF の経路へ戻す）。
        // 成功したら、材質がそろうのを FinishCookedStartupModels が待ち、MegaMesh を作って state を埋める。
        bool StartCookedStartupModelLoad(GameModeContext &ctx,
                                         Rendering3DTestData &data,
                                         const char *debugName,
                                         const char *meshPath,
                                         const MaterialTexturePaths &paths,
                                         bool bBoulder,
                                         const TSharedPtr<BoulderAsyncState> &state,
                                         bool bAllowGltfFallback = true)
        {
            if (!data.m_GetAssetSystem.IsBound())
            {
                return false;
            }
            const TSharedPtr<const Asset::AssetSystem> assetSystem = data.m_GetAssetSystem.Invoke();
            if (!assetSystem)
            {
                return false;
            }

            const Asset::AssetResolveResult resolved = assetSystem->ResolveAsset(meshPath, Asset::AssetKind::Model);
            if (!resolved.UsedCooked())
            {
                return false;
            }
            // 材質のテクスチャ（アルベド・法線・ORM）もクック済みでなければ、ばらの元画像の名前が無いので glTF の経路へ戻す。
            if (!IsTextureCooked(data, paths.Albedo) || !IsTextureCooked(data, paths.Normal) ||
                !IsTextureCooked(data, paths.Orm))
            {
                NORVES_LOG_WARNING("Rendering3DTest", "COOKED_MODEL_TEXTURES_MISSING path=%s 材質のクック済みのテクスチャがそろっていません",
                                   meshPath);
                return false;
            }
            Asset::CookedMeshParseResult parsed = Asset::ParseCookedMesh(resolved.Blob);
            if (!parsed.Succeeded() || parsed.Mesh.Clusters.empty())
            {
                NORVES_LOG_WARNING("Rendering3DTest", "COOKED_MODEL_INVALID path=%s status=%u クック済みのメッシュを解析できません",
                                   meshPath, static_cast<unsigned int>(parsed.Status));
                return false;
            }

            CookedStartupModelLoad load;
            load.DebugName = debugName;
            load.LogicalPath = meshPath;
            load.bBoulder = bBoulder;
            load.bAllowGltfFallback = bAllowGltfFallback;
            load.Mesh = MakeShared<Asset::CookedMeshData>(std::move(parsed.Mesh));
            load.State = state;
            if (state)
            {
                // 最下点の Y（スキャン資産を地面へ据えるのに使う）。
                float minY = std::numeric_limits<float>::max();
                for (const Asset::CookedMeshVertex &vertex : load.Mesh->Vertices)
                {
                    minY = std::min(minY, vertex.Position.Y);
                }
                state->m_BoundsMinY = load.Mesh->Vertices.empty() ? 0.0f : minY;
            }
            load.Material = MakeShared<PendingMaterialUpdate>();
            load.Material->CreateData.DebugName = debugName;

            NORVES_LOG_INFO("Rendering3DTest",
                            "COOKED_MODEL path=%s format_major=%u vertices=%u indices=%u clusters=%u lod_levels=%u fallback_indices=%u",
                            meshPath,
                            static_cast<unsigned int>(load.Mesh->FormatMajor),
                            static_cast<unsigned int>(load.Mesh->Vertices.size()),
                            static_cast<unsigned int>(load.Mesh->Indices.size()),
                            static_cast<unsigned int>(load.Mesh->Clusters.size()),
                            static_cast<unsigned int>(load.Mesh->LODLevelCount),
                            static_cast<unsigned int>(load.Mesh->FallbackIndexCount));

            // 材質は VT（クック済みのアルベド・法線・ORM）で読む。そろったかは Material->PendingTextureCount で見る。
            RequestMaterialTextures(data, ctx.RenderResourcesRef.Textures(), load.Material, paths, []() {});
            data.m_PendingMaterialUpdates.push_back(load.Material);
            data.m_CookedStartupModelLoads.push_back(std::move(load));
            return true;
        }

        // 起動画面の岩・小屋を従来の glTF の実行時の経路で読み始める。結果は state へ入れる
        // （岩・小屋の組み立てが、クック済みの経路と同じ手順で受け取る）。要求番号は outRequestId に入る。
        void StartGltfStartupModelLoad(GameModeContext &ctx,
                                       const String &logicalPath,
                                       const TSharedPtr<BoulderAsyncState> &state,
                                       uint32_t &outRequestId)
        {
            ModelLoadResourceContext loadContext{ctx.RenderResourcesRef.Textures(), ctx.RenderResourcesRef.MegaGeometry()};
            outRequestId = Resource::GLTFAnalyzer::LoadModelAsync(
                logicalPath,
                loadContext,
                [state](ModelHandle handle)
                {
                    state->m_Handle = handle;
                    state->m_bLoaded = handle.IsValid();
                    state->m_bCompleted.Store(true);
                });
            if (outRequestId == 0)
            {
                state->m_bLoaded = false;
                state->m_bCompleted.Store(true);
                NORVES_LOG_ERROR("Rendering3DTest", "glTF の経路へ戻したモデルの非同期ロード開始に失敗しました: %s",
                                 logicalPath.c_str());
            }
        }

        // クック済みで読んでいる岩・小屋のうち、材質のテクスチャがそろったものの MegaMesh を作り、モデルとして登録して
        // state を埋める（岩・小屋の組み立てが、従来の非同期の読み込みと同じ手順で受け取る）。作れなければ失敗として埋める。
        void FinishCookedStartupModels(GameModeContext &ctx, Rendering3DTestData &data)
        {
            auto &loads = data.m_CookedStartupModelLoads;
            for (size_t index = 0; index < loads.size();)
            {
                CookedStartupModelLoad &load = loads[index];
                if (load.State && load.State->m_bCancelled.Load())
                {
                    loads.erase(loads.begin() + static_cast<std::ptrdiff_t>(index));
                    continue;
                }
                if (!load.Material || load.Material->PendingTextureCount != 0)
                {
                    ++index;
                    continue;
                }

                // 必須のテクスチャ（アルベド・法線・ORM）が読めなかった（パッケージの欠け・壊れ）ときは、
                // 無効なハンドルのままメッシュを作らず、警告して従来の glTF の経路へ戻す。
                const MaterialCreateData &loadedMaterial = load.Material->CreateData;
                if (!loadedMaterial.AlbedoTexture.IsValid() || !loadedMaterial.NormalTexture.IsValid() ||
                    !loadedMaterial.ORMTexture.IsValid())
                {
                    NORVES_LOG_WARNING("Rendering3DTest",
                                       "COOKED_MODEL_TEXTURES_FAILED path=%s 材質のクック済みのテクスチャを読めないため、"
                                       "%s（albedo=%d normal=%d orm=%d）",
                                       load.LogicalPath.c_str(),
                                       load.bAllowGltfFallback ? "glTF の実行時の経路で読みます" : "この資産は置きません",
                                       loadedMaterial.AlbedoTexture.IsValid() ? 1 : 0,
                                       loadedMaterial.NormalTexture.IsValid() ? 1 : 0,
                                       loadedMaterial.ORMTexture.IsValid() ? 1 : 0);
                    auto &textures = ctx.RenderResourcesRef.Textures();
                    for (const TextureHandle handle : {loadedMaterial.AlbedoTexture, loadedMaterial.NormalTexture,
                                                       loadedMaterial.ORMTexture})
                    {
                        if (handle.IsValid())
                        {
                            textures.ReleaseTexture(handle);
                        }
                    }
                    const String logicalPath = load.LogicalPath;
                    const TSharedPtr<BoulderAsyncState> state = load.State;
                    const bool bAllowGltfFallback = load.bAllowGltfFallback;
                    uint32_t &requestId = load.bBoulder ? data.m_BoulderLoadRequestId : data.m_CottageLoadRequestId;
                    loads.erase(loads.begin() + static_cast<std::ptrdiff_t>(index));
                    if (state && !bAllowGltfFallback)
                    {
                        // glTF の経路を持たない資産（スキャン資産）は、失敗として state を埋める（置かない）。
                        state->m_bLoaded = false;
                        state->m_bCompleted.Store(true);
                    }
                    else if (state)
                    {
                        StartGltfStartupModelLoad(ctx, logicalPath, state, requestId);
                    }
                    continue;
                }

                const auto createStartTime = std::chrono::steady_clock::now();
                auto &megaGeometry = ctx.RenderResourcesRef.MegaGeometry();
                ModelHandle modelHandle = ModelHandle::Invalid();
                MegaMeshCreateInfo createInfo;
                if (BuildMegaMeshCreateInfoFromCookedMesh(*load.Mesh, createInfo))
                {
                    const MaterialCreateData &material = load.Material->CreateData;
                    createInfo.Material.AlbedoTexture = material.AlbedoTexture;
                    createInfo.Material.NormalTexture = material.NormalTexture;
                    createInfo.Material.MetallicTexture = material.MetallicTexture;
                    createInfo.Material.RoughnessTexture = material.RoughnessTexture;
                    createInfo.Material.AOTexture = material.AOTexture;
                    createInfo.Material.ORMTexture = material.ORMTexture;
                    createInfo.Material.bNormalTwoChannel = material.bNormalTwoChannel;
                    createInfo.DebugName = load.DebugName;

                    const MegaMeshHandle megaMeshHandle = megaGeometry.CreateMegaMesh(createInfo);
                    if (megaMeshHandle.IsValid())
                    {
                        modelHandle = megaGeometry.RegisterModel(megaMeshHandle, load.DebugName, load.LogicalPath);
                        if (!modelHandle.IsValid())
                        {
                            megaGeometry.ReleaseMegaMesh(megaMeshHandle);
                        }
                    }
                }

                NORVES_LOG_INFO("AssetLoadProfile",
                                "stage=cooked_startup_model_create name=%s create_ms=%.1f vertices=%u clusters=%u success=%d",
                                load.DebugName.c_str(),
                                ElapsedMilliseconds(createStartTime),
                                static_cast<unsigned int>(load.Mesh->Vertices.size()),
                                static_cast<unsigned int>(load.Mesh->Clusters.size()),
                                modelHandle.IsValid() ? 1 : 0);
                if (!modelHandle.IsValid())
                {
                    NORVES_LOG_ERROR("Rendering3DTest", "クック済みのモデルのMegaMeshを作れませんでした: %s", load.DebugName.c_str());
                }

                if (load.State)
                {
                    load.State->m_Handle = modelHandle;
                    load.State->m_bLoaded = modelHandle.IsValid();
                    load.State->m_bCompleted.Store(true);
                }
                else if (modelHandle.IsValid())
                {
                    megaGeometry.ReleaseModel(modelHandle);
                }
                loads.erase(loads.begin() + static_cast<std::ptrdiff_t>(index));
            }
        }

        // 地面の外周に並べるスキャン資産のクック済みの読み込みを始める。クック済みが無い資産は、警告して置かない。
        void StartStartupScanPropLoads(GameModeContext &ctx, Rendering3DTestData &data)
        {
            data.m_ScanPropLoads.clear();
            for (uint32_t propIndex = 0; propIndex < kStartupScanPropCount; ++propIndex)
            {
                const StartupScanPropSpec &spec = kStartupScanProps[propIndex];
                const String meshPath = MakeStartupScanPropMeshPath(spec);
                auto state = MakeShared<BoulderAsyncState>();
                if (!StartCookedStartupModelLoad(ctx, data, spec.AssetId, meshPath.c_str(), MakeStartupScanPropTexturePaths(spec),
                                                 false, state, false))
                {
                    NORVES_LOG_WARNING("Rendering3DTest",
                                       "SCAN_PROP_MISSING id=%s クック済みのスキャン資産が無いため置きません"
                                       "（Scripts/FetchPolyHavenModels.ps1 で落とし、CookAssets の対象を実行すると焼けます）",
                                       spec.AssetId);
                    continue;
                }
                StartupScanPropLoad load;
                load.SpecIndex = propIndex;
                load.State = state;
                data.m_ScanPropLoads.push_back(std::move(load));
            }
        }

        // 読み込みが終わったスキャン資産を、最下点を地面（Y=-1）へ据えて World へ置く。失敗したものは警告して置かない。
        void PlaceFinishedScanProps(GameModeContext &ctx, Rendering3DTestData &data)
        {
            auto &loads = data.m_ScanPropLoads;
            for (size_t index = 0; index < loads.size();)
            {
                StartupScanPropLoad &load = loads[index];
                if (!load.State || !load.State->m_bCompleted.Load() || load.State->m_bCancelled.Load())
                {
                    ++index;
                    continue;
                }

                const StartupScanPropSpec &spec = kStartupScanProps[load.SpecIndex];
                const TSharedPtr<BoulderAsyncState> state = load.State;
                loads.erase(loads.begin() + static_cast<std::ptrdiff_t>(index));

                auto &megaGeometry = ctx.RenderResourcesRef.MegaGeometry();
                MegaMeshHandle megaMeshHandle;
                if (state->m_bLoaded)
                {
                    megaMeshHandle = megaGeometry.GetModelMegaMeshHandle(state->m_Handle);
                    if (!megaMeshHandle.IsValid())
                    {
                        megaGeometry.ReleaseModel(state->m_Handle);
                    }
                }
                if (!megaMeshHandle.IsValid())
                {
                    NORVES_LOG_WARNING("Rendering3DTest", "SCAN_PROP_FAILED id=%s スキャン資産を読めなかったため置きません", spec.AssetId);
                    continue;
                }

                auto &world = ctx.WorldRef;
                Entity *object = world.SpawnObject<Entity>();
                ctx.ScopeRef.TrackObject(object);
                const float positionY = -1.0f - state->m_BoundsMinY * spec.Scale - kStartupScanPropSink;
                object->SetPosition(spec.X, positionY, spec.Z);
                object->SetScale(spec.Scale, spec.Scale, spec.Scale);
                const NorvesLib::Math::Vector3 yAxis(0.0f, 1.0f, 0.0f);
                object->SetRotation(NorvesLib::Math::Quaternion(yAxis, spec.YawDegrees * (3.14159265f / 180.0f)));
                auto *component = world.CreateComponent<Component::MegaGeometryComponent>(object);
                component->SetMegaMeshHandle(megaMeshHandle);
                component->SetCastShadow(true);
                // 消費したモデルはスコープに解放を委ねる。
                ctx.ScopeRef.TrackModel(state->m_Handle);
                NORVES_LOG_INFO("Rendering3DTest", "SCAN_PROP_PLACED id=%s x=%.1f y=%.3f z=%.1f scale=%.2f", spec.AssetId, spec.X,
                                positionY, spec.Z, spec.Scale);
            }
        }

        // 大きな球のクック済みのメッシュ（NVMESH v1。AssetSets の一覧が焼く）を解決・解析して outCooked に入れる。
        // クック済みが無い・解析できないときは outCooked を空のまま false を返す（呼び出し側が実行時の生成へ戻す）。
        // 別スレッドで走る（約 77 MB の読み込みと検証を、メインスレッドと石畳のテクスチャの読み込みから外すため）。
        // assetSystem はマニフェストの変更が走っていない読み取り専用のスナップショットなので、並行に引ける。
        bool TryLoadCookedBigSphere(const TSharedPtr<const Asset::AssetSystem> &assetSystem,
                                    Asset::CookedMeshData &outCooked)
        {
            const auto loadStartTime = std::chrono::steady_clock::now();
            const char *meshPath = MegaGeometry::StartupBigSphere::kCookedMeshLogicalPath;
            if (!assetSystem)
            {
                return false;
            }
            const Asset::AssetResolveResult resolved = assetSystem->ResolveAsset(meshPath, Asset::AssetKind::Model);
            if (!resolved.UsedCooked())
            {
                NORVES_LOG_WARNING("Rendering3DTest",
                                   "COOKED_BIG_SPHERE_MISSING path=%s クック済みの大きな球が無いため、実行時に生成します",
                                   meshPath);
                return false;
            }
            Asset::CookedMeshParseResult parsed = Asset::ParseCookedMesh(resolved.Blob);
            if (!parsed.Succeeded() || parsed.Mesh.Clusters.empty() || parsed.Mesh.FormatMajor < 1)
            {
                NORVES_LOG_WARNING("Rendering3DTest",
                                   "COOKED_BIG_SPHERE_INVALID path=%s status=%u クック済みの大きな球を解析できないため、"
                                   "実行時に生成します",
                                   meshPath,
                                   static_cast<unsigned int>(parsed.Status));
                return false;
            }
            outCooked = std::move(parsed.Mesh);
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=cooked_big_sphere_load load_ms=%.1f format_major=%u vertices=%u indices=%u clusters=%u "
                            "lod_levels=%u fallback_indices=%u",
                            ElapsedMilliseconds(loadStartTime),
                            static_cast<unsigned int>(outCooked.FormatMajor),
                            static_cast<unsigned int>(outCooked.Vertices.size()),
                            static_cast<unsigned int>(outCooked.Indices.size()),
                            static_cast<unsigned int>(outCooked.Clusters.size()),
                            static_cast<unsigned int>(outCooked.LODLevelCount),
                            static_cast<unsigned int>(outCooked.FallbackIndexCount));
            return true;
        }

        // 大きな球のメッシュを用意するジョブの中身。既定はクック済みを読み、読めなければ（警告して）実行時に生成する。
        // どちらか一方だけが outCooked / outRuntime に入る（クック済みを読めたら outCooked が空でなくなる）。
        void PrepareBigSphereMeshData(bool bTryCooked,
                                      const TSharedPtr<const Asset::AssetSystem> &assetSystem,
                                      Asset::CookedMeshData &outCooked,
                                      ProceduralMegaSphereData &outRuntime)
        {
            if (bTryCooked && TryLoadCookedBigSphere(assetSystem, outCooked))
            {
                return;
            }
            BuildBigSphereMegaData(outRuntime);
        }

        // 起動時に作った（クック済みを読んだ、または実行時に生成した）大きな球の頂点・クラスタから、石畳の材質を付けた
        // MegaMeshを作り、仮に置いていた通常のメッシュの球（32×16）と差し替える。作れなければ仮の球を残す。
        void CreateBigSphereMegaGeometry(GameModeContext &ctx, Rendering3DTestData &data)
        {
            TSharedPtr<ProceduralMegaSphereData> sphereData = data.m_pBigSphereMegaData;
            const TSharedPtr<Asset::CookedMeshData> cookedSphere = data.m_pBigSphereCooked;
            data.m_pBigSphereMegaData.reset();
            data.m_pBigSphereCooked.reset();
            const bool bCooked = cookedSphere && !cookedSphere->Clusters.empty();
            if (!data.m_CobbleStoneMaterialUpdate || (!bCooked && (!sphereData || sphereData->Vertices.empty())))
            {
                return;
            }

            const auto createStartTime = std::chrono::steady_clock::now();
            auto &megaGeometry = ctx.RenderResourcesRef.MegaGeometry();
            const MaterialCreateData &cobble = data.m_CobbleStoneMaterialUpdate->CreateData;

            MegaMeshCreateInfo createInfo;
            float displacementUVSpacing = 0.0f;
            if (bCooked)
            {
                // クック済みは階層（クラスタの DAG）をクッカーが焼いてあり、段の選び方は GPU のカリングが誤差で決める。
                // 影・レイトレは常駐のフォールバックの段を使う。球は常に変位しているので、頂点の間隔は仕様から求める。
                if (!BuildMegaMeshCreateInfoFromCookedMesh(*cookedSphere, createInfo))
                {
                    NORVES_LOG_ERROR("Rendering3DTest",
                                     "クック済みの大きな球からMegaMeshの入力を作れませんでした（仮の球のまま）");
                    return;
                }
                displacementUVSpacing = MegaGeometry::StartupBigSphere::DisplacementUVSpacing();
            }
            else
            {
                createInfo.VertexData = sphereData->Vertices.data();
                createInfo.VertexDataSize = sphereData->Vertices.size() * sizeof(Mesh3DVertex);
                createInfo.VertexCount = static_cast<uint32_t>(sphereData->Vertices.size());
                createInfo.VertexStride = static_cast<uint32_t>(sizeof(Mesh3DVertex));
                createInfo.IndexData = sphereData->Indices.data();
                createInfo.IndexCount = static_cast<uint32_t>(sphereData->Indices.size());
                createInfo.Clusters = sphereData->Clusters;
                createInfo.TotalBounds = sphereData->Bounds;
                // どの段も閉じた球なので、メッシュ全体で同じ段を選ばせて段の境目の割れ目を防ぐ。
                createInfo.LODBounds = sphereData->Bounds;
                createInfo.bBuildLODHierarchy = false;
                createInfo.ShadowLODLevel = kBigSphereShadowLODLevel;
                displacementUVSpacing = sphereData->DisplacementUVSpacing;
            }
            createInfo.Material.AlbedoTexture = cobble.AlbedoTexture;
            createInfo.Material.NormalTexture = cobble.NormalTexture;
            createInfo.Material.RoughnessTexture = cobble.RoughnessTexture;
            createInfo.Material.AOTexture = cobble.AOTexture;
            createInfo.Material.ORMTexture = cobble.ORMTexture;
            createInfo.Material.bNormalTwoChannel = cobble.bNormalTwoChannel;
            if (displacementUVSpacing > 0.0f)
            {
                // 凹凸は形（変位）で出すので POM は切る。法線マップは形が持つ粗い傾きを差し引いて細部だけ載せる。
                createInfo.Material.DisplacementUVSpacing = displacementUVSpacing;
            }
            else
            {
                createInfo.Material.HeightTexture = cobble.HeightTexture;
                createInfo.Material.HeightScale = kBigSphereHeightScale;
                createInfo.Material.bHasHeightMap = createInfo.Material.HeightTexture.IsValid();
            }
            createInfo.DebugName = "BigCobbleSphere";

            const MegaMeshHandle megaMeshHandle = megaGeometry.CreateMegaMesh(createInfo);
            const double createMs = ElapsedMilliseconds(createStartTime);
            if (!megaMeshHandle.IsValid())
            {
                NORVES_LOG_ERROR("Rendering3DTest", "大きな球のMegaMeshを作れませんでした（仮の球のまま）");
                return;
            }
            const ModelHandle modelHandle = megaGeometry.RegisterModel(megaMeshHandle, "BigCobbleSphere");
            if (!modelHandle.IsValid())
            {
                megaGeometry.ReleaseMegaMesh(megaMeshHandle);
                NORVES_LOG_ERROR("Rendering3DTest", "大きな球のMegaMeshをモデルとして登録できませんでした（仮の球のまま）");
                return;
            }
            ctx.ScopeRef.TrackModel(modelHandle);
            data.m_BigSphereModelHandle = modelHandle;

            auto &world = ctx.WorldRef;
            Entity *megaSphereObject = world.SpawnObject<Entity>();
            ctx.ScopeRef.TrackObject(megaSphereObject);
            megaSphereObject->SetPosition(0.0f, 0.0f, 0.0f);
            if (data.m_pSphereObject)
            {
                megaSphereObject->SetRotation(data.m_pSphereObject->GetRotation());
                // スコープ追跡から外してから World から除去する（Cleanup が解放済みポインタを触らないように）。
                ctx.ScopeRef.Untrack(data.m_pSphereObject);
                world.RemoveObject(data.m_pSphereObject);
            }
            data.m_pSphereObject = megaSphereObject;
            data.m_pSphereMeshComponent = nullptr;
            data.m_pSphereMegaGeometryComponent =
                world.CreateComponent<Component::MegaGeometryComponent>(megaSphereObject);
            data.m_pSphereMegaGeometryComponent->SetMegaMeshHandle(megaMeshHandle);
            data.m_pSphereMegaGeometryComponent->SetCastShadow(true);

            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=big_sphere_megamesh_create cooked=%d create_ms=%.1f vertices=%u triangles=%u clusters=%u",
                            bCooked ? 1 : 0,
                            createMs,
                            createInfo.VertexCount,
                            createInfo.IndexCount / 3u,
                            static_cast<uint32_t>(createInfo.Clusters.size()));
        }

        // 環境変数 NORVES_STARTUP_SUN_STEP="<仰角(度)>,<秒>" を読む。形式が違うときは false。
        bool TryReadStartupSunStep(float& outElevation, float& outDelaySeconds)
        {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
            const char* value = std::getenv("NORVES_STARTUP_SUN_STEP");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
            if (value == nullptr || value[0] == '\0')
            {
                return false;
            }
            char* end = nullptr;
            const float elevation = std::strtof(value, &end);
            if (end == value || *end != ',')
            {
                return false;
            }
            const char* delayText = end + 1;
            const float delaySeconds = std::strtof(delayText, &end);
            if (end == delayText || *end != '\0' || !std::isfinite(elevation) || !std::isfinite(delaySeconds) ||
                elevation < 0.0f || elevation > 90.0f || delaySeconds < 0.0f)
            {
                return false;
            }
            outElevation = elevation;
            outDelaySeconds = delaySeconds;
            return true;
        }

        // 環境変数 NORVES_STARTUP_LENS_EFFECTS が "0" なら false（起動画面のレンズの効果を切って撮り比べる用）。
        bool ReadStartupLensEffectsEnabled()
        {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
            const char* value = std::getenv("NORVES_STARTUP_LENS_EFFECTS");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
            return value == nullptr || std::strcmp(value, "0") != 0;
        }

        // 環境変数 NORVES_STARTUP_RTGI が "0" なら false（起動画面のRTGIを切り、IBLだけで撮り比べる用）。
        bool ReadStartupRTGIEnabled()
        {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
            const char* value = std::getenv("NORVES_STARTUP_RTGI");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
            return value == nullptr || std::strcmp(value, "0") != 0;
        }

        // 環境変数 NORVES_STARTUP_LOOK_LUT が "0" なら false（起動画面の見た目の LUT を切って撮り比べる用）。
        bool ReadStartupLookLutEnabled()
        {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
            const char* value = std::getenv("NORVES_STARTUP_LOOK_LUT");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
            return value == nullptr || std::strcmp(value, "0") != 0;
        }

        // 環境変数 NORVES_STARTUP_SPHERE_SPIN が "0" なら false（大きな球の自転を止め、同じ向きで撮り比べる用）。
        bool ReadStartupSphereSpinEnabled()
        {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
            const char* value = std::getenv("NORVES_STARTUP_SPHERE_SPIN");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
            return value == nullptr || std::strcmp(value, "0") != 0;
        }

        // 起動画面の組み立て（材質のテクスチャ・岩と小屋のモデル・大きな球の生成）が非同期の読み込みを含めて
        // 終わったか。VT のタイルがそろうのは待たない（IsStartupSceneAssembled が加える）。
        bool IsStartupSceneAssembledBeforeVirtualTexture(const Rendering3DTestData &data)
        {
            for (const TSharedPtr<PendingMaterialUpdate> &update : data.m_PendingMaterialUpdates)
            {
                if (update && update->PendingTextureCount != 0)
                {
                    return false;
                }
            }
            return !data.m_BoulderAsyncState && !data.m_CottageAsyncState && data.m_ScanPropLoads.empty() &&
                   !data.m_pBigSphereMegaData && !data.m_BigSphereBuildTask;
        }

        // 起動画面の組み立てが、VT のタイルがそろうまで含めてすべて終わったか。
        // 決定的な撮影（--capture-deterministic）はこれが真になった時点から時間を数え直す。
        bool IsStartupSceneAssembled(const Rendering3DTestData &data)
        {
            return IsStartupSceneAssembledBeforeVirtualTexture(data) && data.m_bVirtualTextureSettled;
        }

        // 組み立てが終わった後、VT のタイルがそろう（要求が途絶えて、ストリーマが落ち着く）まで待つ。
        // 材質のアルベドが VT でなければ待たない。要求は 4×4 画素のうち巡回する 1 画素が書き、数フレーム遅れて届くので、
        // 落ち着いた状態が kVirtualTextureIdleTicksToSettle ティック続くのを待つ。タイルがそろわないまま
        // kVirtualTextureMaxWaitTicks を超えたら待つのをやめ、ログに残す（撮影の差で見える）。
        constexpr uint32_t kVirtualTextureIdleTicksToSettle = 60;
        constexpr uint32_t kVirtualTextureMaxWaitTicks = 1800;
        void UpdateVirtualTextureSettle(GameModeContext &ctx, Rendering3DTestData &data)
        {
            if (data.m_bVirtualTextureSettled || !IsStartupSceneAssembledBeforeVirtualTexture(data))
            {
                return;
            }
            TextureResources &textures = ctx.RenderResourcesRef.Textures();
            if (!data.m_bVirtualTexture || !textures.SupportsVirtualTexture())
            {
                data.m_bVirtualTextureSettled = true;
                return;
            }

            ++data.m_VirtualTextureWaitTicks;
            data.m_VirtualTextureIdleTicks =
                textures.IsVirtualTextureStreamingIdle() ? data.m_VirtualTextureIdleTicks + 1 : 0;
            const bool bIdle = data.m_VirtualTextureIdleTicks >= kVirtualTextureIdleTicksToSettle;
            const bool bTimedOut = data.m_VirtualTextureWaitTicks >= kVirtualTextureMaxWaitTicks;
            if (bIdle || bTimedOut)
            {
                data.m_bVirtualTextureSettled = true;
                NORVES_LOG_INFO("Rendering3DTest", "VT_SETTLED ticks=%u timed_out=%d",
                                static_cast<unsigned>(data.m_VirtualTextureWaitTicks), bIdle ? 0 : 1);
            }
        }

        // 起動画面の高さフォグ（R3）。地面での密度（1/m、0で無効）と、高さ方向の減衰（1/m）。
        // 減衰を上限の 1/m にして地面すれすれの薄い層にし、遠くの地面へ向かう浅い視線だけが厚く霞む
        // ようにする。密度は太陽 45° の撮り比べ（0.02〜0.1）で、地面すれすれの低角度視点でも近くの各球の
        // 表示のリニア輝度の標準偏差がフォグ無しの 90% 以上に残る値を選んだ（0.04 では低角度の手前の球が
        // 約 85% まで落ちる）。--height-fog-density・--height-fog-falloff で替えられる。
        constexpr float kStartupHeightFogDensity = 0.02f;
        constexpr float kStartupHeightFogFalloff = 1.0f;

        // 起動画面の自動露出の露出補正（EV）。自動露出は画面の log2 輝度の平均を中間調へ合わせるため、
        // 明るい空が画面の多くを占める昼の屋外では地面が暗く写る。写真の逆光補正と同じく露出を上げる。
        // トーンマップを暗部を縮めない NeutralLinear にしたので、昼（仰角45°）の画面の平均が 0〜255 で約120に
        // なる +1.25 EV にとどめる（以前の ACES での +2 EV は平均が約180で白っぽく飛んだ）。手動露出には掛けない。
        constexpr float kStartupAutoExposureCompensationEV = 1.25f;

        // 起動画面の自動露出で、測光した明るさ（露出補正の前の目標の EV100）に応じて足す露出補正。
        // 自動露出だけでは夕も夜も昼と同じ中間調まで持ち上がり、夜が夜に見えない。目の暗順応が昼の明るさまで
        // 戻らないのと同じく、14 EV（朝の仰角10°の測光値が約13.5〜13.8、昼の45°が約15.4〜15.8）より暗いほど
        // 1 EV あたり約0.41 EV ずつ暗く保ち、0 EV で -5.7 EV にする（夕の3°の約11.4〜12.1で約 -0.8〜-1.1 EV、
        // 夜の約2〜4で約 -4.1〜-4.9 EV）。夜は光源のにじみが画面を広く占める低角度の視点が最も明るく写るので、
        // その画面の平均が80を超えない傾きにした。撮影で朝・昼・夕・夜の画面の平均が約115・120・90・60になる。
        constexpr float kStartupDarkSceneEV100 = 0.0f;
        constexpr float kStartupDarkSceneCompensationEV = -5.7f;
        constexpr float kStartupBrightSceneEV100 = 14.0f;

        // --night の静的HDR（grasslands_sunset_4k）の倍率。倍率1の上半球の放射輝度を余弦で積分した水平面の
        // 照度は約 3.9 lx なので、0.08 で約 0.31 lx（満月の夜の地面の目安 0.1〜1 lx）にする。
        constexpr float kNightStaticEnvironmentIntensityScale = 0.08f;

        // 光源球の電球の色（リニアの sRGB、最大の成分を1にした値）。白熱電球の色温度 2850 K（100 W 形の
        // タングステン電球、CIE 標準イルミナント A の 2856 K に近い）の黒体の色度を Kim ほかの3次式の近似で
        // 求め（x = 0.4475、y = 0.4067）、XYZ から sRGB（D65）の行列でリニアの RGB へ直した。
        // 照明の強さは色の輝度で割って光束（lm）に合わせるので、ここは色だけを決める。
        constexpr float kLightBulbColor[3] = {1.0f, 0.4457f, 0.1276f};

        float StartupExposureCompensationEV(bool bAutoExposure)
        {
            return bAutoExposure ? kStartupAutoExposureCompensationEV : 0.0f;
        }

        // 起動画面のレンズの効果。色収差は画面の左右の端で R・B が 1.5 画素ずれる弱さにとどめ、輪郭の色の
        // にじみがわずかに見える程度にする。レンズダートは、ブルームのうちプリエクスポージャ後で1を超えた
        // 明るいにじみ（太陽・発光球の周り）にだけ模様を浮かせる。強さ2は夕（仰角3°）に太陽のにじみの中で
        // しみの丸が見え、昼（45°）の空には模様が出ない値（しきい値0.3では昼の空にもしみが浮いた）。夜の点光源の
        // 周りの暗い背景では、BloomSettings::LensDirtSceneRatio が加算をブルーム前の画素の値の0.25倍へ頭打ちにし、
        // しみが色付きの円（ゴースト）として浮かないようにする。
        constexpr float kStartupChromaticAberrationPixels = 1.5f;
        constexpr float kStartupLensDirtIntensity = 2.0f;

        // 起動画面のトーンマップとグレーディングとビネット（検証シーンのカメラは View の既定のまま）。
        // トーンマップは中間調まで線形の NeutralLinear（明部だけを Khronos PBR Neutral の式で圧縮する）。ACES Filmic と
        // Khronos PBR Neutral の参照実装はどちらも足元で暗部を縮め、空の光だけの影の中（ライティングの出力で日向の
        // 約2割）が、画面の平均を約120にする露出では表示のリニア輝度で日向の1割強まで落ちる。
        // 線形の暗部の上に、黒と白を動かさない S 字のコントラスト 1.15（軸 0.5）で中間を立てる（影の中は日向の
        // 約18〜21%）。彩度は 1.6: 線形の曲線は ACES のように中間の彩度を上げず、見た目の LUT が明部の彩度を
        // 落とすため、これより低いと昼の空の上端が灰色に寄る（B − R が符号化値で40を切る）。色温度は0（暖かみは
        // LUT が持つ）。ビネットは強さ 0.25・半径 0.85・幅 0.6 で、画面の隅を約 0.78 倍にする（View の既定 0.3・0.8・0.5 の
        // 約 0.72 倍より弱め、明るい地面の隅が濁らないようにする）。
        // 起動画面の見た目の 3D LUT（Scripts/BakeLookLut.py が焼く暖かみのある映画調）。上のグレーディングの後に
        // 掛かり、中間と明部を琥珀へ、暗部をわずかに青緑へ寄せ、黒をわずかに持ち上げる。
        constexpr const char* kStartupLookLutAssetPath = "Textures/LookLuts/WarmFilm.lut3d";

        void ApplyStartupGrading(CameraProxy& camera, bool bLensEffects, bool bLookLut)
        {
            camera.ToneMapCurve = CameraToneMapCurve::NeutralLinear;
            camera.AutoExposureCurve.bEnabled = true;
            camera.AutoExposureCurve.DarkEV100 = kStartupDarkSceneEV100;
            camera.AutoExposureCurve.DarkCompensation = kStartupDarkSceneCompensationEV;
            camera.AutoExposureCurve.BrightEV100 = kStartupBrightSceneEV100;
            camera.AutoExposureCurve.BrightCompensation = 0.0f;
            camera.LookLut.AssetPath = bLookLut ? kStartupLookLutAssetPath : nullptr;
            camera.LookLut.Intensity = 1.0f;
            camera.LensEffects.ChromaticAberrationPixels = bLensEffects ? kStartupChromaticAberrationPixels : 0.0f;
            camera.LensEffects.LensDirtIntensity = bLensEffects ? kStartupLensDirtIntensity : 0.0f;
            camera.GradingOverride.bEnabled = true;
            camera.GradingOverride.Contrast = 1.15f;
            camera.GradingOverride.ContrastPivot = 0.5f;
            camera.GradingOverride.Saturation = 1.6f;
            camera.GradingOverride.Temperature = 0.0f;
            camera.GradingOverride.VignetteIntensity = 0.25f;
            camera.GradingOverride.VignetteRadius = 0.85f;
            camera.GradingOverride.VignetteSoftness = 0.6f;
        }

        void UnregisterRendering3DInput(GameModeContext& ctx, Rendering3DTestData& data)
        {
            auto& inputRouter = ctx.EngineRef.GetInputRouter();
            inputRouter.UnregisterController(&data.m_CameraController);
            inputRouter.UnregisterController(&data.m_CameraInputCollector);
            inputRouter.UnregisterController(&data.m_PickingController);
            inputRouter.UnregisterController(&data.m_LightController);
            inputRouter.UnregisterController(&data.m_DebugInput);
            data.m_CameraInputCollector.ResetAll();
        }

        void ClearCameraReferences(Rendering3DTestData& data)
        {
            data.m_pCameraPivotObject = nullptr;
            data.m_pCameraObject = nullptr;
            data.m_pSpringArmComponent = nullptr;
            data.m_pCameraComponent = nullptr;
            data.m_bCameraSmokeSyncEmitted = false;
            data.m_bCameraSmokeCompleteEmitted = false;
        }

        bool InitializeCameraPath(GameModeContext& ctx, Rendering3DTestData& data)
        {
            data.m_CameraController.Initialize(Math::Vector3(0.0f, 0.0f, 0.0f), 5.0f, 0.0f, 30.0f);
            data.m_CameraInputCollector.ResetAll();
            data.m_bCameraSmokeSyncEmitted = false;
            data.m_bCameraSmokeCompleteEmitted = false;

            auto& inputRouter = ctx.EngineRef.GetInputRouter();
            inputRouter.RegisterController(
                &data.m_CameraInputCollector,
                NorvesLib::Core::Input::InputRouter::PriorityGame);
            inputRouter.RegisterController(
                &data.m_PickingController,
                NorvesLib::Core::Input::InputRouter::PriorityGame + 10);

            data.m_pCameraPivotObject = ctx.ScopeRef.SpawnObject<Entity>();
            data.m_pCameraObject = ctx.ScopeRef.SpawnObject<Entity>();
            if (data.m_pCameraPivotObject == nullptr || data.m_pCameraObject == nullptr)
            {
                UnregisterRendering3DInput(ctx, data);
                ClearCameraReferences(data);
                return false;
            }

            // 負荷モードは、カメラの軸を格子の中心へ移す（視点の引数はその周りの角度と距離になる）
            if (data.m_bStressTextures)
            {
                data.m_pCameraPivotObject->SetPosition(kStressGridCenterX, 0.0f, kStressGridCenterZ);
            }
            else
            {
                data.m_pCameraPivotObject->SetPosition(0.0f, 0.0f, 0.0f);
            }
            data.m_pSpringArmComponent =
                ctx.WorldRef.CreateComponent<Component::SpringArmComponent>(data.m_pCameraObject);
            data.m_pCameraComponent =
                ctx.WorldRef.CreateComponent<Component::CameraComponent>(data.m_pCameraObject);
            if (data.m_pSpringArmComponent == nullptr || data.m_pCameraComponent == nullptr ||
                !data.m_pSpringArmComponent->SetPivot(data.m_pCameraPivotObject))
            {
                UnregisterRendering3DInput(ctx, data);
                ClearCameraReferences(data);
                return false;
            }

            // 既定の視点は、球・岩・材質見本の球と奥の小屋がまとめて入るよう引いて浅く見下ろす。
            data.m_pSpringArmComponent->SetArmLength(10.0f);
            data.m_pSpringArmComponent->SetYaw(0.0f);
            data.m_pSpringArmComponent->SetPitch(20.0f);
            if (data.m_bHasStartupCamera)
            {
                data.m_pSpringArmComponent->SetArmLength(data.m_StartupCameraArmLength);
                data.m_pSpringArmComponent->SetYaw(data.m_StartupCameraYaw);
                data.m_pSpringArmComponent->SetPitch(data.m_StartupCameraPitch);
            }
            data.m_pCameraComponent->SetActiveCamera(true);
            // 晴天の昼の手動露出（f/16・1/100 s・ISO 100、EV100 約14.6）。光・発光・空は物理単位の値で、
            // 露出補正は掛けない。
            data.m_pCameraComponent->SetAperture(16.0f);
            data.m_pCameraComponent->SetShutterSpeed(1.0f / 100.0f);
            data.m_pCameraComponent->SetISO(100.0f);
            data.m_pCameraComponent->SetExposureCompensation(0.0f);
            // --exposure-ev100 の指定があれば、絞り・ISO を保ったままシャッター速度で合わせる。
            data.m_ExposureEV100 = ComputeEV100(data.m_pCameraComponent->GetAperture(),
                                                data.m_pCameraComponent->GetShutterSpeed(),
                                                data.m_pCameraComponent->GetISO());
            if (data.m_bHasStartupExposureEV100)
            {
                data.m_ExposureEV100 = data.m_StartupExposureEV100;
                data.m_pCameraComponent->SetShutterSpeed(ComputeShutterSpeedForEV100(
                    data.m_pCameraComponent->GetAperture(), data.m_pCameraComponent->GetISO(),
                    data.m_ExposureEV100));
            }
            data.m_AppliedExposureEV100 = data.m_ExposureEV100;
            // 起動画面は自動露出にする。上の手動露出は、最初の測定が出るまでの露出と、ImGui で自動を
            // 切ったときの露出になる。
            data.m_bAutoExposure = true;
            data.m_bAppliedAutoExposure = true;
            data.m_pCameraComponent->SetExposureMode(CameraExposureMode::Auto);
            data.m_pCameraComponent->SetExposureCompensation(StartupExposureCompensationEV(data.m_bAutoExposure));
            // 既定のコントラスト（1.05）は表示のリニア値 0.024 未満（sRGB で約 43/255 以下）を黒へ切り、晴天の
            // 影の中の地面（空の光だけで日向の約 2 割）が真っ黒になるため、起動画面ではこの式のコントラストを
            // 掛けない。起動画面のコントラストは、黒を切らない式で ApplyStartupGrading が差し替える。
            data.m_pCameraComponent->SetGradingContrast(1.0f);
            // 起動画面のアンチエイリアシングは TAA（--anti-aliasing=fxaa と ImGui で FXAA を選べる）。
            data.m_bTemporalAA = data.m_bStartupTemporalAA;
            data.m_bAppliedTemporalAA = data.m_bTemporalAA;
            data.m_pCameraComponent->SetAntiAliasingMode(data.m_bTemporalAA ? CameraAntiAliasingMode::TemporalAA
                                                                            : CameraAntiAliasingMode::FXAA);
            data.m_pSpringArmComponent->RefreshOwnerTransform();

            CameraProxy initialCamera;
            if (!data.m_pCameraComponent->BuildCameraProxy(initialCamera))
            {
                UnregisterRendering3DInput(ctx, data);
                ClearCameraReferences(data);
                return false;
            }

            data.m_bLensEffects = ReadStartupLensEffectsEnabled();
            data.m_bLookLut = ReadStartupLookLutEnabled();
            ApplyStartupGrading(initialCamera, data.m_bLensEffects, data.m_bLookLut);
            ctx.EngineRef.GetRenderWorld().SetMainCamera(initialCamera);
            // 起動画面の間接光はRTGI（レイトレが使えない環境では環境光（IBL）へ落ちる）。
            // 環境変数 NORVES_STARTUP_RTGI=0 で切り、IBLだけの画面と撮り比べられる。
            ctx.EngineRef.GetRenderWorld().GetRenderingCoordinator().SetRTGIEnabled(
                ReadStartupRTGIEnabled());
            // --render-scale の指定があれば、内部解像度（画面解像度×倍率）で描いて拡大する。
            if (data.m_StartupRenderScale < 1.0f)
            {
                ctx.EngineRef.GetRenderWorld().SetRenderScale(data.m_StartupRenderScale);
                LOG_INFO_F("Rendering3DTest render_scale=%.3f", static_cast<double>(data.m_StartupRenderScale));
            }
            data.m_PickingController.SetFallbackSelectionDepth(
                data.m_pSpringArmComponent->GetArmLength());
            LOG_INFO("CAMERA_COMPONENT_SMOKE stage=registered");
            EmitM9WorldSmokeMarker("CAMERA_COMPONENT_SMOKE stage=registered");
            return true;
        }

        void FailM9WorldSmoke(GameModeContext& ctx, const char* reason)
        {
            LOG_ERROR("M9_WORLD_SMOKE stage=failure reason=%s", reason);
#if defined(NORVES_GAME_AUDIO)
            EmitM9WorldSmokeMarker("M9_WORLD_SMOKE stage=failure reason=%s", reason);
#endif
            ctx.EngineRef.RequestExit(1);
        }

        void CleanupM9WorldAcceptance(GameModeContext& ctx, Rendering3DTestData& data)
        {
            auto& inputRouter = ctx.EngineRef.GetInputRouter();
            inputRouter.UnregisterController(&data.m_CameraController);
            inputRouter.UnregisterController(&data.m_CameraInputCollector);
            inputRouter.UnregisterController(&data.m_PickingController);
            inputRouter.UnregisterController(&data.m_LightController);
            inputRouter.UnregisterController(&data.m_DebugInput);
            data.m_CameraInputCollector.ResetAll();
            ClearCameraReferences(data);

#if defined(NORVES_GAME_AUDIO)
            if (data.m_M9WorldAcceptance && data.m_M9WorldAcceptance->bRequested)
            {
                auto* audioModule = NorvesLib::Modules::Audio::FindAudioModule(
                    NorvesLib::Core::Module::GetModuleRegistry());
                if (audioModule != nullptr)
                {
                    auto& audio = audioModule->GetAudioService();
                    (void)audio.Shutdown();
                }
            }
#endif

            if (!data.m_F11ImpostorComponents.empty())
            {
                ctx.EngineRef.GetRenderWorld().WaitForRender();
                for (uint32_t index = 0; index < data.m_F11ImpostorComponents.size(); ++index)
                {
                    auto* impostorComponent = data.m_F11ImpostorComponents[index];
                    if (impostorComponent)
                    {
                        impostorComponent->ReleaseBakedAtlas(ctx.RenderResourcesRef.Textures());
                        if (index < data.m_F11ImpostorSmokeAtlasHandles.size())
                        {
                            data.m_F11ImpostorSmokeAtlasHandles[index] = TextureHandle::Invalid();
                        }
                    }
                }
            }
            for (TextureHandle atlasHandle : data.m_F11ImpostorSmokeAtlasHandles)
            {
                if (atlasHandle.IsValid())
                {
                    ctx.RenderResourcesRef.Textures().ReleaseTexture(atlasHandle);
                }
            }
            data.m_F11ImpostorSmokeAtlasHandles.clear();

            if (data.m_F6AtlasTextureHandle.IsValid())
            {
                ctx.RenderResourcesRef.Textures().ReleaseTexture(data.m_F6AtlasTextureHandle);
                data.m_F6AtlasTextureHandle = TextureHandle::Invalid();
            }

            data.m_pM9SkinnedObject = nullptr;
            data.m_pM9SkinnedMeshComponent = nullptr;
            if (data.m_M9WorldAcceptance)
            {
                data.m_M9WorldAcceptance->SkeletalAsset.reset();
#if defined(NORVES_GAME_AUDIO)
                data.m_M9WorldAcceptance->EffectClip.reset();
                data.m_M9WorldAcceptance->LoopClip.reset();
#endif
                data.m_M9WorldAcceptance->bAssetsReady = false;
            }
#if defined(NORVES_GAME_AUDIO)
            data.m_M9EffectVoice = {};
            data.m_M9LoopVoice = {};
            data.m_bM9AudioStarted = false;
            data.m_bM9AudioStopIssued = false;
#endif
            data.m_M9FirstCapture = {};
            data.m_M9NegativeCapture = {};
            data.m_M9SecondCapture = {};
            data.m_M9T0PoseFingerprint = 0;
            data.m_M9T1PoseFingerprint = 0;
            data.m_M9StatsHistory.clear();
            data.m_M9TickCount = 0;
            data.m_M9CapturePhase = 0;
            data.m_M9StatsWaitTicks = 0;
            data.m_bM9NegativeCaptureRequested = false;
            data.m_bM9VisualComplete = false;
            data.m_bM9AudioComplete = false;
            data.m_bM9Completed = false;
        }

        bool EvaluateM9PixelDelta(const NorvesLib::Core::Rendering::CapturedFrame& first,
                                  const NorvesLib::Core::Rendering::CapturedFrame& second,
                                  uint32_t& outChangedPixels,
                                  float& outCentroidX,
                                  float& outCentroidY,
                                  uint32_t& outWidth,
                                  uint32_t& outHeight)
        {
            outChangedPixels = 0;
            outCentroidX = 0.0f;
            outCentroidY = 0.0f;
            outWidth = 0;
            outHeight = 0;
            if (!first.IsSuccess() || !second.IsSuccess() || first.Width != second.Width ||
                first.Height != second.Height || first.BytesPerPixel == 0 ||
                first.BytesPerPixel != second.BytesPerPixel || first.RowPitchBytes != second.RowPitchBytes)
            {
                return false;
            }

            uint32_t minX = first.Width;
            uint32_t minY = first.Height;
            uint32_t maxX = 0;
            uint32_t maxY = 0;
            uint64_t totalX = 0;
            uint64_t totalY = 0;
            for (uint32_t y = 0; y < first.Height; ++y)
            {
                for (uint32_t x = 0; x < first.Width; ++x)
                {
                    const size_t pixelOffset = static_cast<size_t>(y) * first.RowPitchBytes +
                                               static_cast<size_t>(x) * first.BytesPerPixel;
                    bool bDifferent = false;
                    for (uint32_t channel = 0; channel < first.BytesPerPixel; ++channel)
                    {
                        if (first.Pixels[pixelOffset + channel] != second.Pixels[pixelOffset + channel])
                        {
                            bDifferent = true;
                            break;
                        }
                    }
                    if (!bDifferent)
                    {
                        continue;
                    }
                    ++outChangedPixels;
                    totalX += x;
                    totalY += y;
                    minX = x < minX ? x : minX;
                    minY = y < minY ? y : minY;
                    maxX = x > maxX ? x : maxX;
                    maxY = y > maxY ? y : maxY;
                }
            }
            if (outChangedPixels == 0)
            {
                return true;
            }
            outCentroidX = static_cast<float>(totalX) / static_cast<float>(outChangedPixels);
            outCentroidY = static_cast<float>(totalY) / static_cast<float>(outChangedPixels);
            outWidth = maxX - minX + 1;
            outHeight = maxY - minY + 1;
            return outWidth > 0 && outHeight > 0;
        }
    } // namespace

    GameModeEnterResult Rendering3DTestRoutine::Enter(GameModeContext &ctx, Rendering3DTestData &data)
    {
        if (data.m_bPhysicsSmoke && !data.m_M8MinimalPhysicsSmoke.Enter(ctx))
        {
            return GameModeEnterResult::Failed;
        }

        LOG_INFO("=================================================");
        LOG_INFO("3Dレンダリングテスト開始");
        LOG_INFO("=================================================");

#if NORVES_ENABLE_CORE_TEXT
        if (ctx.EngineRef.GetRenderWorld().GetRenderingCoordinator().GetCanvasView() &&
            ctx.EngineRef.GetDefaultFontAtlas())
        {
            auto* textEntity = ctx.WorldRef.SpawnObject<Entity>();
            ctx.ScopeRef.TrackObject(textEntity);
            textEntity->SetPosition(24.0f, 48.0f, 0.0f);
            auto* text = ctx.WorldRef.CreateComponent<Component::TextComponent>(textEntity);
            text->SetText("NorvesLib M5");
            text->SetFontAtlas(ctx.EngineRef.GetDefaultFontAtlas().get());
            text->SetTint(Math::Vector4(1.0f, 1.0f, 1.0f, 1.0f));
        }
#endif

        // Maya の値 intent を SpringArm へ適用し、CameraComponent の値 proxy を
        // RenderWorld へ渡す3層カメラ経路を構築する。失敗時は借用登録と公開参照を
        // 即時に戻し、StateMachine の Scope cleanup に Entity 解放を委ねる。
        if (!InitializeCameraPath(ctx, data))
        {
            LOG_ERROR("Rendering3DTest camera path initialization failed");
            return GameModeEnterResult::Failed;
        }

        if (!data.m_M9WorldAcceptance || !data.m_M9WorldAcceptance->bRequested)
        {
            NorvesLib::Core::Particle::ParticleEmitterDesc particleDesc;
            particleDesc.Position = Math::Vector3(96.0f, 72.0f, 0.0f);
            particleDesc.VelocityMin = Math::Vector3(-16.0f, -28.0f, 0.0f);
            particleDesc.VelocityMax = Math::Vector3(16.0f, -12.0f, 0.0f);
            particleDesc.Gravity = Math::Vector3(0.0f, 16.0f, 0.0f);
            particleDesc.SpawnRate = 12.0f;
            particleDesc.Lifetime = 1.5f;
            particleDesc.MaxCount = 24u;
            particleDesc.Color = Math::Vector4(1.0f, 0.45f, 0.1f, 0.85f);
            particleDesc.SizePx = Math::Vector2(14.0f, 14.0f);
            data.m_ParticleEmitter = ctx.EngineRef.GetParticleSystem().CreateEmitter(particleDesc);
            LOG_INFO("Rendering3DTest particle emitter created valid=%d", data.m_ParticleEmitter.IsValid());
        }

        // ========================================
        // 1. プロシージャルメッシュの生成とGPU登録
        // ========================================
        {
            auto &meshes = ctx.RenderResourcesRef.Meshes();

            // 球体メッシュの生成
            VariableArray<Mesh3DVertex> sphereVertices;
            VariableArray<uint32_t> sphereIndices;
            ProceduralMeshGenerator::GenerateUVSphere(1.0f, 32, 16, sphereVertices, sphereIndices);

            bool bSphereOk = meshes.Register(
                data.m_SphereMeshHandle,
                sphereVertices.data(),
                static_cast<uint32_t>(sphereVertices.size() * sizeof(Mesh3DVertex)),
                sphereIndices.data(),
                static_cast<uint32_t>(sphereIndices.size()));

            if (bSphereOk)
            {
                ctx.ScopeRef.TrackMesh(data.m_SphereMeshHandle);
                NORVES_LOG_INFO("Rendering3DTest", "Sphere mesh registered: %zu vertices, %zu indices",
                                sphereVertices.size(), sphereIndices.size());
            }
            else
            {
                NORVES_LOG_ERROR("Rendering3DTest", "Failed to register sphere mesh");
            }

            // 地面（60 m 四方）を x 方向の帯の区画に分ける。左右の外側（|x| > 14 m）と中央の帯（大きな球の下）は
            // 石畳、ほかの幅4 mの帯は見本の材質（テクスチャが無ければ石畳）。
            data.m_GroundPieces.clear();
            {
                auto addGroundPiece = [&data](float minX, float maxX, int32_t swatchIndex, float tileMeters)
                {
                    GroundPiece piece;
                    piece.CenterX = (minX + maxX) * 0.5f;
                    piece.Width = maxX - minX;
                    piece.TileMeters = tileMeters;
                    piece.SwatchIndex = swatchIndex;
                    data.m_GroundPieces.push_back(piece);
                };
                const float halfGround = kGroundSize * 0.5f;
                addGroundPiece(-halfGround, -kGroundSwatchHalfSpan, -1, kGroundTileSize);
                const uint32_t stripCount = static_cast<uint32_t>(2.0f * kGroundSwatchHalfSpan / kGroundSwatchWidth + 0.5f);
                for (uint32_t stripIndex = 0; stripIndex < stripCount; ++stripIndex)
                {
                    const float stripMinX = -kGroundSwatchHalfSpan + static_cast<float>(stripIndex) * kGroundSwatchWidth;
                    const float stripCenterX = stripMinX + kGroundSwatchWidth * 0.5f;
                    int32_t swatchIndex = -1;
                    for (uint32_t candidate = 0; candidate < kGroundSwatchCount; ++candidate)
                    {
                        if (std::fabs(kGroundSwatches[candidate].CenterX - stripCenterX) < 0.01f)
                        {
                            swatchIndex = static_cast<int32_t>(candidate);
                        }
                    }
                    if (swatchIndex >= 0 && !AreGroundSwatchTexturesPresent(data, kGroundSwatches[swatchIndex]))
                    {
                        NORVES_LOG_WARNING("Rendering3DTest",
                                           "地面の見本の材質のテクスチャが無いので、その帯は石畳で描きます: %s"
                                           "（Scripts/FetchPolyHavenTextures.ps1 で Assets/Textures/PolyHaven へ落とせます）",
                                           kGroundSwatches[swatchIndex].AssetId);
                        swatchIndex = -1;
                    }
                    addGroundPiece(stripMinX,
                                   stripMinX + kGroundSwatchWidth,
                                   swatchIndex,
                                   swatchIndex >= 0 ? kGroundSwatches[swatchIndex].TileMeters : kGroundTileSize);
                }
                addGroundPiece(kGroundSwatchHalfSpan, halfGround, -1, kGroundTileSize);
            }

            // 区画ごとの平面。UVは世界のx・zをテクスチャ1枚の実寸で割った値（石畳の区画は、以前の60 m四方の
            // 1枚の地面と同じ模様の位置になる）。格子は約7.5 m間隔。
            bool bGroundOk = true;
            for (uint32_t pieceIndex = 0; pieceIndex < data.m_GroundPieces.size(); ++pieceIndex)
            {
                GroundPiece &piece = data.m_GroundPieces[pieceIndex];
                piece.MeshHandle = MeshDataHandle{Rendering3DTestData::kGroundPieceMeshHandleBase + pieceIndex};
                const uint32_t subdivisionsX =
                    std::max(1u, static_cast<uint32_t>(piece.Width / 7.5f + 0.5f));
                VariableArray<Mesh3DVertex> pieceVertices;
                VariableArray<uint32_t> pieceIndices;
                ProceduralMeshGenerator::GeneratePlane(piece.Width, kGroundSize, subdivisionsX, 8, pieceVertices, pieceIndices);
                const float halfGround = kGroundSize * 0.5f;
                for (Mesh3DVertex &vertex : pieceVertices)
                {
                    vertex.TexCoord[0] = (vertex.Position[0] + piece.CenterX + halfGround) / piece.TileMeters;
                    vertex.TexCoord[1] = (vertex.Position[2] + halfGround) / piece.TileMeters;
                }

                const bool bPieceOk = meshes.Register(
                    piece.MeshHandle,
                    pieceVertices.data(),
                    static_cast<uint32_t>(pieceVertices.size() * sizeof(Mesh3DVertex)),
                    pieceIndices.data(),
                    static_cast<uint32_t>(pieceIndices.size()));
                if (bPieceOk)
                {
                    ctx.ScopeRef.TrackMesh(piece.MeshHandle);
                }
                else
                {
                    NORVES_LOG_ERROR("Rendering3DTest", "地面の区画のメッシュを登録できませんでした: center_x=%.1f", piece.CenterX);
                }
                bGroundOk = bGroundOk && bPieceOk;
            }
            NORVES_LOG_INFO("Rendering3DTest", "Ground pieces registered: %zu pieces", data.m_GroundPieces.size());

            // 負荷モードの板（12 m 四方の平面。UVは板の中心を原点にした位置をテクスチャ1枚の実寸で割った値）
            if (data.m_bStressTextures)
            {
                for (uint32_t panelIndex = 0; panelIndex < kStressMaterialCount; ++panelIndex)
                {
                    const MeshDataHandle panelHandle{Rendering3DTestData::kStressPanelMeshHandleBase + panelIndex};
                    VariableArray<Mesh3DVertex> panelVertices;
                    VariableArray<uint32_t> panelIndices;
                    ProceduralMeshGenerator::GeneratePlane(kStressPanelSize, kStressPanelSize, 4, 4, panelVertices, panelIndices);
                    for (Mesh3DVertex &vertex : panelVertices)
                    {
                        vertex.TexCoord[0] = (vertex.Position[0] + kStressPanelSize * 0.5f) / kStressMaterials[panelIndex].TileMeters;
                        vertex.TexCoord[1] = (vertex.Position[2] + kStressPanelSize * 0.5f) / kStressMaterials[panelIndex].TileMeters;
                    }
                    if (meshes.Register(panelHandle,
                                        panelVertices.data(),
                                        static_cast<uint32_t>(panelVertices.size() * sizeof(Mesh3DVertex)),
                                        panelIndices.data(),
                                        static_cast<uint32_t>(panelIndices.size())))
                    {
                        ctx.ScopeRef.TrackMesh(panelHandle);
                    }
                    else
                    {
                        NORVES_LOG_ERROR("Rendering3DTest", "負荷モードの板のメッシュを登録できませんでした: %s",
                                         kStressMaterials[panelIndex].AssetId);
                    }
                }
            }

            data.m_bMeshesRegistered = bSphereOk && bGroundOk;

            // 大きな球の高ポリのMegaGeometry。既定はクック済み（NVMESH v1。クッカーが変位した球の階層を焼いてある）を
            // 別スレッドで読み、クック済みが無ければ同じジョブの中で BuildBigSphereMegaData の頂点・変位・クラスタを作る。
            // どちらも石畳のテクスチャがそろったら MegaMesh にして、上の通常のメッシュの球（仮の球）と差し替える。
            // ジョブを投げられなければここで用意する。
            {
                auto sphereData = MakeShared<ProceduralMegaSphereData>();
                auto cookedSphere = MakeShared<Asset::CookedMeshData>();
                data.m_pBigSphereMegaData = sphereData;
                data.m_pBigSphereCooked = cookedSphere;
                const bool bTryCooked = !data.m_bBigSphereFromRuntime;
                const TSharedPtr<const Asset::AssetSystem> assetSystem =
                    bTryCooked && data.m_GetAssetSystem.IsBound() ? data.m_GetAssetSystem.Invoke()
                                                                  : TSharedPtr<const Asset::AssetSystem>();
                NorvesLib::Thread::TaskPtr buildTask = NorvesLib::Thread::Task::Create(
                    [sphereData, cookedSphere, assetSystem, bTryCooked]()
                    { PrepareBigSphereMeshData(bTryCooked, assetSystem, *cookedSphere, *sphereData); });
                if (buildTask && NorvesLib::Thread::JobSystem::Get().SubmitTask(buildTask))
                {
                    data.m_BigSphereBuildTask = buildTask;
                }
                else
                {
                    PrepareBigSphereMeshData(bTryCooked, assetSystem, *cookedSphere, *sphereData);
                }
            }

            // ポイントライト光源球体メッシュの生成（小さい球体: 半径0.15）
            VariableArray<Mesh3DVertex> lightSphereVertices;
            VariableArray<uint32_t> lightSphereIndices;
            ProceduralMeshGenerator::GenerateUVSphere(0.15f, 16, 8, lightSphereVertices, lightSphereIndices);

            bool bLightSphereOk = meshes.Register(
                data.m_LightSphereMeshHandle,
                lightSphereVertices.data(),
                static_cast<uint32_t>(lightSphereVertices.size() * sizeof(Mesh3DVertex)),
                lightSphereIndices.data(),
                static_cast<uint32_t>(lightSphereIndices.size()));

            if (bLightSphereOk)
            {
                ctx.ScopeRef.TrackMesh(data.m_LightSphereMeshHandle);
                NORVES_LOG_INFO("Rendering3DTest", "Light sphere mesh registered: %zu vertices, %zu indices",
                                lightSphereVertices.size(), lightSphereIndices.size());
            }

            data.m_bMeshesRegistered = bSphereOk && bGroundOk && bLightSphereOk;
        }

        // ========================================
        // 1.5 プロシージャルチェッカーボードテクスチャ生成
        // ========================================
        {
            auto &textures = ctx.RenderResourcesRef.Textures();

            constexpr uint32_t TEX_SIZE = 256;
            constexpr uint32_t CHECKER_SIZE = 32; // 32ピクセルごとに色が切り替わる
            VariableArray<uint8_t> checkerData(TEX_SIZE * TEX_SIZE * 4);

            for (uint32_t y = 0; y < TEX_SIZE; ++y)
            {
                for (uint32_t x = 0; x < TEX_SIZE; ++x)
                {
                    uint32_t idx = (y * TEX_SIZE + x) * 4;
                    bool bWhite = ((x / CHECKER_SIZE) + (y / CHECKER_SIZE)) % 2 == 0;
                    uint8_t c = bWhite ? 230 : 50;
                    checkerData[idx + 0] = c;
                    checkerData[idx + 1] = c;
                    checkerData[idx + 2] = c;
                    checkerData[idx + 3] = 255;
                }
            }

            TextureCreateInfo texInfo;
            texInfo.Width = TEX_SIZE;
            texInfo.Height = TEX_SIZE;
            texInfo.PixelFormat = TextureCreateInfo::Format::RGBA8_UNORM;
            texInfo.DebugName = "CheckerboardTexture";

            data.m_CheckerTextureHandle = textures.CreateTexture(
                texInfo, checkerData.data(), static_cast<uint32_t>(checkerData.size()));

            if (data.m_CheckerTextureHandle.IsValid())
            {
                NORVES_LOG_INFO("Rendering3DTest", "Checkerboard texture created (256x256)");
            }
            else
            {
                NORVES_LOG_ERROR("Rendering3DTest", "Failed to create checkerboard texture");
            }
        }

        // ========================================
        // 1.6 マテリアルの作成（テクスチャは非同期読み込み）
        // ========================================
        {
            auto &renderResources = ctx.RenderResourcesRef;
            auto &textures = renderResources.Textures();
            auto &materials = renderResources.Materials();

            // --- Silver PBRマテリアル（テクスチャなしで先に作成、後で差し替え） ---
            {
                MaterialCreateData silverMatInfo;
                silverMatInfo.DebugName = "SilverPBR";
                data.m_SilverMaterial = materials.Create(silverMatInfo);

                auto silverUpdate = MakeShared<PendingMaterialUpdate>();
                silverUpdate->TargetMaterial = data.m_SilverMaterial;
                silverUpdate->CreateData = silverMatInfo;

                MaterialTexturePaths silverPaths;
                silverPaths.Albedo = "Assets/Textures/Silver/silver_albedo.png";
                silverPaths.Normal = "Assets/Textures/Silver/silver_normal-ogl.png";
                silverPaths.Orm = "Assets/Textures/Silver/silver_orm";
                silverPaths.Metallic = "Assets/Textures/Silver/silver_metallic.png";
                silverPaths.Roughness = "Assets/Textures/Silver/silver_roughness.png";
                silverPaths.AO = "Assets/Textures/Silver/silver_ao.png";
                RequestMaterialTextures(data, textures, silverUpdate, silverPaths,
                                        [silverUpdate, &materials]()
                                        {
                                            materials.Update(silverUpdate->TargetMaterial, silverUpdate->CreateData);
                                            NORVES_LOG_INFO("Rendering3DTest", "Silver PBR material textures loaded");
                                        });

                data.m_PendingMaterialUpdates.push_back(silverUpdate);
                NORVES_LOG_INFO("Rendering3DTest", "Silver PBR material created (textures loading async)");
            }

            // --- CobbleStoneFloor（石畳）マテリアル（テクスチャなしで先に作成） ---
            // 地面も同じ石畳のテクスチャを使う。読み込みは1回だけで、そろったら両方の材質へ入れる。
            {
                MaterialCreateData cobbleMatInfo;
                cobbleMatInfo.HeightScale = 0.05f;
                cobbleMatInfo.DebugName = "CobbleStoneFloor";
                data.m_CobbleStoneMaterial = materials.Create(cobbleMatInfo);

                MaterialCreateData groundMatInfo;
                groundMatInfo.DebugName = "Ground";
                data.m_GroundMaterial = materials.Create(groundMatInfo);

                auto cobbleUpdate = MakeShared<PendingMaterialUpdate>();
                cobbleUpdate->TargetMaterial = data.m_CobbleStoneMaterial;
                cobbleUpdate->CreateData = cobbleMatInfo;

                // 地面の石畳は2 mのタイルで、仮の球（UVの1周が約6.3 m）より1タイルが小さいため、
                // 凹凸の深さが同じ程度になるよう高さのスケールを小さくする（kGroundHeightScale）。
                const MaterialHandle groundMaterial = data.m_GroundMaterial;
                auto finishCobbleStone = [cobbleUpdate, groundMaterial, &materials]()
                {
                    materials.Update(cobbleUpdate->TargetMaterial, cobbleUpdate->CreateData);
                    MaterialCreateData groundData = cobbleUpdate->CreateData;
                    groundData.HeightScale = kGroundHeightScale;
                    groundData.DebugName = "Ground";
                    materials.Update(groundMaterial, groundData);
                    NORVES_LOG_INFO("Rendering3DTest", "CobbleStoneFloor material textures loaded");
                };

                MaterialTexturePaths cobblePaths;
                cobblePaths.Albedo = "Assets/Textures/CobbleStoneFloor/cobblestone_floor_09_diff_4k.png";
                cobblePaths.Normal = "Assets/Textures/CobbleStoneFloor/cobblestone_floor_09_nor_dx_4k.png";
                cobblePaths.Orm = "Assets/Textures/CobbleStoneFloor/cobblestone_floor_09_orm_4k";
                cobblePaths.Roughness = "Assets/Textures/CobbleStoneFloor/cobblestone_floor_09_rough_4k.png";
                cobblePaths.AO = "Assets/Textures/CobbleStoneFloor/cobblestone_floor_09_ao_4k.png";
                cobblePaths.Height = "Assets/Textures/CobbleStoneFloor/cobblestone_floor_09_disp_4k.png";
                RequestMaterialTextures(data, textures, cobbleUpdate, cobblePaths, finishCobbleStone);

                data.m_PendingMaterialUpdates.push_back(cobbleUpdate);
                data.m_CobbleStoneMaterialUpdate = cobbleUpdate;
                NORVES_LOG_INFO("Rendering3DTest", "CobbleStoneFloor material created (textures loading async)");
            }

            // --- 材質見本の球の材質（同じ色で、金属0と1 × 粗さ5段） ---
            {
                constexpr float kShowcaseMetallic[] = {0.0f, 1.0f};
                constexpr float kShowcaseRoughness[] = {0.1f, 0.3f, 0.5f, 0.7f, 0.9f};
                data.m_ShowcaseMaterials.clear();
                for (float metallic : kShowcaseMetallic)
                {
                    for (float roughness : kShowcaseRoughness)
                    {
                        MaterialCreateData showcaseMatInfo;
                        showcaseMatInfo.Metallic = metallic;
                        showcaseMatInfo.Roughness = roughness;
                        showcaseMatInfo.DebugName = "ShowcaseSphere";
                        data.m_ShowcaseMaterials.push_back(materials.Create(showcaseMatInfo));
                    }
                }
            }

            // --- 地面の見本の区画の材質（テクスチャなしで先に作り、Poly Haven のテクスチャがそろったら入れる） ---
            // テクスチャが無く石畳で描く帯の材質は作らない（無効のハンドルのまま）。
            {
                data.m_GroundSwatchMaterials.clear();
                for (uint32_t swatchIndex = 0; swatchIndex < kGroundSwatchCount; ++swatchIndex)
                {
                    bool bUsed = false;
                    for (const GroundPiece &piece : data.m_GroundPieces)
                    {
                        bUsed = bUsed || piece.SwatchIndex == static_cast<int32_t>(swatchIndex);
                    }
                    if (!bUsed)
                    {
                        data.m_GroundSwatchMaterials.push_back(MaterialHandle::Invalid());
                        continue;
                    }

                    const GroundSwatchSpec &spec = kGroundSwatches[swatchIndex];
                    const bool bHasHeight = spec.HeightDepthMeters > 0.0f;
                    MaterialCreateData swatchMatInfo;
                    // 視差の深さ（m）を、テクスチャ1枚の実寸に対する割合（高さのスケール）へ直す
                    swatchMatInfo.HeightScale = bHasHeight ? spec.HeightDepthMeters / spec.TileMeters : 0.0f;
                    swatchMatInfo.DebugName = spec.AssetId;
                    const MaterialHandle swatchMaterial = materials.Create(swatchMatInfo);
                    data.m_GroundSwatchMaterials.push_back(swatchMaterial);

                    auto swatchUpdate = MakeShared<PendingMaterialUpdate>();
                    swatchUpdate->TargetMaterial = swatchMaterial;
                    swatchUpdate->CreateData = swatchMatInfo;
                    const char *assetId = spec.AssetId;
                    auto finishSwatch = [swatchUpdate, assetId, &materials]()
                    {
                        materials.Update(swatchUpdate->TargetMaterial, swatchUpdate->CreateData);
                        NORVES_LOG_INFO("Rendering3DTest", "地面の見本の材質のテクスチャを読み込みました: %s", assetId);
                    };

                    // このエンジンの余接フレームは DirectX の向き（石畳と同じく nor_dx）
                    MaterialTexturePaths swatchPaths;
                    swatchPaths.Albedo = MakeGroundSwatchTexturePath(spec, "diff");
                    swatchPaths.Normal = MakeGroundSwatchTexturePath(spec, "nor_dx");
                    swatchPaths.Orm = String("Assets/Textures/PolyHaven/") + spec.AssetId + "/" + spec.AssetId + "_orm_4k";
                    swatchPaths.Roughness = MakeGroundSwatchTexturePath(spec, "rough");
                    swatchPaths.AO = MakeGroundSwatchTexturePath(spec, "ao");
                    if (bHasHeight)
                    {
                        swatchPaths.Height = MakeGroundSwatchTexturePath(spec, "disp");
                    }
                    RequestMaterialTextures(data, textures, swatchUpdate, swatchPaths, finishSwatch);

                    data.m_PendingMaterialUpdates.push_back(swatchUpdate);
                }
            }

            // --- 負荷モードの材質（見本の区画と同じ流れ。高さは使わない。テクスチャが無い材質は作らない） ---
            data.m_StressMaterials.clear();
            if (data.m_bStressTextures)
            {
                uint32_t stressPresentCount = 0;
                for (uint32_t stressIndex = 0; stressIndex < kStressMaterialCount; ++stressIndex)
                {
                    const StressMaterialSpec &stressSpec = kStressMaterials[stressIndex];
                    const GroundSwatchSpec textureSpec = MakeStressTextureSpec(stressSpec);
                    if (!AreGroundSwatchTexturesPresent(data, textureSpec))
                    {
                        NORVES_LOG_WARNING("Rendering3DTest",
                                           "負荷モードの材質のテクスチャが無いので、その板は置きません: %s"
                                           "（Scripts/FetchPolyHavenTextures.ps1 -StressSet で落とし、CookAssets で焼けます）",
                                           stressSpec.AssetId);
                        data.m_StressMaterials.push_back(MaterialHandle::Invalid());
                        continue;
                    }

                    MaterialCreateData stressMatInfo;
                    stressMatInfo.DebugName = stressSpec.AssetId;
                    const MaterialHandle stressMaterial = materials.Create(stressMatInfo);
                    data.m_StressMaterials.push_back(stressMaterial);
                    ++stressPresentCount;

                    auto stressUpdate = MakeShared<PendingMaterialUpdate>();
                    stressUpdate->TargetMaterial = stressMaterial;
                    stressUpdate->CreateData = stressMatInfo;
                    auto finishStress = [stressUpdate, &materials]()
                    {
                        materials.Update(stressUpdate->TargetMaterial, stressUpdate->CreateData);
                    };

                    MaterialTexturePaths stressPaths;
                    stressPaths.Albedo = MakeGroundSwatchTexturePath(textureSpec, "diff");
                    stressPaths.Normal = MakeGroundSwatchTexturePath(textureSpec, "nor_dx");
                    stressPaths.Orm = String("Assets/Textures/PolyHaven/") + stressSpec.AssetId + "/" + stressSpec.AssetId + "_orm_4k";
                    stressPaths.Roughness = MakeGroundSwatchTexturePath(textureSpec, "rough");
                    stressPaths.AO = MakeGroundSwatchTexturePath(textureSpec, "ao");
                    RequestMaterialTextures(data, textures, stressUpdate, stressPaths, finishStress);

                    data.m_PendingMaterialUpdates.push_back(stressUpdate);
                }
                NORVES_LOG_INFO("Rendering3DTest", "STRESS_TEXTURES materials=%u of %u",
                                static_cast<unsigned>(stressPresentCount), static_cast<unsigned>(kStressMaterialCount));
            }

            // 光源球体マテリアル作成（エミッシブ、テクスチャ不要）
            MaterialCreateData lightSphereMatInfo;
            lightSphereMatInfo.EmissiveColor[0] = kLightBulbColor[0];
            lightSphereMatInfo.EmissiveColor[1] = kLightBulbColor[1];
            lightSphereMatInfo.EmissiveColor[2] = kLightBulbColor[2];
            // 内面つや消しの電球の見かけの表面の輝度（約15 cd/cm² = 150000 nits）で描く。光束1600 lmを半径約3 cmの
            // 球から一様に出したときの平均（Φ/(π·4πr²) ≈ 45000 nits）より、フィラメントの光が集まる正面は明るい。
            // 見える球（半径0.15 m）の面で割った値（約1800 nits）では夕の自動露出でも背景の数倍にとどまり
            // ブルームでにじまないため、電球そのものの明るさで描く。照明は点光源の光束のまま。
            // GBufferへはプリエクスポージャ後の値を書くため、昼・夕の露出では半精度の範囲に十分収まる。
            lightSphereMatInfo.EmissiveLuminanceNits = 150000.0f;
            lightSphereMatInfo.DebugName = "LightSphere";
            data.m_LightSphereMaterial = materials.Create(lightSphereMatInfo);
        }

        // ========================================
        // 2. Entityの作成とメッシュコンポーネントの追加
        // ========================================
        {
            auto &world = ctx.WorldRef;

            // --- 球体オブジェクト ---
            data.m_pSphereObject = world.SpawnObject<Entity>();
            ctx.ScopeRef.TrackObject(data.m_pSphereObject);

            // 球体を地面の上に置く（半径1.0、地面Y=-1.0 → 中心Y=0.0で接地）
            data.m_pSphereObject->SetPosition(0.0f, 0.0f, 0.0f);

            data.m_pSphereMeshComponent = world.CreateComponent<Component::MeshComponent>(data.m_pSphereObject);
            data.m_pSphereMeshComponent->SetMeshHandle(data.m_SphereMeshHandle);
            data.m_pSphereMeshComponent->SetCastShadow(true);
            // オブジェクトカラー（白 = テクスチャカラーをそのまま使用）
            data.m_pSphereMeshComponent->SetCustomData(0, 1.0f);
            data.m_pSphereMeshComponent->SetCustomData(1, 1.0f);
            data.m_pSphereMeshComponent->SetCustomData(2, 1.0f);
            data.m_pSphereMeshComponent->SetCustomData(3, 1.0f);
            // CobbleStoneFloor（石畳）マテリアルを適用
            data.m_pSphereMeshComponent->SetMaterial(0, data.m_CobbleStoneMaterial);

            LOG_INFO("Sphere Entity created and added to World");

            if (data.m_InstancedMeshCount > 0u)
            {
                constexpr uint32_t kInstancedMeshGridColumns = 8u;
                constexpr float kInstancedMeshSpacing = 1.25f;
                constexpr float kInstancedMeshStartZ = -4.5f;
                const float startX =
                    -static_cast<float>(kInstancedMeshGridColumns - 1u) * kInstancedMeshSpacing * 0.5f;

                for (uint32_t meshIndex = 0; meshIndex < data.m_InstancedMeshCount; ++meshIndex)
                {
                    const float column = static_cast<float>(meshIndex % kInstancedMeshGridColumns);
                    const float row = static_cast<float>(meshIndex / kInstancedMeshGridColumns);
                    const float x = startX + column * kInstancedMeshSpacing;
                    const float z = kInstancedMeshStartZ + row * kInstancedMeshSpacing;

                    Entity* instancedObject = world.SpawnObject<Entity>();
                    ctx.ScopeRef.TrackObject(instancedObject);
                    instancedObject->SetPosition(x, 0.5f, z);

                    Component::MeshComponent* instancedMeshComponent =
                        world.CreateComponent<Component::MeshComponent>(instancedObject);
                    instancedMeshComponent->SetMeshHandle(data.m_SphereMeshHandle);
                    instancedMeshComponent->SetCastShadow(true);
                    instancedMeshComponent->SetCustomData(0, 1.0f);
                    instancedMeshComponent->SetCustomData(1, 1.0f);
                    instancedMeshComponent->SetCustomData(2, 1.0f);
                    instancedMeshComponent->SetCustomData(3, 1.0f);
                    instancedMeshComponent->SetMaterial(0, data.m_CobbleStoneMaterial);
                }

                LOG_INFO("Rendering3DTest instanced mesh grid created count=%u",
                         data.m_InstancedMeshCount);
            }

            // --- 地面の区画のオブジェクト（Y=-1.0、帯の中心のxに置く） ---
            // 石畳の区画は地面の材質、見本の区画はその材質。中央の帯（大きな球の下）を地面の代表として持つ。
            data.m_pGroundObject = nullptr;
            data.m_pGroundMeshComponent = nullptr;
            for (GroundPiece &piece : data.m_GroundPieces)
            {
                piece.Material = piece.SwatchIndex >= 0
                                     ? data.m_GroundSwatchMaterials[static_cast<uint32_t>(piece.SwatchIndex)]
                                     : data.m_GroundMaterial;
                piece.pObject = world.SpawnObject<Entity>();
                ctx.ScopeRef.TrackObject(piece.pObject);
                piece.pObject->SetPosition(piece.CenterX, -1.0f, 0.0f);

                Component::MeshComponent *pieceMeshComponent =
                    world.CreateComponent<Component::MeshComponent>(piece.pObject);
                pieceMeshComponent->SetMeshHandle(piece.MeshHandle);
                pieceMeshComponent->SetCastShadow(false);
                // オブジェクトカラー（白 = テクスチャの色をそのまま使う）→ CustomData
                pieceMeshComponent->SetCustomData(0, 1.0f);
                pieceMeshComponent->SetCustomData(1, 1.0f);
                pieceMeshComponent->SetCustomData(2, 1.0f);
                pieceMeshComponent->SetCustomData(3, 1.0f);
                pieceMeshComponent->SetMaterial(0, piece.Material);

                if (piece.SwatchIndex < 0 && std::fabs(piece.CenterX) < 0.01f)
                {
                    data.m_pGroundObject = piece.pObject;
                    data.m_pGroundMeshComponent = pieceMeshComponent;
                }
            }

            LOG_INFO("Ground pieces created and added to World count=%zu", data.m_GroundPieces.size());

            // --- 負荷モードの板（Y=-1.0。材質が作れた板だけ置く） ---
            if (data.m_bStressTextures)
            {
                for (uint32_t panelIndex = 0; panelIndex < data.m_StressMaterials.size(); ++panelIndex)
                {
                    if (!data.m_StressMaterials[panelIndex].IsValid())
                    {
                        continue;
                    }
                    float panelX = 0.0f;
                    float panelZ = 0.0f;
                    GetStressPanelCenter(panelIndex, panelX, panelZ);
                    Entity *panelObject = world.SpawnObject<Entity>();
                    ctx.ScopeRef.TrackObject(panelObject);
                    panelObject->SetPosition(panelX, -1.0f, panelZ);

                    Component::MeshComponent *panelMeshComponent =
                        world.CreateComponent<Component::MeshComponent>(panelObject);
                    panelMeshComponent->SetMeshHandle(MeshDataHandle{Rendering3DTestData::kStressPanelMeshHandleBase + panelIndex});
                    panelMeshComponent->SetCastShadow(false);
                    panelMeshComponent->SetCustomData(0, 1.0f);
                    panelMeshComponent->SetCustomData(1, 1.0f);
                    panelMeshComponent->SetCustomData(2, 1.0f);
                    panelMeshComponent->SetCustomData(3, 1.0f);
                    panelMeshComponent->SetMaterial(0, data.m_StressMaterials[panelIndex]);
                }
                LOG_INFO("Stress panels created and added to World count=%zu", data.m_StressMaterials.size());
            }

            // --- ポイントライト光源球体オブジェクト ---
            data.m_pLightSphereObject = world.SpawnObject<Entity>();
            ctx.ScopeRef.TrackObject(data.m_pLightSphereObject);

            // 球体の横に配置（X=4.0, Y=1.0, Z=0.0）――少し遠め
            data.m_pLightSphereObject->SetPosition(4.0f, 1.0f, 0.0f);

            data.m_pLightSphereMeshComponent = world.CreateComponent<Component::MeshComponent>(data.m_pLightSphereObject);
            data.m_pLightSphereMeshComponent->SetMeshHandle(data.m_LightSphereMeshHandle);
            data.m_pLightSphereMeshComponent->SetCastShadow(false); // 光源自体は影を落とさない
            // 電球の色（発光体の見た目）
            data.m_pLightSphereMeshComponent->SetCustomData(0, kLightBulbColor[0]);
            data.m_pLightSphereMeshComponent->SetCustomData(1, kLightBulbColor[1]);
            data.m_pLightSphereMeshComponent->SetCustomData(2, kLightBulbColor[2]);
            data.m_pLightSphereMeshComponent->SetCustomData(3, 1.0f);
            // 光源球体マテリアル（エミッシブ設定はマテリアル側に移動済み）
            data.m_pLightSphereMeshComponent->SetMaterial(0, data.m_LightSphereMaterial);

            // PointLightComponentの追加（SceneViewへのLightProxy登録はWorld::SyncToSceneView()で自動化）
            data.m_pPointLightComponent = world.CreateComponent<Component::PointLightComponent>(data.m_pLightSphereObject);
            data.m_pPointLightComponent->SetLightColor(kLightBulbColor[0], kLightBulbColor[1], kLightBulbColor[2]);
            // 100 W 形の電球相当の光束（1600 lm）。
            data.m_pPointLightComponent->SetIntensityUnit(Component::LightIntensityUnit::Lumen);
            data.m_pPointLightComponent->SetIntensity(1600.0f);
            data.m_pPointLightComponent->SetRange(10.0f);
            data.m_pPointLightComponent->SetLightVisible(true);
            // キューブシャドウで球・岩・材質見本の球の影を地面へ落とす。
            data.m_pPointLightComponent->SetCastShadows(true);
            LOG_INFO("Light sphere Entity created and added to World");

            // --- 材質見本の球の列 ---
            // 球（中心X=0）の左に、手前の列を非金属、奥の列を金属として粗さ0.1〜0.9を左から並べる。
            // 奥の列は既定の視点で手前の列に隠れないよう間を空ける。
            {
                constexpr uint32_t kShowcaseColumns = 5u;
                constexpr float kShowcaseRadius = 0.4f;
                constexpr float kShowcaseSpacing = 1.0f;
                constexpr float kShowcaseStartX = -6.4f;
                constexpr float kShowcaseRowZ[] = {1.2f, -0.8f};
                // 見本の色（リニア）。金属では反射の色になる。
                constexpr float kShowcaseColor[3] = {0.9f, 0.7f, 0.4f};
                data.m_ShowcaseSphereObjects.clear();
                for (uint32_t materialIndex = 0; materialIndex < data.m_ShowcaseMaterials.size(); ++materialIndex)
                {
                    const uint32_t row = materialIndex / kShowcaseColumns;
                    const uint32_t column = materialIndex % kShowcaseColumns;
                    Entity *showcaseObject = world.SpawnObject<Entity>();
                    ctx.ScopeRef.TrackObject(showcaseObject);
                    showcaseObject->SetPosition(kShowcaseStartX + static_cast<float>(column) * kShowcaseSpacing,
                                                -1.0f + kShowcaseRadius,
                                                kShowcaseRowZ[row]);
                    showcaseObject->SetScale(kShowcaseRadius, kShowcaseRadius, kShowcaseRadius);

                    Component::MeshComponent *showcaseMeshComponent =
                        world.CreateComponent<Component::MeshComponent>(showcaseObject);
                    showcaseMeshComponent->SetMeshHandle(data.m_SphereMeshHandle);
                    showcaseMeshComponent->SetCastShadow(true);
                    showcaseMeshComponent->SetCustomData(0, kShowcaseColor[0]);
                    showcaseMeshComponent->SetCustomData(1, kShowcaseColor[1]);
                    showcaseMeshComponent->SetCustomData(2, kShowcaseColor[2]);
                    showcaseMeshComponent->SetCustomData(3, 1.0f);
                    showcaseMeshComponent->SetMaterial(0, data.m_ShowcaseMaterials[materialIndex]);
                    data.m_ShowcaseSphereObjects.push_back(showcaseObject);
                }
                LOG_INFO("Rendering3DTest showcase spheres created count=%zu", data.m_ShowcaseSphereObjects.size());
            }

            // --- 物理空と空の太陽 ---
            // 空を有効にすると、エンジンが空の太陽の方向光（影を落とす）を光源表へ加え、IBLも空から作る。
            // 太陽は仰角40°・方位30°（+X寄り・カメラ側の上空）に置き、既定の視点から球と岩の影が
            // 左奥の地面へ落ちるようにする。
            data.m_SkyAtmosphere = MakeDefaultSkyAtmosphereParameters();
            data.m_SkyAtmosphere.bEnabled = true;
            data.m_SkyAtmosphere.SunAltitudeDegrees = 40.0f;
            data.m_SkyAtmosphere.SunAzimuthDegrees = 30.0f;
            if (data.m_bHasStartupSunElevation)
            {
                data.m_SkyAtmosphere.SunAltitudeDegrees = data.m_StartupSunElevation;
            }
            if (data.m_bHasStartupSunAzimuth)
            {
                data.m_SkyAtmosphere.SunAzimuthDegrees = data.m_StartupSunAzimuth;
            }
            // --night: 空（と空の太陽の方向光）を消し、環境光を空が無効なときの静的HDRにして月明かり程度へ
            // 落とす。点光源（1600 lm）だけが地面を照らすので、点光源の影が目で見える。
            if (data.m_bStartupNight)
            {
                data.m_SkyAtmosphere.bEnabled = false;
            }
            ctx.EngineRef.GetRenderWorld().SetStaticEnvironmentIntensityScale(
                data.m_bStartupNight ? kNightStaticEnvironmentIntensityScale : 1.0f);
            ctx.EngineRef.GetRenderWorld().SetSkyAtmosphere(data.m_SkyAtmosphere);
            if (data.m_bStartupNight)
            {
                LOG_INFO("Rendering3DTest night enabled static_environment_scale=%.3f",
                         kNightStaticEnvironmentIntensityScale);
            }
            else
            {
                LOG_INFO("Sky atmosphere enabled sun_altitude=%.1f sun_azimuth=%.1f",
                         data.m_SkyAtmosphere.SunAltitudeDegrees, data.m_SkyAtmosphere.SunAzimuthDegrees);
            }
            data.m_bSunStepApplied = false;
            data.m_SunStepElapsedSeconds = 0.0;
            data.m_bHasSunStep = TryReadStartupSunStep(data.m_SunStepElevation, data.m_SunStepDelaySeconds);
            if (data.m_bHasSunStep)
            {
                LOG_INFO("Sun step scheduled elevation=%.1f delay_s=%.2f", data.m_SunStepElevation,
                         data.m_SunStepDelaySeconds);
            }

            // R3 の高さフォグ。地面（y=-1）で最も濃く、上へ行くほど薄くする。密度が0なら無効。
            {
                VolumetricFogParameters fog = MakeDefaultVolumetricFogParameters();
                fog.DensityAtBaseHeight = data.m_bHasStartupHeightFogDensity ? data.m_StartupHeightFogDensity
                                                                              : kStartupHeightFogDensity;
                fog.bEnabled = fog.DensityAtBaseHeight > 0.0f;
                fog.BaseHeight = -1.0f;
                fog.HeightFalloffPerUnit = data.m_bHasStartupHeightFogFalloff ? data.m_StartupHeightFogFalloff
                                                                               : kStartupHeightFogFalloff;
                ctx.EngineRef.GetRenderWorld().SetVolumetricFogParameters(fog);
            }

            // 方向ライト操作コントローラーを空の太陽の置き場へ接続し、入力ルーターへ
            // ゲーム優先度で登録する。矢印キーの角度は Tick で空の太陽の仰角・方位へ写す。
            // 空の太陽の明るさは空が決めるので、+/- の強度操作は使わない（速度0）。
            // 自バインドキー（矢印・+/-・Shift）のみ consume し他は透過するため、
            // 同優先度の debug コントローラと共存する。
            {
                float yaw = 0.0f;
                float pitch = 0.0f;
                ConvertSkySunToLightControllerAngles(data.m_SkyAtmosphere.SunAltitudeDegrees,
                                                     data.m_SkyAtmosphere.SunAzimuthDegrees, yaw, pitch);
                data.m_SkySunControlLight = LightProxy{};
                data.m_LightController.SetTargetComponent(nullptr);
                data.m_LightController.SetTargetLight(&data.m_SkySunControlLight);
                data.m_LightController.SetIntensitySpeed(0.0f);
                data.m_LightController.SetDirection(yaw, pitch);
            }
            ctx.EngineRef.GetInputRouter().RegisterController(
                &data.m_LightController,
                NorvesLib::Core::Input::InputRouter::PriorityGame);

            // F1-F5 デバッグビュー切替コントローラーを RenderWorld へ接続し登録する。
            // F1-F5 のみ consume・他は透過。light コントローラと同優先度で並ぶ。
            data.m_DebugInput.SetRenderWorld(&ctx.EngineRef.GetRenderWorld());
            ctx.EngineRef.GetInputRouter().RegisterController(
                &data.m_DebugInput,
                NorvesLib::Core::Input::InputRouter::PriorityGame);

            // ImGui 有効時のみ、空の太陽の編集 view を本段(Rendering3DTest)へ併走 push する。
            // push は遅延適用され同一ドレイン内で現在の top 段へ積まれ Enter(=RegisterImGuiView)される。
            // MakeUnique<派生>(=std::make_unique)の prvalue は ISubRoutine の仮想デストラクタにより
            // TUniquePtr<ISubRoutine>(=std::unique_ptr<ISubRoutine>)の値引数へ暗黙 upcast move される。
#if defined(NORVES_ENABLE_IMGUI)
            ctx.ControllerRef.RequestPushSubRoutine(
                MakeUnique<DirectionalLightEditSubRoutine>(&data.m_LightController, &data.m_ExposureEV100,
                                                          &data.m_bAutoExposure,
                                                          &data.m_AutoExposureMeasurement,
                                                          &data.m_bTemporalAA,
                                                          &data.m_bLensEffects,
                                                          &data.m_bLookLut));
#endif
        }

        // ========================================
        // 2.5 F5 ScreenSpace Board showcase
        // ========================================
        {
            auto canvasView = ctx.EngineRef.GetRenderWorld().GetRenderingCoordinator().GetCanvasView();
            if (canvasView)
            {
                auto &world = ctx.WorldRef;
                auto &textures = ctx.RenderResourcesRef.Textures();
                LOG_INFO("Rendering3DTest board smoke count=%u batching=%s",
                         data.m_BoardSmokeCount,
                         canvasView->IsBoardInstanceBatchingEnabled() ? "enabled" : "disabled");
                if (data.m_bLayerCompositeSmoke)
                {
                    canvasView->SetLayerCompositeMode(1u, CanvasLayerCompositeMode::OwnRT);
                    canvasView->SetLayerOpacity(1u, 0.55f);
                    LOG_INFO("Rendering3DTest layer composite smoke enabled layer=1 mode=OwnRT opacity=0.55");
                }

                constexpr uint32_t atlasWidth = 128;
                constexpr uint32_t atlasHeight = 64;
                constexpr uint32_t atlasCellWidth = 32;
                constexpr uint32_t atlasCellHeight = 32;
                VariableArray<uint8_t> atlasData(atlasWidth * atlasHeight * 4u);
                const Math::Vector4 atlasColors[] =
                {
                    Math::Vector4(0.95f, 0.20f, 0.25f, 1.0f),
                    Math::Vector4(0.20f, 0.80f, 0.35f, 1.0f),
                    Math::Vector4(0.20f, 0.45f, 1.00f, 1.0f),
                    Math::Vector4(0.95f, 0.75f, 0.20f, 1.0f),
                    Math::Vector4(0.80f, 0.25f, 0.95f, 1.0f),
                    Math::Vector4(0.20f, 0.85f, 0.85f, 1.0f),
                    Math::Vector4(1.00f, 0.50f, 0.20f, 1.0f),
                    Math::Vector4(0.90f, 0.90f, 0.90f, 1.0f),
                };

                for (uint32_t y = 0; y < atlasHeight; ++y)
                {
                    for (uint32_t x = 0; x < atlasWidth; ++x)
                    {
                        const uint32_t cellX = x / atlasCellWidth;
                        const uint32_t cellY = y / atlasCellHeight;
                        const uint32_t cellIndex = cellY * (atlasWidth / atlasCellWidth) + cellX;
                        const uint32_t localX = x % atlasCellWidth;
                        const uint32_t localY = y % atlasCellHeight;
                        const float normalizedX = static_cast<float>(localX) / static_cast<float>(atlasCellWidth - 1u);
                        const float normalizedY = static_cast<float>(localY) / static_cast<float>(atlasCellHeight - 1u);
                        const bool bDiagonal = localX >= localY / 2u;
                        const bool bCutout = localX > atlasCellWidth / 3u &&
                                             localX < (atlasCellWidth * 2u) / 3u &&
                                             localY > atlasCellHeight / 4u &&
                                             localY < (atlasCellHeight * 3u) / 4u;
                        const float alphaScale = bCutout ? 0.35f : 1.0f;
                        const Math::Vector4 &cellColor = atlasColors[cellIndex % static_cast<uint32_t>(sizeof(atlasColors) / sizeof(atlasColors[0]))];
                        const float brightness = bDiagonal ? 1.0f : 0.45f;
                        const uint32_t pixelIndex = (y * atlasWidth + x) * 4u;
                        atlasData[pixelIndex + 0u] = static_cast<uint8_t>(255.0f * cellColor.x * brightness);
                        atlasData[pixelIndex + 1u] = static_cast<uint8_t>(255.0f * cellColor.y * (0.7f + 0.3f * normalizedX));
                        atlasData[pixelIndex + 2u] = static_cast<uint8_t>(255.0f * cellColor.z * (0.7f + 0.3f * normalizedY));
                        atlasData[pixelIndex + 3u] = static_cast<uint8_t>(255.0f * alphaScale);
                    }
                }

                TextureCreateInfo atlasInfo;
                atlasInfo.Width = atlasWidth;
                atlasInfo.Height = atlasHeight;
                atlasInfo.PixelFormat = TextureCreateInfo::Format::RGBA8_UNORM;
                atlasInfo.DebugName = "F6BoardAtlas";
                data.m_F6AtlasTextureHandle = textures.CreateTexture(
                    atlasInfo,
                    atlasData.data(),
                    static_cast<uint32_t>(atlasData.size()));

                const VariableArray<Math::Vector4> atlasRects =
                    Component::BoardComponent::ComputeSpriteSheetUVRects(
                        atlasWidth,
                        atlasHeight,
                        atlasCellWidth,
                        atlasCellHeight);

                struct F5BoardSpec
                {
                    float X;
                    float Y;
                    float Width;
                    float Height;
                    BlendMode BlendModeProp;
                    Math::Vector4 Tint;
                    bool bFlipX;
                    bool bFlipY;
                    Math::Vector2 Pivot;
                    Math::Vector2 SizePx;
                    uint32_t LayerPriority;
                    uint32_t OrderInLayer;
                    uint32_t AtlasRectIndex;
                    bool bAnimated;
                };

                const F5BoardSpec boardSpecs[] =
                {
                    {72.0f, 72.0f, 160.0f, 96.0f, BlendMode::Translucent, Math::Vector4(1.00f, 1.00f, 1.00f, 0.90f), false, false, Math::Vector2(0.0f, 0.0f), Math::Vector2(0.0f, 0.0f), 0u, 0u, 0u, false},
                    {112.0f, 104.0f, 180.0f, 104.0f, BlendMode::Opaque, Math::Vector4(1.00f, 1.00f, 1.00f, 1.00f), false, false, Math::Vector2(0.0f, 0.0f), Math::Vector2(0.0f, 0.0f), 0u, 1u, 1u, false},
                    {176.0f, 132.0f, 132.0f, 72.0f, BlendMode::Additive, Math::Vector4(0.85f, 0.85f, 0.85f, 0.50f), false, false, Math::Vector2(0.0f, 0.0f), Math::Vector2(0.0f, 0.0f), 0u, 2u, 2u, false},
                    {472.0f, 108.0f, 72.0f, 72.0f, BlendMode::Translucent, Math::Vector4(1.00f, 1.00f, 1.00f, 0.80f), true, false, Math::Vector2(0.0f, 0.0f), Math::Vector2(140.0f, 40.0f), 0u, 3u, 4u, false},
                    {400.0f, 260.0f, 96.0f, 96.0f, BlendMode::Translucent, Math::Vector4(1.00f, 1.00f, 1.00f, 0.95f), false, false, Math::Vector2(0.5f, 0.5f), Math::Vector2(112.0f, 112.0f), 1u, 0u, 0u, true},
                };

                constexpr uint32_t boardSpecCount = static_cast<uint32_t>(sizeof(boardSpecs) / sizeof(boardSpecs[0]));

                data.m_F4BoardObjects.clear();
                data.m_F4BoardComponents.clear();
                const uint32_t totalBoardReserve = boardSpecCount + data.m_BoardSmokeCount;
                data.m_F4BoardObjects.reserve(totalBoardReserve);
                data.m_F4BoardComponents.reserve(totalBoardReserve);

                for (const F5BoardSpec &boardSpec : boardSpecs)
                {
                    Entity *boardObject = world.SpawnObject<Entity>();
                    ctx.ScopeRef.TrackObject(boardObject);
                    boardObject->SetPosition(boardSpec.X, boardSpec.Y, 0.0f);
                    boardObject->SetScale(boardSpec.Width, boardSpec.Height, 1.0f);

                    auto *boardComponent = world.CreateComponent<Component::BoardComponent>(boardObject);
                    boardComponent->SetBoardSpace(BoardSpace::ScreenSpace);
                    boardComponent->SetRenderLayer(RenderLayer::UI);
                    boardComponent->SetBlendMode(boardSpec.BlendModeProp);
                    boardComponent->SetTint(boardSpec.Tint);
                    boardComponent->SetFlipX(boardSpec.bFlipX);
                    boardComponent->SetFlipY(boardSpec.bFlipY);
                    boardComponent->SetPivot(boardSpec.Pivot);
                    boardComponent->SetSizePx(boardSpec.SizePx);
                    boardComponent->SetLayerPriority(boardSpec.LayerPriority);
                    boardComponent->SetOrderInLayer(boardSpec.OrderInLayer);
                    boardComponent->SetVisible(true);
                    if (data.m_F6AtlasTextureHandle.IsValid())
                    {
                        boardComponent->SetTextureHandle(data.m_F6AtlasTextureHandle);
                        if (boardSpec.AtlasRectIndex < atlasRects.size())
                        {
                            boardComponent->SetUVRect(atlasRects[boardSpec.AtlasRectIndex]);
                        }

                        if (boardSpec.bAnimated)
                        {
                            boardComponent->SetFrameCount(static_cast<uint32_t>(atlasRects.size()));
                            if (boardComponent->SetFlipbookGrid(atlasWidth, atlasHeight, atlasCellWidth, atlasCellHeight, 0u))
                            {
                                boardComponent->SetFramesPerSecond(4.0f);
                                boardComponent->SetLoop(true);
                                boardComponent->Play();
                            }
                        }
                    }

                    data.m_F4BoardObjects.push_back(boardObject);
                    data.m_F4BoardComponents.push_back(boardComponent);
                }

                LOG_INFO("F5/F7 ScreenSpace Board showcase created with blend, tint, flip, pivot, size, atlas UV, and flipbook variants");

                if (data.m_BoardSmokeCount > 0u)
                {
                    if (!data.m_F6AtlasTextureHandle.IsValid() || atlasRects.empty())
                    {
                        LOG_WARNING("Rendering3DTest board smoke skipped because atlas texture creation failed");
                    }
                    else
                    {
                        constexpr uint32_t smokeColumns = 16u;
                        constexpr float smokeCellWidth = 28.0f;
                        constexpr float smokeCellHeight = 22.0f;
                        constexpr float smokeStartX = 32.0f;
                        constexpr float smokeStartY = 360.0f;
                        for (uint32_t smokeIndex = 0; smokeIndex < data.m_BoardSmokeCount; ++smokeIndex)
                        {
                            const uint32_t column = smokeIndex % smokeColumns;
                            const uint32_t row = smokeIndex / smokeColumns;

                            Entity *boardObject = world.SpawnObject<Entity>();
                            ctx.ScopeRef.TrackObject(boardObject);
                            boardObject->SetPosition(smokeStartX + static_cast<float>(column) * smokeCellWidth,
                                                     smokeStartY + static_cast<float>(row) * smokeCellHeight,
                                                     0.0f);
                            boardObject->SetScale(20.0f, 16.0f, 1.0f);

                            auto *boardComponent = world.CreateComponent<Component::BoardComponent>(boardObject);
                            boardComponent->SetBoardSpace(BoardSpace::ScreenSpace);
                            boardComponent->SetRenderLayer(RenderLayer::UI);
                            boardComponent->SetBlendMode(BlendMode::Translucent);
                            boardComponent->SetTint(Math::Vector4(0.55f + 0.05f * static_cast<float>(column % 6u),
                                                                  0.45f + 0.08f * static_cast<float>(row % 5u),
                                                                  0.80f,
                                                                  0.70f));
                            boardComponent->SetLayerPriority(2u);
                            boardComponent->SetOrderInLayer(smokeIndex);
                            boardComponent->SetTextureHandle(data.m_F6AtlasTextureHandle);
                            boardComponent->SetUVRect(atlasRects[smokeIndex % static_cast<uint32_t>(atlasRects.size())]);
                            boardComponent->SetVisible(true);

                            data.m_F4BoardObjects.push_back(boardObject);
                            data.m_F4BoardComponents.push_back(boardComponent);
                        }

                        LOG_INFO("Rendering3DTest board smoke grid created count=%u batching=%s",
                                 data.m_BoardSmokeCount,
                                 canvasView->IsBoardInstanceBatchingEnabled() ? "enabled" : "disabled");
                    }
                }
            }
            else if (data.m_BoardSmokeCount > 0u || data.m_bLayerCompositeSmoke)
            {
                LOG_WARNING("Rendering3DTest Canvas smoke requested but CanvasView is not available");
            }
        }

        // ========================================
        // 2.6 F9 WorldSpace Billboard smoke
        // ========================================
        if (data.m_BillboardSmokeCount > 0u)
        {
            auto &world = ctx.WorldRef;
            data.m_F9BillboardObjects.clear();
            data.m_F9BillboardComponents.clear();
            data.m_F9BillboardObjects.reserve(data.m_BillboardSmokeCount);
            data.m_F9BillboardComponents.reserve(data.m_BillboardSmokeCount);

            for (uint32_t smokeIndex = 0; smokeIndex < data.m_BillboardSmokeCount; ++smokeIndex)
            {
                const float column = static_cast<float>(smokeIndex % 5u);
                const float row = static_cast<float>(smokeIndex / 5u);
                const float x = -2.0f + column * 1.0f;
                const float y = 0.45f + row * 0.65f;
                const float z = (smokeIndex % 3u == 0u) ? -0.65f : ((smokeIndex % 3u == 1u) ? 0.0f : 0.65f);

                Entity *billboardObject = world.SpawnObject<Entity>();
                ctx.ScopeRef.TrackObject(billboardObject);
                billboardObject->SetPosition(x, y, z);

                auto *billboardComponent = world.CreateComponent<Component::BillboardComponent>(billboardObject);
                billboardComponent->SetRenderLayer(RenderLayer::Default);
                billboardComponent->SetBlendMode(BlendMode::Translucent);
                billboardComponent->SetSizeWorld(Math::Vector2(0.55f, 0.55f));
                billboardComponent->SetPivot(Math::Vector2(0.5f, 0.5f));
                billboardComponent->SetTint(Math::Vector4(0.85f,
                                                          0.45f + 0.10f * static_cast<float>(smokeIndex % 4u),
                                                          0.20f + 0.12f * static_cast<float>(smokeIndex % 5u),
                                                          0.82f));
                if (data.m_CheckerTextureHandle.IsValid())
                {
                    billboardComponent->SetTextureHandle(data.m_CheckerTextureHandle);
                }
                billboardComponent->SetVisible(true);

                data.m_F9BillboardObjects.push_back(billboardObject);
                data.m_F9BillboardComponents.push_back(billboardComponent);
            }

            LOG_INFO("Rendering3DTest billboard smoke created count=%u texture=%s",
                     data.m_BillboardSmokeCount,
                     data.m_CheckerTextureHandle.IsValid() ? "checker" : "white-fallback");
        }

        // ========================================
        // 2.7 F11 WorldSpace Impostor smoke
        // ========================================
        if (data.m_ImpostorSmokeCount > 0u)
        {
            auto &world = ctx.WorldRef;
            auto &textures = ctx.RenderResourcesRef.Textures();
            data.m_F11ImpostorSmokeObjects.clear();
            data.m_F11ImpostorSourceMeshComponents.clear();
            data.m_F11ImpostorComponents.clear();
            data.m_F11ImpostorSmokeAtlasHandles.clear();
            data.m_F11ImpostorSmokeObjects.reserve(data.m_ImpostorSmokeCount);
            data.m_F11ImpostorSourceMeshComponents.reserve(data.m_ImpostorSmokeCount);
            data.m_F11ImpostorComponents.reserve(data.m_ImpostorSmokeCount);
            data.m_F11ImpostorSmokeAtlasHandles.reserve(data.m_ImpostorSmokeCount);

            constexpr uint32_t impostorCellResolution = 32u;
            constexpr uint32_t impostorAxisCellCountX = 4u;
            constexpr uint32_t impostorAxisCellCountY = 4u;
            constexpr uint32_t impostorAtlasWidth = impostorCellResolution * impostorAxisCellCountX;
            constexpr uint32_t impostorAtlasHeight = impostorCellResolution * impostorAxisCellCountY;

            VariableArray<uint8_t> impostorAtlasData(impostorAtlasWidth * impostorAtlasHeight * 4u);
            for (uint32_t y = 0; y < impostorAtlasHeight; ++y)
            {
                for (uint32_t x = 0; x < impostorAtlasWidth; ++x)
                {
                    const uint32_t cellX = x / impostorCellResolution;
                    const uint32_t cellY = y / impostorCellResolution;
                    const uint32_t localX = x % impostorCellResolution;
                    const uint32_t localY = y % impostorCellResolution;
                    const uint32_t pixelIndex = (y * impostorAtlasWidth + x) * 4u;
                    const uint8_t red = static_cast<uint8_t>(70u + cellX * 42u);
                    const uint8_t green = static_cast<uint8_t>(70u + cellY * 42u);
                    const uint8_t blue = static_cast<uint8_t>(120u + ((localX + localY) % 48u));
                    const bool bCross = localX == localY ||
                                        localX + localY + 1u == impostorCellResolution;
                    impostorAtlasData[pixelIndex + 0u] = bCross ? 245u : red;
                    impostorAtlasData[pixelIndex + 1u] = bCross ? 245u : green;
                    impostorAtlasData[pixelIndex + 2u] = blue;
                    impostorAtlasData[pixelIndex + 3u] = 220u;
                }
            }

            ImpostorBakeMetadata metadata;
            metadata.CellResolution = impostorCellResolution;
            metadata.AxisCellCountX = impostorAxisCellCountX;
            metadata.AxisCellCountY = impostorAxisCellCountY;
            metadata.AtlasWidth = impostorAtlasWidth;
            metadata.AtlasHeight = impostorAtlasHeight;
            metadata.VertexCount = 3u;
            metadata.IndexCount = 3u;
            metadata.PixelFormat = TextureCreateInfo::Format::RGBA8_UNORM;

            uint32_t createdCount = 0u;
            for (uint32_t smokeIndex = 0; smokeIndex < data.m_ImpostorSmokeCount; ++smokeIndex)
            {
                TextureCreateInfo atlasInfo;
                atlasInfo.Width = impostorAtlasWidth;
                atlasInfo.Height = impostorAtlasHeight;
                atlasInfo.PixelFormat = TextureCreateInfo::Format::RGBA8_UNORM;
                atlasInfo.DebugName = "F11ImpostorSmokeAtlas";

                const TextureHandle atlasTexture = textures.CreateTexture(
                    atlasInfo,
                    impostorAtlasData.data(),
                    static_cast<uint32_t>(impostorAtlasData.size()));
                if (!atlasTexture.IsValid())
                {
                    NORVES_LOG_WARNING("Rendering3DTest",
                                       "F11 impostor smoke atlas creation failed at index=%u",
                                       smokeIndex);
                    continue;
                }

                const float column = static_cast<float>(smokeIndex % 4u);
                const float row = static_cast<float>(smokeIndex / 4u);
                const float x = -1.8f + column * 1.2f;
                const float y = 0.65f + row * 0.85f;
                const float z = 1.4f + static_cast<float>(smokeIndex % 2u) * 0.55f;

                Entity *impostorObject = world.SpawnObject<Entity>();
                ctx.ScopeRef.TrackObject(impostorObject);
                impostorObject->SetPosition(x, y, z);

                auto *sourceMeshComponent = world.CreateComponent<Component::MeshComponent>(impostorObject);
                sourceMeshComponent->SetMeshHandle(data.m_SphereMeshHandle);
                sourceMeshComponent->SetMaterial(0, data.m_SilverMaterial);
                sourceMeshComponent->SetCastShadow(true);
                sourceMeshComponent->SetCustomData(0, 0.35f);
                sourceMeshComponent->SetCustomData(1, 0.75f);
                sourceMeshComponent->SetCustomData(2, 1.0f);
                sourceMeshComponent->SetCustomData(3, 1.0f);

                auto *impostorComponent = world.CreateComponent<Component::ImpostorComponent>(impostorObject);
                impostorComponent->SetRenderLayer(RenderLayer::Default);
                impostorComponent->SetBlendMode(BlendMode::Translucent);
                impostorComponent->SetSizeWorld(Math::Vector2(0.9f, 0.9f));
                impostorComponent->SetPivot(Math::Vector2(0.5f, 0.5f));
                impostorComponent->SetTint(Math::Vector4(1.0f, 1.0f, 1.0f, 0.88f));
                impostorComponent->SetSourceMeshComponentId(sourceMeshComponent->GetComponentId());
                impostorComponent->SetLODSwitchDistance(3.5f);
                if (!impostorComponent->SetBakedAtlas(atlasTexture, metadata))
                {
                    textures.ReleaseTexture(atlasTexture);
                    ctx.ScopeRef.Untrack(impostorObject);
                    world.RemoveObject(impostorObject);
                    continue;
                }
                impostorComponent->SetVisible(true);

                data.m_F11ImpostorSmokeObjects.push_back(impostorObject);
                data.m_F11ImpostorSourceMeshComponents.push_back(sourceMeshComponent);
                data.m_F11ImpostorComponents.push_back(impostorComponent);
                data.m_F11ImpostorSmokeAtlasHandles.push_back(atlasTexture);
                ++createdCount;
            }

            LOG_INFO("Rendering3DTest impostor smoke created count=%u requested=%u atlas=procedural-f11-grid sourceMesh=sphere switchDistance=3.5",
                     createdCount,
                     data.m_ImpostorSmokeCount);
        }

        // ========================================
        // 3. glTFモデルのロード
        // ========================================
        if (!data.m_M9WorldAcceptance || !data.m_M9WorldAcceptance->bRequested)
        {
            auto& world = ctx.WorldRef;
            auto &renderResources = ctx.RenderResourcesRef;
            ModelLoadResourceContext modelLoadContext{
                renderResources.Textures(),
                renderResources.MegaGeometry()};

            // 非同期ロード中に表示する簡易プレースホルダ
            data.m_pBoulderPlaceholderObject = world.SpawnObject<Entity>();
            ctx.ScopeRef.TrackObject(data.m_pBoulderPlaceholderObject);
            data.m_pBoulderPlaceholderObject->SetPosition(3.0f, -0.25f, 0.0f);
            data.m_pBoulderPlaceholderObject->SetScale(0.75f, 0.75f, 0.75f);

            data.m_pBoulderPlaceholderMeshComponent = world.CreateComponent<Component::MeshComponent>(data.m_pBoulderPlaceholderObject);
            data.m_pBoulderPlaceholderMeshComponent->SetMeshHandle(data.m_SphereMeshHandle);
            data.m_pBoulderPlaceholderMeshComponent->SetMaterial(0, data.m_SilverMaterial);
            data.m_pBoulderPlaceholderMeshComponent->SetCastShadow(true);

            data.m_bBoulderModelLoaded = false;
            data.m_bBoulderModelLoadPending = true;

            auto asyncState = MakeShared<BoulderAsyncState>();
            data.m_BoulderAsyncState = asyncState;

            const String modelPath = data.m_ModelPath.empty()
                                         ? String("Assets/Models/boulder_01_4k.gltf/boulder_01_4k.gltf")
                                         : data.m_ModelPath;
            // 既定の岩・小屋はクック済み（NVMESH v1・BC・VT）で読む。クック済みが無い・解析できないときは、
            // 警告して従来の glTF の実行時の経路へ戻す（--rendering3dtest-model-source=gltf は最初から glTF）。
            bool bBoulderCooked = false;
            bool bCottageCooked = false;
            if (!data.m_bUseCookedModel && !data.m_bStartupModelsFromGltf && modelPath == String(kStartupBoulderMeshPath))
            {
                bBoulderCooked = StartCookedStartupModelLoad(ctx, data, "Boulder", kStartupBoulderMeshPath,
                                                             MakeStartupBoulderTexturePaths(), true, asyncState);
                if (!bBoulderCooked)
                {
                    NORVES_LOG_WARNING("Rendering3DTest",
                                       "COOKED_MODEL_MISSING path=%s クック済みのモデルが無いため、glTF の実行時の経路で読みます"
                                       "（CookAssets の対象を実行すると焼けます）",
                                       kStartupBoulderMeshPath);
                }
            }
            Delegate<void, ModelHandle> modelLoadedCallback =
                [asyncState](ModelHandle handle)
                {
                    // cancelled 済みで到着した結果は破棄する。Leave 側で
                    // 発行元の CancelModelLoad と finalize 済みハンドルの
                    // 明示解放を行うため、ここでの use-after-free・リークは防がれる。
                    asyncState->m_Handle = handle;
                    asyncState->m_bLoaded = handle.IsValid();
                    asyncState->m_bCompleted.Store(true);
                };
            if (bBoulderCooked)
            {
                // 材質（VT）がそろったら、Tick の FinishCookedStartupModels が MegaMesh を作って asyncState を埋める。
            }
            else if (data.m_bUseCookedModel)
            {
                data.m_BoulderLoadRequestId = renderResources.MegaGeometry().LoadModelAsync(
                    modelPath,
                    std::move(modelLoadedCallback));
            }
            else
            {
                data.m_BoulderLoadRequestId = Resource::GLTFAnalyzer::LoadModelAsync(
                    modelPath,
                    modelLoadContext,
                    std::move(modelLoadedCallback));
            }

            if (bBoulderCooked)
            {
                NORVES_LOG_INFO("Rendering3DTest", "Boulder model (cooked) load started: %s", kStartupBoulderMeshPath);
            }
            else if (data.m_BoulderLoadRequestId == 0)
            {
                data.m_bBoulderModelLoadPending = false;
                asyncState->m_bLoaded = false;
                asyncState->m_bCompleted.Store(true);
                NORVES_LOG_ERROR("Rendering3DTest", "Boulderモデルの非同期ロード開始に失敗しました");
            }
            else
            {
                NORVES_LOG_INFO("Rendering3DTest", "Boulder model async load started: %s", modelPath.c_str());
            }

            // 小屋（Scripts/ConvertObjToGltf.py で OBJ から変換した glTF）。cooked モデルの計測では読み込まない。
            // 既定ではクック済みで読み、無ければ警告して glTF の経路へ戻す。
            if (!data.m_bUseCookedModel && !data.m_bStartupModelsFromGltf)
            {
                auto cookedCottageState = MakeShared<BoulderAsyncState>();
                bCottageCooked = StartCookedStartupModelLoad(ctx, data, "Cottage", kStartupCottageMeshPath,
                                                             MakeStartupCottageTexturePaths(), false, cookedCottageState);
                if (bCottageCooked)
                {
                    data.m_CottageAsyncState = cookedCottageState;
                }
                else
                {
                    NORVES_LOG_WARNING("Rendering3DTest",
                                       "COOKED_MODEL_MISSING path=%s クック済みのモデルが無いため、glTF の実行時の経路で読みます"
                                       "（CookAssets の対象を実行すると焼けます）",
                                       kStartupCottageMeshPath);
                }
            }
            if (!data.m_bUseCookedModel && !bCottageCooked)
            {
                auto cottageState = MakeShared<BoulderAsyncState>();
                data.m_CottageAsyncState = cottageState;
                data.m_CottageLoadRequestId = Resource::GLTFAnalyzer::LoadModelAsync(
                    String("Assets/Models/Cottage_Clean/Cottage_Clean.gltf"),
                    modelLoadContext,
                    [cottageState](ModelHandle handle)
                    {
                        cottageState->m_Handle = handle;
                        cottageState->m_bLoaded = handle.IsValid();
                        cottageState->m_bCompleted.Store(true);
                    });
                if (data.m_CottageLoadRequestId == 0)
                {
                    data.m_CottageAsyncState.reset();
                    NORVES_LOG_ERROR("Rendering3DTest", "小屋のモデルの非同期ロード開始に失敗しました");
                }
            }

            // 地面の外周のスキャン資産は、既定のクック済みの経路のときだけ読む（glTF の経路は持たない）。
            data.m_ScanPropLoads.clear();
            if (!data.m_bUseCookedModel && !data.m_bStartupModelsFromGltf && data.m_bStartupScanProps)
            {
                StartStartupScanPropLoads(ctx, data);
            }
        }

#if defined(NORVES_GAME_AUDIO)
        if (data.m_M9WorldAcceptance && data.m_M9WorldAcceptance->bRequested)
        {
            if (!InitializeM9WorldSkeletal(ctx, data))
            {
                CleanupM9WorldAcceptance(ctx, data);
                FailM9WorldSmoke(ctx, "skeletal_assets_not_ready");
                return GameModeEnterResult::Failed;
            }
            auto* audioModule = NorvesLib::Modules::Audio::FindAudioModule(
                NorvesLib::Core::Module::GetModuleRegistry());
            if (audioModule == nullptr || !data.m_M9WorldAcceptance->EffectClip || !data.m_M9WorldAcceptance->LoopClip)
            {
                CleanupM9WorldAcceptance(ctx, data);
                FailM9WorldSmoke(ctx, "xaudio2_module_or_clip_unavailable");
                return GameModeEnterResult::Failed;
            }
            auto& audio = audioModule->GetAudioService();
            if (audio.CreateVoice(data.m_M9WorldAcceptance->EffectClip, data.m_M9EffectVoice) != NorvesLib::Modules::Audio::AudioResult::Success ||
                audio.CreateVoice(data.m_M9WorldAcceptance->LoopClip, data.m_M9LoopVoice) != NorvesLib::Modules::Audio::AudioResult::Success ||
                audio.StartVoice(data.m_M9EffectVoice) != NorvesLib::Modules::Audio::AudioResult::Success ||
                audio.StartVoice(data.m_M9LoopVoice) != NorvesLib::Modules::Audio::AudioResult::Success)
            {
                CleanupM9WorldAcceptance(ctx, data);
                FailM9WorldSmoke(ctx, "xaudio2_play_failed");
                return GameModeEnterResult::Failed;
            }
            data.m_bM9AudioStarted = true;
            LOG_INFO("M9_WORLD_SMOKE stage=audio_play effect=1 loop=1 backend=XAudio2");
            EmitM9WorldSmokeMarker("M9_WORLD_SMOKE stage=audio_play effect=1 loop=1 backend=XAudio2");
        }
#endif

        // 決定的な撮影では、組み立てが終わるまで読み込み中として扱う（最初の Tick が判定する）。
        ctx.EngineRef.GetDeterministicCapture().SetSceneReady(false);

        return GameModeEnterResult::Succeeded;
    }

    void Rendering3DTestRoutine::Tick(GameModeContext &ctx, Rendering3DTestData &data, float deltaTime)
    {
        if (data.m_bPhysicsSmoke)
        {
            data.m_M8MinimalPhysicsSmoke.Update(ctx);
        }

        // 空の太陽の操作の連続 hold 適用。held フラグは LightController::OnKey が
        // InputRouter 経由で更新する。ImGui がキーボードを掴んでいる間は
        // 上位で consume されるため held は積まれず、ここでも動かない（排他）。
        data.m_LightController.Update(deltaTime);

        // NORVES_STARTUP_SUN_STEP の指定があれば、起動からその秒数の後に一度だけ太陽の仰角を急に変える。
        // 操作の角度へ書くので、下の空への写しで同じフレームに空へ渡る。
        if (data.m_bHasSunStep && !data.m_bSunStepApplied && data.m_SkyAtmosphere.bEnabled)
        {
            data.m_SunStepElapsedSeconds += static_cast<double>(deltaTime);
            if (data.m_SunStepElapsedSeconds >= static_cast<double>(data.m_SunStepDelaySeconds))
            {
                float yaw = 0.0f;
                float pitch = 0.0f;
                ConvertSkySunToLightControllerAngles(data.m_SunStepElevation,
                                                     data.m_SkyAtmosphere.SunAzimuthDegrees, yaw, pitch);
                data.m_LightController.SetDirection(yaw, pitch);
                data.m_bSunStepApplied = true;
                LOG_INFO("Sun step applied elevation=%.1f elapsed_s=%.3f", data.m_SunStepElevation,
                         data.m_SunStepElapsedSeconds);
            }
        }

        // 操作の角度（矢印キー・ImGui）を空の太陽の仰角・方位へ写す。太陽は地平線より下へ
        // 行かせない（光が上向きに進む角度は水平へ戻す）。変わったときだけ空へ渡し、
        // 空由来のIBLの作り直しを角度が動いたフレームに限る。
        if (data.m_SkyAtmosphere.bEnabled)
        {
            if (data.m_LightController.GetPitch() > 0.0f)
            {
                data.m_LightController.SetDirection(data.m_LightController.GetYaw(), 0.0f);
            }
            float altitude = 0.0f;
            float azimuth = 0.0f;
            ConvertLightControllerAnglesToSkySun(data.m_LightController.GetYaw(),
                                                 data.m_LightController.GetPitch(), altitude, azimuth);
            if (std::abs(altitude - data.m_SkyAtmosphere.SunAltitudeDegrees) > 1.0e-3f ||
                std::abs(azimuth - data.m_SkyAtmosphere.SunAzimuthDegrees) > 1.0e-3f)
            {
                data.m_SkyAtmosphere.SunAltitudeDegrees = altitude;
                data.m_SkyAtmosphere.SunAzimuthDegrees = azimuth;
                ctx.EngineRef.GetRenderWorld().SetSkyAtmosphere(data.m_SkyAtmosphere);
            }
        }

        // RenderThread が読み戻した自動露出の目標・順応後の EV100 を、統計のスナップショットから表示用へ写す。
        data.m_AutoExposureMeasurement =
            ctx.EngineRef.GetRenderWorld().GetRenderingCoordinator().GetStatsSnapshot().AutoExposure;

        // ImGui で切り替えた自動露出の有無を、カメラの露出の方式へ写す。
        if (data.m_pCameraComponent != nullptr && data.m_bAutoExposure != data.m_bAppliedAutoExposure)
        {
            data.m_pCameraComponent->SetExposureMode(data.m_bAutoExposure ? CameraExposureMode::Auto
                                                                          : CameraExposureMode::Manual);
            data.m_pCameraComponent->SetExposureCompensation(StartupExposureCompensationEV(data.m_bAutoExposure));
            data.m_bAppliedAutoExposure = data.m_bAutoExposure;
        }

        // ImGui で切り替えたアンチエイリアシング（TAA/FXAA）を、カメラの方式へ写す。
        if (data.m_pCameraComponent != nullptr && data.m_bTemporalAA != data.m_bAppliedTemporalAA)
        {
            data.m_pCameraComponent->SetAntiAliasingMode(data.m_bTemporalAA ? CameraAntiAliasingMode::TemporalAA
                                                                            : CameraAntiAliasingMode::FXAA);
            data.m_bAppliedTemporalAA = data.m_bTemporalAA;
        }

        // ImGui で動かした手動露出（EV100）を、絞り・ISO を保ったままシャッター速度へ写す。
        if (data.m_pCameraComponent != nullptr &&
            std::abs(data.m_ExposureEV100 - data.m_AppliedExposureEV100) > 1.0e-4f)
        {
            if (data.m_pCameraComponent->SetShutterSpeed(ComputeShutterSpeedForEV100(
                    data.m_pCameraComponent->GetAperture(), data.m_pCameraComponent->GetISO(),
                    data.m_ExposureEV100)))
            {
                data.m_AppliedExposureEV100 = data.m_ExposureEV100;
            }
            else
            {
                data.m_ExposureEV100 = data.m_AppliedExposureEV100;
            }
        }

        // InputRouter で上位 controller に consume されなかった値状態だけを Maya
        // の変換層へ渡し、SpringArm を単一のカメラ姿勢正本として更新する。
        // CameraProxy は値 snapshot なので RenderThread に live Object は渡らない。
        if (data.m_pSpringArmComponent != nullptr && data.m_pCameraComponent != nullptr)
        {
            const NorvesLib::Core::Input::InputState cameraInput =
                data.m_CameraInputCollector.BuildFrameInputState();
            const Component::SpringArmIntent intent = data.m_CameraController.BuildIntent(
                cameraInput,
                deltaTime,
                data.m_pSpringArmComponent->GetArmLength());
            data.m_pSpringArmComponent->ApplyIntent(intent);
            // --orbit-degrees-per-second の指定があれば、経過時間に比例してカメラを軸の周りに回す。
            if (data.m_OrbitDegreesPerSecond != 0.0f)
            {
                float yaw = data.m_pSpringArmComponent->GetYaw() + data.m_OrbitDegreesPerSecond * deltaTime;
                const auto &orbitDeterministicCapture = ctx.EngineRef.GetDeterministicCapture();
                if (orbitDeterministicCapture.IsEnabled())
                {
                    // 決定的な撮影では、読み込みにかかったフレーム数に依らないよう、旋回の角度を
                    // エポックからの固定刻みの時間で決める（エポック前は最初の向きのまま）。
                    if (!data.m_bOrbitBaseYawLatched)
                    {
                        data.m_OrbitBaseYaw = data.m_pSpringArmComponent->GetYaw();
                        data.m_bOrbitBaseYawLatched = true;
                    }
                    yaw = data.m_OrbitBaseYaw +
                          static_cast<float>(static_cast<double>(data.m_OrbitDegreesPerSecond) *
                                             orbitDeterministicCapture.GetEpochSeconds());
                }
                yaw = std::fmod(yaw, 360.0f);
                data.m_pSpringArmComponent->SetYaw(yaw);
            }
            data.m_CameraInputCollector.ResetFrame();
            data.m_pSpringArmComponent->RefreshOwnerTransform();
            data.m_PickingController.SetFallbackSelectionDepth(
                data.m_pSpringArmComponent->GetArmLength());

            const float scroll = cameraInput.GetMouseState().ScrollDelta;
            if (std::abs(scroll) > 0.0f)
            {
                const float armLength = data.m_pSpringArmComponent->GetArmLength();
                const Math::Vector3 cameraPosition = data.m_pCameraObject != nullptr
                    ? data.m_pCameraObject->GetPosition()
                    : Math::Vector3(0.0f, 0.0f, 0.0f);
                NORVES_LOG_DEBUG(
                    "Input",
                    "ScrollDelta={:.3f}, ArmLength={:.3f}, CamPos=({:.2f}, {:.2f}, {:.2f})",
                    scroll,
                    armLength,
                    cameraPosition.x,
                    cameraPosition.y,
                    cameraPosition.z);
            }

            CameraProxy cameraProxy;
            if (data.m_pCameraComponent->BuildCameraProxy(cameraProxy))
            {
                ApplyStartupGrading(cameraProxy, data.m_bLensEffects, data.m_bLookLut);
                ctx.EngineRef.GetRenderWorld().SetMainCamera(cameraProxy);
                if (!data.m_bCameraSmokeSyncEmitted)
                {
                    LOG_INFO("CAMERA_COMPONENT_SMOKE stage=sync snapshot=1");
                    EmitM9WorldSmokeMarker("CAMERA_COMPONENT_SMOKE stage=sync snapshot=1");
                    data.m_bCameraSmokeSyncEmitted = true;
                }
            }
        }

        data.m_PickingController.DrawSelection();
        if (data.m_bDebugDrawTestLines)
        {
            Game::GameModes::SubmitRendering3DTestDebugDraw();
        }

        data.m_ElapsedTime += deltaTime;

#if defined(NORVES_GAME_AUDIO)
        if (data.m_M9WorldAcceptance && data.m_M9WorldAcceptance->bRequested && !data.m_bM9Completed)
        {
            ++data.m_M9TickCount;
            auto& renderWorld = ctx.EngineRef.GetRenderWorld();
            const RenderingCoordinatorStatsSnapshot statsSnapshot =
                renderWorld.GetRenderingCoordinator().GetStatsSnapshot();
            if (statsSnapshot.bRenderFrameCompleted && statsSnapshot.Stats.FrameNumber != 0 &&
                (data.m_M9StatsHistory.empty() ||
                 data.m_M9StatsHistory.back().Stats.FrameNumber != statsSnapshot.Stats.FrameNumber))
            {
                if (data.m_M9StatsHistory.size() == 8)
                {
                    data.m_M9StatsHistory.erase(data.m_M9StatsHistory.begin());
                }
                data.m_M9StatsHistory.push_back(statsSnapshot);
            }
            if (data.m_M9CapturePhase == 0)
            {
                if (!renderWorld.RequestFrameCapture().IsAccepted())
                {
                    FailM9WorldSmoke(ctx, "t0_capture_request_rejected");
                    return;
                }
                data.m_M9CapturePhase = 1;
            }
            else if (data.m_M9CapturePhase == 1)
            {
                NorvesLib::Core::Rendering::CapturedFrame frame;
                if (renderWorld.TryConsumeCapturedFrame(frame))
                {
                    if (!frame.IsSuccess())
                    {
                        FailM9WorldSmoke(ctx, data.m_bM9NegativeCaptureRequested ? "negative_capture_failed" : "t0_capture_failed");
                        return;
                    }
                    if (!data.m_bM9NegativeCaptureRequested)
                    {
                        data.m_M9FirstCapture = std::move(frame);
                        if (!BuildM9WorldSkeletalPoseFingerprint(data, data.m_M9T0PoseFingerprint))
                        {
                            FailM9WorldSmoke(ctx, "t0_pose_fingerprint_unavailable");
                            return;
                        }
                        if (!renderWorld.RequestFrameCapture().IsAccepted())
                        {
                            FailM9WorldSmoke(ctx, "negative_capture_request_rejected");
                            return;
                        }
                        data.m_bM9NegativeCaptureRequested = true;
                    }
                    else
                    {
                        data.m_M9NegativeCapture = std::move(frame);
                        uint32_t negativeChangedPixels = 0;
                        uint32_t unusedWidth = 0;
                        uint32_t unusedHeight = 0;
                        float unusedCentroidX = 0.0f;
                        float unusedCentroidY = 0.0f;
                        if (!EvaluateM9PixelDelta(data.m_M9FirstCapture, data.m_M9NegativeCapture,
                                                  negativeChangedPixels, unusedCentroidX, unusedCentroidY,
                                                  unusedWidth, unusedHeight) || negativeChangedPixels > 32)
                        {
                            FailM9WorldSmoke(ctx, "negative_control_pixel_delta");
                            return;
                        }
                        SetM9WorldSkeletalAnimationTime(data, 1.0f);
                        if (!BuildM9WorldSkeletalPoseFingerprint(data, data.m_M9T1PoseFingerprint))
                        {
                            FailM9WorldSmoke(ctx, "t1_pose_fingerprint_unavailable");
                            return;
                        }
                        if (data.m_M9T0PoseFingerprint == data.m_M9T1PoseFingerprint)
                        {
                            FailM9WorldSmoke(ctx, "pose_time_noop");
                            return;
                        }
                        if (!renderWorld.RequestFrameCapture().IsAccepted())
                        {
                            FailM9WorldSmoke(ctx, "t1_capture_request_rejected");
                            return;
                        }
                        data.m_M9CapturePhase = 2;
                    }
                }
            }
            else if (data.m_M9CapturePhase == 2)
            {
                if (!data.m_M9SecondCapture.IsSuccess())
                {
                    NorvesLib::Core::Rendering::CapturedFrame frame;
                    if (renderWorld.TryConsumeCapturedFrame(frame))
                    {
                        if (!frame.IsSuccess())
                        {
                            FailM9WorldSmoke(ctx, "t1_capture_failed");
                            return;
                        }
                        data.m_M9SecondCapture = std::move(frame);
                        data.m_M9StatsWaitTicks = 0;
                    }
                }

                if (data.m_M9SecondCapture.IsSuccess())
                {
                    const RenderingCoordinatorStatsSnapshot* matchingStats = nullptr;
                    for (const RenderingCoordinatorStatsSnapshot& candidate : data.m_M9StatsHistory)
                    {
                        if (candidate.Stats.FrameNumber == data.m_M9SecondCapture.FrameNumber)
                        {
                            matchingStats = &candidate;
                            break;
                        }
                    }
                    if (matchingStats == nullptr)
                    {
                        if (++data.m_M9StatsWaitTicks > 16)
                        {
                            FailM9WorldSmoke(ctx, "t1_stats_frame_unmatched");
                        }
                        return;
                    }

                    uint32_t changedPixels = 0;
                    uint32_t bboxWidth = 0;
                    uint32_t bboxHeight = 0;
                    float centroidX = 0.0f;
                    float centroidY = 0.0f;
                    uint32_t negativeChangedPixels = 0;
                    uint32_t unusedWidth = 0;
                    uint32_t unusedHeight = 0;
                    float unusedCentroidX = 0.0f;
                    float unusedCentroidY = 0.0f;
                    if (!EvaluateM9PixelDelta(data.m_M9FirstCapture, data.m_M9SecondCapture,
                                              changedPixels, centroidX, centroidY, bboxWidth, bboxHeight) ||
                        changedPixels == 0 ||
                        !EvaluateM9PixelDelta(data.m_M9FirstCapture, data.m_M9NegativeCapture,
                                              negativeChangedPixels, unusedCentroidX, unusedCentroidY,
                                              unusedWidth, unusedHeight) ||
                        negativeChangedPixels > 32 ||
                        data.m_M9T0PoseFingerprint == data.m_M9T1PoseFingerprint ||
                        matchingStats->SkinnedGBufferRecordedDraws == 0 ||
                        matchingStats->SkinnedShadowRecordedDraws == 0)
                    {
                        FailM9WorldSmoke(ctx, "pixel_or_skinned_submit_validation");
                        return;
                    }
                    data.m_bM9VisualComplete = true;
                    data.m_M9CapturePhase = 3;
                    LOG_INFO("M9_WORLD_SMOKE stage=visual t0_frame=%llu t1_frame=%llu stats_frame=%llu changed_pixels=%u negative_changed_pixels=%u pose_changed=1 bbox_width=%u bbox_height=%u centroid_x=%f centroid_y=%f gbuffer=%u shadow=%u",
                             static_cast<unsigned long long>(data.m_M9FirstCapture.FrameNumber),
                             static_cast<unsigned long long>(data.m_M9SecondCapture.FrameNumber),
                             static_cast<unsigned long long>(matchingStats->Stats.FrameNumber), changedPixels,
                             negativeChangedPixels, bboxWidth, bboxHeight, centroidX, centroidY,
                             matchingStats->SkinnedGBufferRecordedDraws,
                             matchingStats->SkinnedShadowRecordedDraws);
                    EmitM9WorldSmokeMarker("M9_WORLD_SMOKE stage=visual t0_frame=%llu t1_frame=%llu stats_frame=%llu changed_pixels=%u negative_changed_pixels=%u pose_changed=1 bbox_width=%u bbox_height=%u centroid_x=%f centroid_y=%f gbuffer=%u shadow=%u",
                                           static_cast<unsigned long long>(data.m_M9FirstCapture.FrameNumber),
                                           static_cast<unsigned long long>(data.m_M9SecondCapture.FrameNumber),
                                           static_cast<unsigned long long>(matchingStats->Stats.FrameNumber), changedPixels,
                                           negativeChangedPixels, bboxWidth, bboxHeight, centroidX, centroidY,
                                           matchingStats->SkinnedGBufferRecordedDraws,
                                           matchingStats->SkinnedShadowRecordedDraws);
                }
            }

            auto* audioModule = NorvesLib::Modules::Audio::FindAudioModule(
                NorvesLib::Core::Module::GetModuleRegistry());
            if (audioModule == nullptr)
            {
                FailM9WorldSmoke(ctx, "xaudio2_module_lost");
                return;
            }
            auto& audio = audioModule->GetAudioService();
            if (data.m_bM9AudioStarted && !data.m_bM9AudioStopIssued && data.m_M9TickCount >= 4)
            {
                if (audio.StopVoice(data.m_M9EffectVoice) != NorvesLib::Modules::Audio::AudioResult::Success ||
                    audio.StopVoice(data.m_M9LoopVoice) != NorvesLib::Modules::Audio::AudioResult::Success)
                {
                    FailM9WorldSmoke(ctx, "xaudio2_stop_failed");
                    return;
                }
                data.m_bM9AudioStopIssued = true;
            }
            if (data.m_bM9AudioStopIssued && !data.m_bM9AudioComplete)
            {
                NorvesLib::Modules::Audio::AudioVoiceState effectState;
                NorvesLib::Modules::Audio::AudioVoiceState loopState;
                if (audio.GetVoiceState(data.m_M9EffectVoice, effectState) == NorvesLib::Modules::Audio::AudioResult::Success &&
                    audio.GetVoiceState(data.m_M9LoopVoice, loopState) == NorvesLib::Modules::Audio::AudioResult::Success &&
                    effectState == NorvesLib::Modules::Audio::AudioVoiceState::Drained &&
                    loopState == NorvesLib::Modules::Audio::AudioVoiceState::Drained)
                {
                    if (audio.DestroyVoice(data.m_M9EffectVoice) != NorvesLib::Modules::Audio::AudioResult::Success ||
                        audio.DestroyVoice(data.m_M9LoopVoice) != NorvesLib::Modules::Audio::AudioResult::Success ||
                        audio.Shutdown() != NorvesLib::Modules::Audio::AudioResult::Success)
                    {
                        FailM9WorldSmoke(ctx, "xaudio2_destroy_or_shutdown_failed");
                        return;
                    }
                    const auto diagnostics = audio.GetDiagnostics();
                    if (diagnostics.ActiveVoiceCount != 0 || diagnostics.PendingEventCount != 0 ||
                        diagnostics.InFlightCallbackCount != 0)
                    {
                        FailM9WorldSmoke(ctx, "xaudio2_shutdown_not_drained");
                        return;
                    }
                    data.m_bM9AudioComplete = true;
                    LOG_INFO("M9_WORLD_SMOKE stage=audio effect_play=1 effect_stop=1 effect_callback=1 loop_play=1 loop_stop=1 loop_callback=1 voices=0 callbacks=0 shutdown_complete=1");
                    EmitM9WorldSmokeMarker("M9_WORLD_SMOKE stage=audio effect_play=1 effect_stop=1 effect_callback=1 loop_play=1 loop_stop=1 loop_callback=1 voices=0 callbacks=0 shutdown_complete=1");
                }
            }

            if (data.m_bM9VisualComplete && data.m_bM9AudioComplete)
            {
                data.m_bM9Completed = true;
                LOG_INFO("M9_WORLD_SMOKE stage=complete exit_code=0");
                EmitM9WorldSmokeMarker("M9_WORLD_SMOKE stage=complete exit_code=0");
                ctx.EngineRef.RequestExit(0);
            }
        }
#endif

        // クック済みで読んでいる岩・小屋は、材質（VT）がそろったら MegaMesh を作り、下の組み立てが受け取る。
        FinishCookedStartupModels(ctx, data);

        if (data.m_BoulderAsyncState &&
            data.m_BoulderAsyncState->m_bCompleted.Load() &&
            !data.m_BoulderAsyncState->m_bCancelled.Load())
        {
            auto state = data.m_BoulderAsyncState;
            data.m_BoulderAsyncState.reset(); // 一度だけ消費

            data.m_BoulderModelHandle = state->m_Handle;
            data.m_bBoulderModelLoaded = state->m_bLoaded;
            data.m_bBoulderModelLoadPending = false;

            if (!data.m_bBoulderModelLoaded)
            {
                NORVES_LOG_ERROR("Rendering3DTest", "Boulderモデルのロードに失敗しました");
            }
            else
            {
                auto &world = ctx.WorldRef;
                auto &megaGeometry = ctx.RenderResourcesRef.MegaGeometry();
                auto megaMeshHandle = megaGeometry.GetModelMegaMeshHandle(data.m_BoulderModelHandle);
                if (!megaMeshHandle.IsValid())
                {
                    NORVES_LOG_ERROR("Rendering3DTest", "BoulderモデルからMegaMeshを取得できませんでした");
                    megaGeometry.ReleaseModel(data.m_BoulderModelHandle);
                    data.m_BoulderModelHandle = ModelHandle::Invalid();
                    data.m_bBoulderModelLoaded = false;
                }
                else
                {
                    if (data.m_pBoulderPlaceholderObject)
                    {
                        // スコープ追跡から外してから World から除去する。
                        // これ以降このポインタは解放されるため、Cleanup が
                        // 解放済みポインタへ RemoveObject しないようにする。
                        ctx.ScopeRef.Untrack(data.m_pBoulderPlaceholderObject);
                        world.RemoveObject(data.m_pBoulderPlaceholderObject);
                        data.m_pBoulderPlaceholderObject = nullptr;
                        data.m_pBoulderPlaceholderMeshComponent = nullptr;
                    }

                    if (!data.m_pBoulderObject)
                    {
                        data.m_pBoulderObject = world.SpawnObject<Entity>();
                        ctx.ScopeRef.TrackObject(data.m_pBoulderObject);
                        // 岩のモデルの最下点はY=-0.074なので、地面（Y=-1.0）に接するよう下げる。
                        data.m_pBoulderObject->SetPosition(3.0f, -0.93f, 0.0f);

                        data.m_pBoulderMegaGeometryComponent = world.CreateComponent<Component::MegaGeometryComponent>(data.m_pBoulderObject);
                        data.m_pBoulderMegaGeometryComponent->SetMegaMeshHandle(megaMeshHandle);
                        data.m_pBoulderMegaGeometryComponent->SetCastShadow(true);
                    }

                    // 消費したモデルはスコープに解放を委ねる（成功パスのみ）。
                    // 失敗パスは即時 ReleaseModel 済みのため追跡してはならない。
                    ctx.ScopeRef.TrackModel(data.m_BoulderModelHandle);

                    NORVES_LOG_INFO("Rendering3DTest", "Boulder model loaded and added to World");
                }
            }
        }

        if (data.m_CottageAsyncState &&
            data.m_CottageAsyncState->m_bCompleted.Load() &&
            !data.m_CottageAsyncState->m_bCancelled.Load())
        {
            auto state = data.m_CottageAsyncState;
            data.m_CottageAsyncState.reset(); // 一度だけ消費
            data.m_CottageLoadRequestId = 0;

            auto &megaGeometry = ctx.RenderResourcesRef.MegaGeometry();
            bool bCottageReady = false;
            if (state->m_bLoaded)
            {
                const auto megaMeshHandle = megaGeometry.GetModelMegaMeshHandle(state->m_Handle);
                bCottageReady = megaMeshHandle.IsValid();
                if (!bCottageReady)
                {
                    megaGeometry.ReleaseModel(state->m_Handle);
                }
            }
            if (!bCottageReady)
            {
                NORVES_LOG_ERROR("Rendering3DTest", "小屋のモデルのロードに失敗しました");
            }
            else
            {
                const auto megaMeshHandle = megaGeometry.GetModelMegaMeshHandle(state->m_Handle);
                auto &world = ctx.WorldRef;
                data.m_CottageModelHandle = state->m_Handle;
                // 小屋（幅約12.4 m・奥行き約14.7 m・高さ約6.8 m）は球の奥に置き、既定の視点で
                // 手前の展示物の上に見えるようにする。モデルの最下点はY=-0.016。
                data.m_pCottageObject = world.SpawnObject<Entity>();
                ctx.ScopeRef.TrackObject(data.m_pCottageObject);
                data.m_pCottageObject->SetPosition(0.0f, -0.984f, -18.0f);
                data.m_pCottageMegaGeometryComponent =
                    world.CreateComponent<Component::MegaGeometryComponent>(data.m_pCottageObject);
                data.m_pCottageMegaGeometryComponent->SetMegaMeshHandle(megaMeshHandle);
                data.m_pCottageMegaGeometryComponent->SetCastShadow(true);
                // 消費したモデルはスコープに解放を委ねる。
                ctx.ScopeRef.TrackModel(data.m_CottageModelHandle);
                NORVES_LOG_INFO("Rendering3DTest", "Cottage model loaded and added to World");
            }
        }

        // 地面の外周のスキャン資産: 読み込みが終わったものから World へ置く。
        PlaceFinishedScanProps(ctx, data);

        // 大きな球: 石畳のテクスチャがそろったら高ポリのMegaGeometryを作り、仮の球と差し替える。
        if (data.m_pBigSphereMegaData && data.m_CobbleStoneMaterialUpdate &&
            data.m_CobbleStoneMaterialUpdate->PendingTextureCount == 0)
        {
            // 頂点を作るジョブがまだなら待つ（テクスチャがそろった直後に差し替え、撮影の数え始めと揃える）。
            if (data.m_BigSphereBuildTask)
            {
                const auto waitStartTime = std::chrono::steady_clock::now();
                const bool bWasCompleted = data.m_BigSphereBuildTask->IsCompleted();
                if (!bWasCompleted)
                {
                    data.m_BigSphereBuildTask->Wait();
                }
                NORVES_LOG_INFO("AssetLoadProfile",
                                "stage=big_sphere_build_wait completed_before_wait=%d wait_ms=%.1f",
                                bWasCompleted ? 1 : 0,
                                ElapsedMilliseconds(waitStartTime));
                data.m_BigSphereBuildTask.reset();
            }
            CreateBigSphereMegaGeometry(ctx, data);
        }

        // 球体をY軸回転させる
        if (data.m_pSphereObject && (!data.m_M9WorldAcceptance || !data.m_M9WorldAcceptance->bRequested))
        {
            static const bool bSphereSpin = ReadStartupSphereSpinEnabled();
            // 決定的な撮影では、自転を壁時計ではなく、読み込み完了の時点から数えた固定刻みの時間に従わせる。
            const auto &deterministicCapture = ctx.EngineRef.GetDeterministicCapture();
            const float spinSeconds = deterministicCapture.IsEnabled()
                                          ? static_cast<float>(deterministicCapture.GetEpochSeconds())
                                          : data.m_ElapsedTime;
            float angle = bSphereSpin ? spinSeconds * data.m_RotationSpeed : 0.0f;
            NorvesLib::Math::Vector3 yAxis(0.0f, 1.0f, 0.0f);
            NorvesLib::Math::Quaternion rotation(yAxis, angle);
            data.m_pSphereObject->SetRotation(rotation);
        }

        // 決定的な撮影は、組み立てが終わった時点（この Tick の大きな球の生成まで含む）から数え直す。
        if (ctx.EngineRef.GetDeterministicCapture().IsEnabled())
        {
            UpdateVirtualTextureSettle(ctx, data);
            ctx.EngineRef.GetDeterministicCapture().SetSceneReady(IsStartupSceneAssembled(data));
        }

    }

    void Rendering3DTestRoutine::Leave(GameModeContext &ctx, Rendering3DTestData &data, GameModeExitReason reason)
    {
        (void)reason;

        if (data.m_bPhysicsSmoke)
        {
            data.m_M8MinimalPhysicsSmoke.Leave(ctx);
        }

        LOG_INFO("=================================================");
        LOG_INFO("3Dレンダリングテスト終了");
        LOG_INFO("=================================================");
        const uint64_t finalRenderedFrameCount = ctx.EngineRef.GetRenderWorld().GetRenderedFrameCount();
        if (!data.m_bCameraSmokeCompleteEmitted)
        {
            LOG_INFO(
                "CAMERA_COMPONENT_SMOKE stage=complete snapshot=1 live_pointer=0 rendered=%llu",
                static_cast<unsigned long long>(finalRenderedFrameCount));
            EmitM9WorldSmokeMarker(
                "CAMERA_COMPONENT_SMOKE stage=complete snapshot=1 live_pointer=0 rendered=%llu",
                static_cast<unsigned long long>(finalRenderedFrameCount));
            data.m_bCameraSmokeCompleteEmitted = true;
        }
        LOG_INFO(
            "CAMERA_COMPONENT_SMOKE stage=exit rendered=%llu",
            static_cast<unsigned long long>(finalRenderedFrameCount));
        EmitM9WorldSmokeMarker(
            "CAMERA_COMPONENT_SMOKE stage=exit rendered=%llu",
            static_cast<unsigned long long>(finalRenderedFrameCount));

        // failed Enter と通常 Leave が同じ冪等cleanupを使い、借用登録・module service・
        // Scope外texture・M9 snapshot memberを先に閉じる。
        CleanupM9WorldAcceptance(ctx, data);

        // 1) 非同期ロードの後始末（World/RenderResources 生存中に行う）。
        //    GameModeScope::Cleanup は Leave 直後に呼ばれるため、ここでは
        //    スコープが追跡していない非同期由来のリソースだけを閉じる。
        if (data.m_BoulderAsyncState)
        {
            // 以降到着する callback の結果を破棄する。callback は asyncState を
            // 共有所有しているため、ここで reset しても use-after-free にならない。
            auto state = data.m_BoulderAsyncState;
            data.m_BoulderAsyncState->m_bCancelled.Store(true);
            data.m_BoulderAsyncState.reset();

            // Flush がモデルを finalize し callback を発火したが、Tick がまだ
            // boulder として消費（＝スコープ追跡）していない隙間。ここで解放
            // しないとモデルがリークする。
            if (state->m_bCompleted.Load() && state->m_bLoaded && state->m_Handle.IsValid())
            {
                ctx.RenderResourcesRef.MegaGeometry().ReleaseModel(state->m_Handle);
            }
        }

        // まだ in-flight なロードはキャンセルする（Flush が finalize をスキップ
        // するためリークしない）。
        if (data.m_BoulderLoadRequestId != 0)
        {
            if (data.m_bUseCookedModel)
            {
                ctx.RenderResourcesRef.MegaGeometry().CancelModelLoad(data.m_BoulderLoadRequestId);
            }
            else
            {
                Resource::GLTFAnalyzer::CancelModelLoad(data.m_BoulderLoadRequestId);
            }
            data.m_BoulderLoadRequestId = 0;
        }

        // 小屋の非同期ロードも岩と同じ手順で閉じる。
        if (data.m_CottageAsyncState)
        {
            auto state = data.m_CottageAsyncState;
            data.m_CottageAsyncState->m_bCancelled.Store(true);
            data.m_CottageAsyncState.reset();
            if (state->m_bCompleted.Load() && state->m_bLoaded && state->m_Handle.IsValid())
            {
                ctx.RenderResourcesRef.MegaGeometry().ReleaseModel(state->m_Handle);
            }
        }
        if (data.m_CottageLoadRequestId != 0)
        {
            Resource::GLTFAnalyzer::CancelModelLoad(data.m_CottageLoadRequestId);
            data.m_CottageLoadRequestId = 0;
        }

        // 地面の外周のスキャン資産の読み込みも閉じる（置く前に完了していたモデルは解放する）。
        for (StartupScanPropLoad &load : data.m_ScanPropLoads)
        {
            if (!load.State)
            {
                continue;
            }
            load.State->m_bCancelled.Store(true);
            if (load.State->m_bCompleted.Load() && load.State->m_bLoaded && load.State->m_Handle.IsValid())
            {
                ctx.RenderResourcesRef.MegaGeometry().ReleaseModel(load.State->m_Handle);
            }
        }
        data.m_ScanPropLoads.clear();

        // 2) 追跡済みリソース（球体/地面/光源球体/ディレクショナル/placeholder/
        //    boulder の各オブジェクト・3 メッシュ・boulder モデル）の解放は
        //    GameModeScope::Cleanup（Leave 直後に StateMachine が呼ぶ）が
        //    正しい順序で行う。ここでは手動解放しない。
        // 3) 再 Enter（Change 往復）に備えてキャッシュをクリアする。
        //    スコープは独自の追跡リストを使うため、ここでの null 化は安全。
        data.m_pSphereObject = nullptr;
        data.m_pSphereMeshComponent = nullptr;
        data.m_pSphereMegaGeometryComponent = nullptr;
        data.m_pBigSphereMegaData.reset();
        data.m_pBigSphereCooked.reset();
        // 走行中のジョブは自分の参照で球のデータを持ち続けるので、ここでは待たずに手放す。
        data.m_BigSphereBuildTask.reset();
        data.m_CobbleStoneMaterialUpdate.reset();
        data.m_BigSphereModelHandle = ModelHandle::Invalid();
        data.m_pGroundObject = nullptr;
        data.m_pGroundMeshComponent = nullptr;
        data.m_GroundPieces.clear();
        data.m_GroundSwatchMaterials.clear();
        data.m_StressMaterials.clear();
        data.m_pLightSphereObject = nullptr;
        data.m_pLightSphereMeshComponent = nullptr;
        data.m_pPointLightComponent = nullptr;
        data.m_pBoulderPlaceholderObject = nullptr;
        data.m_pBoulderPlaceholderMeshComponent = nullptr;
        data.m_pBoulderObject = nullptr;
        data.m_pBoulderMegaGeometryComponent = nullptr;
        data.m_pCottageObject = nullptr;
        data.m_pCottageMegaGeometryComponent = nullptr;
        data.m_CottageModelHandle = ModelHandle::Invalid();
        data.m_ShowcaseSphereObjects.clear();
        data.m_ShowcaseMaterials.clear();
        // 空はRenderWorldの設定なので、ほかのモードへ持ち越さないよう無効へ戻す。
        data.m_SkyAtmosphere = MakeDefaultSkyAtmosphereParameters();
        ctx.EngineRef.GetRenderWorld().SetSkyAtmosphere(data.m_SkyAtmosphere);
        ctx.EngineRef.GetRenderWorld().SetVolumetricFogParameters(MakeDefaultVolumetricFogParameters());
        ctx.EngineRef.GetRenderWorld().SetStaticEnvironmentIntensityScale(1.0f);
        data.m_LightController.SetTargetLight(nullptr);
        data.m_F4BoardObjects.clear();
        data.m_F4BoardComponents.clear();
        data.m_F9BillboardObjects.clear();
        data.m_F9BillboardComponents.clear();
        data.m_F11ImpostorSmokeObjects.clear();
        data.m_F11ImpostorSourceMeshComponents.clear();
        data.m_F11ImpostorComponents.clear();

        data.m_bMeshesRegistered = false;
        data.m_bBoulderModelLoaded = false;
        data.m_BoulderModelHandle = ModelHandle::Invalid();
        data.m_bBoulderModelLoadPending = false;

        // 非同期マテリアル更新のクリア
        data.m_PendingMaterialUpdates.clear();
        data.m_CookedStartupModelLoads.clear();

        if (data.m_ParticleEmitter.IsValid())
        {
            ctx.EngineRef.GetParticleSystem().DestroyEmitter(data.m_ParticleEmitter);
            data.m_ParticleEmitter = {};
        }
    }

} // namespace Game::GameModes

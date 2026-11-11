#include "GameModes/Sandbox/SandboxMode.h"

#include "Core/Public/Animation/SkeletalAnimationSampler.h"
#include "Core/Public/Animation/SkeletalAssetResource.h"
#include "Core/Public/Animation/SkeletalPoseBounds.h"
#include "Core/Public/Component/CameraComponent.h"
#include "Core/Public/Component/MeshComponent.h"
#include "Core/Public/Component/SkinnedMeshComponent.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/Engine/NorvesEngine.h"
#include "Core/Public/GameMode/GameModeScope.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Platform/PlatformFile.h"
#include "Core/Public/Rendering/MaterialTypes.h"
#include "Core/Public/Rendering/ProceduralMeshGenerator.h"
#include "Core/Public/Rendering/RenderResources.h"
#include "Core/Public/Rendering/RenderWorld.h"
#include "Core/Public/Rendering/SkyAtmosphere.h"
#include "Core/Public/Rendering/VolumetricFog.h"
#include "Core/Public/Resource/GLTFAnalyzer.h"
#include "GameModes/Rendering3DTest/SkySunControl.h"
#include "Scenes/SceneCatalog.h"
#include "Scenes/SceneReadiness.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <system_error>
#include <utility>

#include <Windows.h>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::GameMode;
using namespace NorvesLib::Core::Rendering;

namespace Game::GameModes
{

    namespace
    {
        namespace Math = NorvesLib::Math;

        // 空の太陽は仰角 40°・方位 30°（Rendering3DTest の既定と同じ）
        constexpr float kSunAltitudeDegrees = 40.0f;
        constexpr float kSunAzimuthDegrees = 30.0f;
        // 晴天の昼の手動露出（絞り f/16・ISO 100 のままシャッター速度で合わせる）。
        constexpr float kExposureEV100 = 15.0f;
        // SceneReady を出すまでの Tick の数。決定的な撮影は「読み込み中」を一度観測してから数え始めるので、
        // 最初のフレームは組み立て中として見せる。
        constexpr uint32_t kReadyTickCount = 3;

        // 地面: 一辺 400 m の平面。格子の模様は 8 m ごとに繰り返す（テクスチャ 1 枚 = 8 m 四方、1 升 = 1 m）
        constexpr float kGroundSize = 400.0f;
        constexpr float kTileMeters = 8.0f;
        constexpr uint32_t kTextureSize = 512;
        constexpr uint32_t kCellsPerTile = 8;
        // 他のシーンのメッシュの ID（100〜、200〜）と重ならない値
        constexpr uint32_t kGroundMeshHandleId = 900;

        void SetPixel(NorvesLib::Core::Container::VariableArray<uint8_t> &pixels, uint32_t x, uint32_t y, uint8_t r,
                      uint8_t g, uint8_t b)
        {
            const uint32_t index = (y * kTextureSize + x) * 4;
            pixels[index + 0] = r;
            pixels[index + 1] = g;
            pixels[index + 2] = b;
            pixels[index + 3] = 255;
        }

        /// 8×8 升の格子。市松の明暗の上に、x=0 の升の列を赤、z=0 の升の行を青、両方が重なる升を黄で示す
        TextureHandle CreateGridTexture(GameModeContext &ctx)
        {
            NorvesLib::Core::Container::VariableArray<uint8_t> pixels(kTextureSize * kTextureSize * 4);
            constexpr uint32_t kCellPixels = kTextureSize / kCellsPerTile;
            for (uint32_t y = 0; y < kTextureSize; ++y)
            {
                for (uint32_t x = 0; x < kTextureSize; ++x)
                {
                    const uint32_t cellX = x / kCellPixels;
                    const uint32_t cellZ = y / kCellPixels;
                    const bool bLight = ((cellX + cellZ) % 2) == 0;
                    if (cellX == 0 && cellZ == 0)
                    {
                        SetPixel(pixels, x, y, 230, 200, 60);
                    }
                    else if (cellX == 0)
                    {
                        SetPixel(pixels, x, y, bLight ? 215 : 160, bLight ? 70 : 45, bLight ? 70 : 45);
                    }
                    else if (cellZ == 0)
                    {
                        SetPixel(pixels, x, y, bLight ? 70 : 45, bLight ? 100 : 70, bLight ? 215 : 160);
                    }
                    else
                    {
                        const uint8_t value = bLight ? 190 : 120;
                        SetPixel(pixels, x, y, value, value, value);
                    }
                }
            }

            TextureCreateInfo info;
            info.Width = kTextureSize;
            info.Height = kTextureSize;
            info.PixelFormat = TextureCreateInfo::Format::RGBA8_SRGB;
            info.DebugName = "SandboxGridTexture";
            return ctx.RenderResourcesRef.Textures().CreateTexture(info, pixels.data(), pixels.size());
        }

        bool CreateGround(GameModeContext &ctx)
        {
            const TextureHandle grid = CreateGridTexture(ctx);
            if (!grid.IsValid())
            {
                LOG_ERROR("sandbox: 地面の格子のテクスチャを作れませんでした");
                return false;
            }
            ctx.ScopeRef.TrackTexture(grid);

            MaterialCreateData materialData;
            materialData.DebugName = "SandboxGround";
            materialData.AlbedoTexture = grid;
            materialData.Metallic = 0.0f;
            materialData.Roughness = 0.9f;
            const MaterialHandle material = ctx.RenderResourcesRef.Materials().Create(materialData);
            if (!material.IsValid())
            {
                LOG_ERROR("sandbox: 地面の材質を作れませんでした");
                return false;
            }
            ctx.ScopeRef.TrackMaterial(material);

            NorvesLib::Core::Container::VariableArray<Mesh3DVertex> vertices;
            NorvesLib::Core::Container::VariableArray<uint32_t> indices;
            ProceduralMeshGenerator::GeneratePlane(kGroundSize, kGroundSize, 1, 1, vertices, indices);
            for (Mesh3DVertex &vertex : vertices)
            {
                // 世界の x・z を 8 m で割った値。原点の升がテクスチャの左上（赤・青・黄の印の升）に来る
                vertex.TexCoord[0] = vertex.Position[0] / kTileMeters;
                vertex.TexCoord[1] = vertex.Position[2] / kTileMeters;
            }
            const MeshDataHandle mesh{kGroundMeshHandleId};
            if (!ctx.RenderResourcesRef.Meshes().Register(mesh, vertices.data(),
                                                          static_cast<uint32_t>(vertices.size() * sizeof(Mesh3DVertex)),
                                                          indices.data(), static_cast<uint32_t>(indices.size())))
            {
                LOG_ERROR("sandbox: 地面のメッシュを登録できませんでした");
                return false;
            }
            ctx.ScopeRef.TrackMesh(mesh);

            Entity *ground = ctx.ScopeRef.SpawnObject<Entity>();
            if (ground == nullptr)
            {
                return false;
            }
            ground->SetPosition(0.0f, 0.0f, 0.0f);
            auto *meshComponent = ctx.WorldRef.CreateComponent<Component::MeshComponent>(ground);
            if (meshComponent == nullptr)
            {
                return false;
            }
            meshComponent->SetMeshHandle(mesh);
            meshComponent->SetMaterial(0, material);
            meshComponent->SetCastShadow(false);
            return true;
        }

        // M9 の試験で生成する fixture.bin と同じオフセット・値を little-endian で書く。
        Container::VariableArray<uint8_t> MakeSkinnedCrowdFixture()
        {
            Container::VariableArray<uint8_t> bytes(416, uint8_t{0});
            const auto writeFloat = [&bytes](size_t offset, float value)
            {
                const uint32_t bits = std::bit_cast<uint32_t>(value);
                for (size_t index = 0; index < 4; ++index)
                {
                    bytes[offset + index] = static_cast<uint8_t>(bits >> (index * 8));
                }
            };
            const auto writeU16 = [&bytes](size_t offset, uint16_t value)
            {
                bytes[offset] = static_cast<uint8_t>(value);
                bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
            };

            constexpr float positions[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
            constexpr float normals[] = {0, 0, 1, 0, 0, 1, 0, 0, 1};
            constexpr float texCoords[] = {0, 0, 1, 0, 0, 1};
            constexpr uint8_t joints[] = {0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0};
            constexpr float weights[] = {0.75f, 0.25f, 0, 0, 0.5f, 0.5f, 0, 0, 1, 0, 0, 0};
            constexpr float translations[] = {0, 1, 0, 0, 3, 0};
            constexpr float rotations[] = {0, 0, 0, 1, 0, 0, 1, 0};

            for (size_t index = 0; index < 9; ++index)
            {
                writeFloat(index * 4, positions[index]);
                writeFloat(36 + index * 4, normals[index]);
            }
            for (size_t index = 0; index < 6; ++index)
            {
                writeFloat(72 + index * 4, texCoords[index]);
                writeFloat(360 + index * 4, translations[index]);
            }
            for (size_t index = 0; index < 12; ++index)
            {
                bytes[96 + index] = joints[index];
                writeFloat(132 + index * 4, weights[index]);
            }
            writeU16(216, 0);
            writeU16(218, 1);
            writeU16(220, 2);
            for (size_t index = 0; index < 4; ++index)
            {
                writeFloat(224 + index * 5 * 4, 1.0f);
                writeFloat(288 + index * 5 * 4, 1.0f);
            }
            writeFloat(288 + 13 * 4, -1.0f);
            writeFloat(352, 0.0f);
            writeFloat(356, 2.0f);
            for (size_t index = 0; index < 8; ++index)
            {
                writeFloat(384 + index * 4, rotations[index]);
            }
            return bytes;
        }

        // 作成に成功したディレクトリだけを所有し、途中で失敗しても後片付けする。
        struct TemporarySkinnedDirectory
        {
            std::filesystem::path Path;

            bool Create()
            {
                std::error_code error;
                auto root = std::filesystem::temp_directory_path(error);
                if (error)
                {
                    return false;
                }
                root = std::filesystem::absolute(root, error);
                if (error)
                {
                    return false;
                }
                for (uint32_t suffix = 0; suffix < 256; ++suffix)
                {
                    wchar_t name[96]{};
                    if (std::swprintf(name, 96, L"NorvesSandboxSkinned-%lu-%u",
                                      static_cast<unsigned long>(GetCurrentProcessId()), suffix) < 0)
                    {
                        return false;
                    }
                    auto candidate = root / name;
                    if (std::filesystem::create_directory(candidate, error))
                    {
                        Path = std::move(candidate);
                        return true;
                    }
                    if (error && error != std::errc::file_exists)
                    {
                        return false;
                    }
                    error.clear();
                }
                return false;
            }

            ~TemporarySkinnedDirectory()
            {
                if (!Path.empty())
                {
                    std::error_code error;
                    std::filesystem::remove_all(Path, error);
                    if (error)
                    {
                        LOG_WARNING("sandbox: 一時ディレクトリを削除できませんでした（PID=%lu、エラー=%d）",
                                    static_cast<unsigned long>(GetCurrentProcessId()), error.value());
                    }
                }
            }
        };

        // Platform の入出力は UTF-8、解析器は Windows の通常のマルチバイトパスを受け取る。
        bool EncodeSkinnedPath(const wchar_t* path, UINT codePage, Container::String& outPath)
        {
            const bool bUtf8 = codePage == CP_UTF8;
            const DWORD flags = bUtf8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
            BOOL bUsedDefault = FALSE;
            BOOL* pUsedDefault = bUtf8 ? nullptr : &bUsedDefault;
            const int size = WideCharToMultiByte(codePage, flags, path, -1, nullptr, 0, nullptr, pUsedDefault);
            if (size <= 0 || bUsedDefault)
            {
                return false;
            }
            Container::VariableArray<char> bytes(static_cast<size_t>(size));
            if (WideCharToMultiByte(codePage, flags, path, -1, bytes.data(), size, nullptr, pUsedDefault) != size ||
                bUsedDefault)
            {
                return false;
            }
            outPath = Container::String(bytes.data());
            return true;
        }

        bool MakeSkinnedAnalyzerPath(const std::filesystem::path& directory, Container::String& outPath)
        {
            if (!EncodeSkinnedPath(directory.c_str(), GetACP(), outPath))
            {
                // 文字コードに収まらないディレクトリは、利用可能なら短縮名を使う。
                const DWORD size = GetShortPathNameW(directory.c_str(), nullptr, 0);
                if (size == 0)
                {
                    return false;
                }
                Container::VariableArray<wchar_t> shortPath(size);
                const DWORD length = GetShortPathNameW(directory.c_str(), shortPath.data(), size);
                if (length == 0 || length >= size || !EncodeSkinnedPath(shortPath.data(), GetACP(), outPath))
                {
                    return false;
                }
            }
            outPath += "\\ValidU8Float.gltf";
            return true;
        }

        // 共有資産と時刻 0 の境界を先に作り、読み込み失敗時は Entity を作らない。
        bool LoadSkinnedCrowdAsset(SandboxData &data, Math::Vector3 &outOffset)
        {
            Container::String sourcePath("Assets/Models/M9Skinned/ValidU8Float.gltf");
#if defined(NORVES_SOURCE_ASSET_DIR)
            sourcePath = Container::String(NORVES_SOURCE_ASSET_DIR) + "/Models/M9Skinned/ValidU8Float.gltf";
#endif
            Container::VariableArray<uint8_t> sourceBytes;
            if (!Platform::ReadFileBytes(sourcePath, sourceBytes).bSuccess)
            {
                return false;
            }

            Skeletal::SkeletalGltfDecodeResult decoded;
            {
                TemporarySkinnedDirectory temporary;
                if (!temporary.Create())
                {
                    return false;
                }
                Container::String directory;
                if (!EncodeSkinnedPath(temporary.Path.c_str(), CP_UTF8, directory))
                {
                    return false;
                }
                const Container::String gltfPath = directory + "\\ValidU8Float.gltf";
                const Container::String bufferPath = directory + "\\fixture.bin";
                const auto fixture = MakeSkinnedCrowdFixture();
                if (!Platform::WriteFileAtomic(gltfPath, sourceBytes, false).bSuccess ||
                    !Platform::WriteFileAtomic(bufferPath, fixture, false).bSuccess)
                {
                    return false;
                }
                Container::String analyzerPath;
                if (!MakeSkinnedAnalyzerPath(temporary.Path, analyzerPath))
                {
                    return false;
                }
                decoded = ResourceIO::GLTFAnalyzer::AnalyzeSkeletal(analyzerPath);
            }
            if (!decoded.Succeeded() || decoded.Data.Clips.empty())
            {
                return false;
            }

            auto &registry = GEngine.GetResourceRegistry();
            auto mesh = registry.CreateTransient<SkinnedMeshResource>(TEXT("SandboxCrowdMesh"));
            auto skeleton = registry.CreateTransient<SkeletonResource>(TEXT("SandboxCrowdSkeleton"));
            auto clip = registry.CreateTransient<AnimationClipResource>(TEXT("SandboxCrowdClip"));
            auto asset = registry.CreateTransient<SkeletalAssetResource>(TEXT("SandboxCrowdAsset"));
            if (!mesh || !skeleton || !clip || !asset)
            {
                return false;
            }
            mesh->SetVertices(std::move(decoded.Data.Vertices));
            mesh->SetIndices(std::move(decoded.Data.Indices));
            mesh->SetSubmeshTables(std::move(decoded.Data.SubMeshes), std::move(decoded.Data.MaterialSlots));
            mesh->SetMeshNodeGlobalTransform(decoded.Data.MeshNodeGlobalTransform);
            skeleton->SetJoints(std::move(decoded.Data.Joints));
            clip->SetClip(std::move(decoded.Data.Clips[0]));
            if (!mesh->Load() || !skeleton->Load() || !clip->Load() ||
                !std::isfinite(clip->GetClip().DurationSeconds) || clip->GetClip().DurationSeconds <= 0.0f)
            {
                return false;
            }
            asset->SetResources(mesh, skeleton, clip);
            if (!asset->IsLoaded() || !asset->IsValid())
            {
                return false;
            }

            // glTF のメッシュノードの変換を含む実寸を使い、格子点が足元の中心に来るようにする。
            const auto &values = mesh->GetMeshNodeGlobalTransform();
            const Math::Matrix4x4 meshTransform(
                values[0], values[1], values[2], values[3], values[4], values[5], values[6], values[7],
                values[8], values[9], values[10], values[11], values[12], values[13], values[14], values[15]);
            Animation::SkeletalPoseSnapshot pose;
            Math::AABB bounds;
            const auto &vertices = mesh->GetVertices();
            if (!Animation::SkeletalAnimationSampler::Sample(*skeleton, *clip, *mesh, 0.0f, meshTransform, pose) ||
                vertices.empty() || !Animation::ComputeExactBounds(
                    {vertices.data(), vertices.size()}, {pose.BonePalette.data(), pose.BonePalette.size()}, true, bounds))
            {
                return false;
            }
            const auto center = bounds.Center();
            outOffset = Math::Vector3(-center.x, -bounds.Min.y, -center.z);
            data.m_SkinnedCrowdAsset = asset;
            return true;
        }

        void CreateSkinnedCrowd(GameModeContext &ctx, SandboxData &data)
        {
            Math::Vector3 offset;
            if (!LoadSkinnedCrowdAsset(data, offset))
            {
                LOG_ERROR("sandbox: 合成スキンの資産を読み込めませんでした。配置を省略します");
                return;
            }

            MaterialCreateData materialData;
            materialData.DebugName = "SandboxCrowdMaterial";
            materialData.Metallic = 0.0f;
            materialData.Roughness = 0.7f;
            materialData.bTwoSided = true;
            const MaterialHandle material = ctx.RenderResourcesRef.Materials().Create(materialData);
            if (!material.IsValid())
            {
                LOG_ERROR("sandbox: 合成スキンの材質を作れませんでした。配置を省略します");
                return;
            }
            ctx.ScopeRef.TrackMaterial(material);

            constexpr float kSpacing = 1.5f;
            constexpr float kCenterZ = 3.0f;
            constexpr float kTimeStepSeconds = 0.137f;
            const uint32_t columns = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(data.m_SkinnedCrowdCount))));
            const uint32_t rows = (data.m_SkinnedCrowdCount + columns - 1u) / columns;
            const float startX = -0.5f * static_cast<float>(columns - 1u) * kSpacing;
            const float startZ = kCenterZ - 0.5f * static_cast<float>(rows - 1u) * kSpacing;
            const auto clip = data.m_SkinnedCrowdAsset->GetAnimationClip();
            const float duration = clip->GetClip().DurationSeconds;
            for (uint32_t i = 0; i < data.m_SkinnedCrowdCount; ++i)
            {
                Entity *entity = ctx.ScopeRef.SpawnObject<Entity>();
                if (entity == nullptr)
                {
                    LOG_ERROR("sandbox: 合成スキンの Entity を作れませんでした（作成済み %u 体）", i);
                    return;
                }
                auto *component = ctx.WorldRef.CreateComponent<Component::SkinnedMeshComponent>(entity);
                if (component == nullptr)
                {
                    LOG_ERROR("sandbox: 合成スキンの Component を作れませんでした（作成済み %u 体）", i);
                    return;
                }
                component->SetSkeletalAsset(data.m_SkinnedCrowdAsset);
                if (!component->SetAnimationClip(clip))
                {
                    LOG_ERROR("sandbox: 合成スキンのクリップを設定できませんでした（作成済み %u 体）", i);
                    return;
                }
                entity->SetPosition(startX + static_cast<float>(i % columns) * kSpacing + offset.x, offset.y,
                                    startZ + static_cast<float>(i / columns) * kSpacing + offset.z);
                component->SetMaterial(material);
                component->SetLooping(true);
                component->SetAnimationTimeSeconds(std::fmod(static_cast<float>(i) * kTimeStepSeconds, duration));
                component->SetPlaying(true);
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
    } // namespace

    GameModeEnterResult SandboxRoutine::Enter(GameModeContext &ctx, SandboxData &data)
    {
        LOG_INFO("sandbox を開始します");
        data.m_TickCount = 0;
        Game::Scenes::BeginSceneAssembly(ctx.EngineRef);

        const Game::Scenes::SceneDescriptor *scene = Game::Scenes::FindScene(Game::Scenes::kSandboxSceneId);
        Game::Scenes::FreeCameraPose pose{};
        if (scene == nullptr || !Game::Scenes::ResolveFreeCameraPose(*scene, data.m_ViewName.c_str(), pose))
        {
            LOG_ERROR("sandbox: シーンの一覧に視点の定義がありません");
            Game::Scenes::EndSceneAssembly(ctx.EngineRef);
            return GameModeEnterResult::Failed;
        }

        ApplyEnvironment(ctx);
        if (!CreateGround(ctx))
        {
            ResetEnvironment(ctx);
            Game::Scenes::EndSceneAssembly(ctx.EngineRef);
            return GameModeEnterResult::Failed;
        }
        data.m_Rig.ConfigurePath(scene->CameraPath, data.m_CameraPathTime, data.m_bCameraPathPlay);
        if (!data.m_Rig.Enter(ctx, pose, data.m_CameraScriptPath.c_str()))
        {
            LOG_ERROR("sandbox: カメラを作れませんでした");
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
        if (data.m_SkinnedCrowdCount > 0)
        {
            CreateSkinnedCrowd(ctx, data);
        }
        if (data.m_ParallelPose != -1 || data.m_ParallelPoseMinCount != 0 || data.m_ParallelPoseGrain != 0)
        {
            data.m_PreviousPoseParallelSettings = ctx.WorldRef.GetPoseEvaluationParallelSettings();
            data.m_bRestorePoseParallelSettings = true;
            auto settings = data.m_PreviousPoseParallelSettings;
            if (data.m_ParallelPose != -1)
            {
                settings.bEnabled = data.m_ParallelPose == 1;
            }
            if (data.m_ParallelPoseMinCount != 0)
            {
                settings.MinBatchCount = data.m_ParallelPoseMinCount;
            }
            if (data.m_ParallelPoseGrain != 0)
            {
                settings.Grain = data.m_ParallelPoseGrain;
            }
            ctx.WorldRef.SetPoseEvaluationParallelSettings(settings);
            LOG_INFO("sandbox: 姿勢の並列評価を設定しました（有効=%u、最小数=%u、取り出し数=%u）",
                     settings.bEnabled ? 1u : 0u, settings.MinBatchCount, settings.Grain);
        }
        return GameModeEnterResult::Succeeded;
    }

    void SandboxRoutine::Tick(GameModeContext &ctx, SandboxData &data, float deltaTime)
    {
        ++data.m_TickCount;
        // 撮影の視点の切り替えは、このフレームのカメラを渡す前に置き直す
        if (const Game::Scenes::SceneDescriptor *scene = Game::Scenes::FindScene(Game::Scenes::kSandboxSceneId))
        {
            data.m_Rig.ApplyRequestedView(data.m_pViewSwitch, *scene);
        }
        const bool bSuspended = data.m_pPauseMenu != nullptr && data.m_pPauseMenu->IsOpen();
        data.m_Rig.Tick(ctx, deltaTime, bSuspended);
        // 台本を流し終えるまで、決定的な撮影を待たせる（撮影は台本の後のカメラで撮る）
        const bool bReady = data.m_TickCount >= kReadyTickCount && !data.m_Rig.IsScriptRunning();
        if (bReady)
        {
            data.m_Rig.NotifySceneReady();
        }
        Game::Scenes::UpdateSceneAssembly(ctx.EngineRef, Game::Scenes::kSandboxSceneId, bReady);
    }

    void SandboxRoutine::Leave(GameModeContext &ctx, SandboxData &data, GameModeExitReason)
    {
        Game::Scenes::EndSceneAssembly(ctx.EngineRef);
        data.m_Rig.Leave(ctx);
        data.m_SkinnedCrowdAsset.reset();
        if (data.m_bRestorePoseParallelSettings)
        {
            ctx.WorldRef.SetPoseEvaluationParallelSettings(data.m_PreviousPoseParallelSettings);
            data.m_bRestorePoseParallelSettings = false;
        }
        // 地面・材質・テクスチャ・カメラの実体は GameModeScope::Cleanup が返す。空は RenderWorld の設定なので戻す。
        ResetEnvironment(ctx);
        LOG_INFO("sandbox を終了します");
    }

} // namespace Game::GameModes

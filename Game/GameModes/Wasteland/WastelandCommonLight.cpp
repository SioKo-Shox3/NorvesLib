#include "GameModes/Wasteland/WastelandCommonLight.h"

#include "Core/Public/Component/DirectionalLightComponent.h"
#include "Core/Public/Component/LightIntensityUnit.h"
#include "Core/Public/Component/MeshComponent.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/GameMode/GameModeContext.h"
#include "Core/Public/GameMode/GameModeScope.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Rendering/ProceduralMeshGenerator.h"
#include "Core/Public/Rendering/RenderResources.h"
#include "Core/Public/Rendering/RenderWorld.h"
#include "Core/Public/Rendering/RenderingCoordinator.h"
#include "Core/Public/Rendering/SkyAtmosphere.h"
#include "Core/Public/Rendering/Terrain/TerrainHeightField.h"
#include "Core/Public/Rendering/VolumetricFog.h"
#include "Core/Public/Text/JsonDocument.h"
#include "Scenes/SceneCatalog.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <utility>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Container;
using namespace NorvesLib::Core::GameMode;
using namespace NorvesLib::Core::Rendering;

namespace Game::GameModes::WastelandCommonLight
{
    namespace
    {
        // 共通の照明の定義（リポジトリの Scripts/Terrain。照合の台本 ReferenceCheck.py --plate も同じファイルを読む）
        constexpr const char *kDefinitionRelativePath = "Scripts/Terrain/CommonLight.json";
        // 板のメッシュの ID（ほかのシーンの 100〜、200〜、900、910、911、920 と重ならない値）
        constexpr uint32_t kPlateMeshHandleId = 930;
        constexpr float kDegreesToRadians = 3.14159265358979323846f / 180.0f;

        String DefinitionPath()
        {
#if defined(NORVES_SOURCE_ASSET_DIR)
            // 作業ディレクトリは実行ファイルのディレクトリのことがあるので、リポジトリの Assets の隣を基準に探す
            return String(NORVES_SOURCE_ASSET_DIR) + "/../" + kDefinitionRelativePath;
#else
            return String(kDefinitionRelativePath);
#endif
        }

        bool ReadFileBytes(const String &path, VariableArray<uint8_t> &outBytes)
        {
            std::ifstream stream(path.c_str(), std::ios::binary | std::ios::ate);
            if (!stream)
            {
                return false;
            }
            const std::streamoff size = stream.tellg();
            if (size <= 0)
            {
                return false;
            }
            outBytes.resize(static_cast<size_t>(size));
            stream.seekg(0, std::ios::beg);
            stream.read(reinterpret_cast<char *>(outBytes.data()), size);
            return static_cast<bool>(stream);
        }

        /// 0 以上の有限の数（無い・負・非有限なら false）
        bool ReadNonNegative(const JsonValue &object, const char *key, float &outValue)
        {
            const JsonValue value = object.FindMember(key);
            if (!value.IsNumber())
            {
                return false;
            }
            const double number = value.AsNumber();
            if (!std::isfinite(number) || number < 0.0)
            {
                return false;
            }
            outValue = static_cast<float>(number);
            return true;
        }

        bool ReadSun(const JsonValue &object, float &outAltitude, float &outAzimuth)
        {
            const JsonValue altitude = object.FindMember("altitude_degrees");
            const JsonValue azimuth = object.FindMember("azimuth_degrees");
            if (!altitude.IsNumber() || !azimuth.IsNumber())
            {
                return false;
            }
            outAltitude = static_cast<float>(altitude.AsNumber());
            outAzimuth = static_cast<float>(azimuth.AsNumber());
            return std::isfinite(outAltitude) && std::isfinite(outAzimuth) && outAltitude > 0.0f &&
                   outAltitude <= 90.0f;
        }

        const char *CaseName(LightCase lightCase)
        {
            switch (lightCase)
            {
            case LightCase::SunOnly:
                return "sun";
            case LightCase::SkyOnly:
                return "sky";
            case LightCase::Both:
            default:
                return "both";
            }
        }

        /// 灰色の板の材質（鏡面の無い純ランバート）と、一辺の平面のメッシュを用意し、影を落とさない実体として置く
        bool CreatePlate(GameModeContext &ctx, State &state)
        {
            MaterialCreateData material;
            material.DebugName = "WastelandGrayPlate";
            material.BaseColor[0] = state.Def.PlateAlbedo;
            material.BaseColor[1] = state.Def.PlateAlbedo;
            material.BaseColor[2] = state.Def.PlateAlbedo;
            material.BaseColor[3] = 1.0f;
            material.Metallic = 0.0f;
            material.Roughness = state.Def.PlateRoughness;
            material.HeightScale = 0.0f;
            material.bCastShadows = false;
            material.bDiffuseOnly = true;
            state.PlateMaterial = ctx.RenderResourcesRef.Materials().Create(material);
            if (!state.PlateMaterial.IsValid())
            {
                LOG_ERROR("wasteland: 灰色の板の材質を作れませんでした");
                return false;
            }
            ctx.ScopeRef.TrackMaterial(state.PlateMaterial);

            VariableArray<Mesh3DVertex> vertices;
            VariableArray<uint32_t> indices;
            ProceduralMeshGenerator::GeneratePlane(state.Def.PlateSizeMeters, state.Def.PlateSizeMeters, 1, 1, vertices,
                                                   indices);
            const MeshDataHandle mesh{kPlateMeshHandleId};
            if (!ctx.RenderResourcesRef.Meshes().Register(mesh, vertices.data(),
                                                          static_cast<uint32_t>(vertices.size() * sizeof(Mesh3DVertex)),
                                                          indices.data(), static_cast<uint32_t>(indices.size())))
            {
                LOG_ERROR("wasteland: 灰色の板のメッシュを登録できませんでした");
                return false;
            }
            ctx.ScopeRef.TrackMesh(mesh);
            state.bPlateMeshRegistered = true;

            state.pPlate = ctx.ScopeRef.SpawnObject<Entity>();
            if (state.pPlate == nullptr)
            {
                return false;
            }
            auto *meshComponent = ctx.WorldRef.CreateComponent<Component::MeshComponent>(state.pPlate);
            if (meshComponent == nullptr)
            {
                return false;
            }
            meshComponent->SetMeshHandle(mesh);
            meshComponent->SetMaterial(0, state.PlateMaterial);
            meshComponent->SetCastShadow(false);
            return true;
        }
    } // namespace

    bool LoadDefinition(Definition &outDefinition, String &outError)
    {
        const String path = DefinitionPath();
        VariableArray<uint8_t> bytes;
        if (!ReadFileBytes(path, bytes))
        {
            outError = String("読めません: ") + path;
            return false;
        }
        JsonDocument document;
        String parseError;
        if (!JsonDocument::TryParseUtf8(Span<const uint8_t>(bytes.data(), bytes.size()), document, &parseError))
        {
            outError = path + ": " + parseError;
            return false;
        }
        const JsonValue root = document.GetRoot();
        if (!root.IsObject() || root.FindMember("version").AsNumber() != 1.0)
        {
            outError = path + ": version が 1 でありません";
            return false;
        }

        Definition definition;
        const JsonValue sky = root.FindMember("sky_radiance_rgb");
        if (!sky.IsArray() || sky.GetArraySize() != 3u)
        {
            outError = path + ": sky_radiance_rgb が 3 つの数の配列でありません";
            return false;
        }
        float skyRgb[3] = {};
        for (size_t channel = 0; channel < 3u; ++channel)
        {
            const JsonValue value = sky.GetArrayElement(channel);
            const double number = value.IsNumber() ? value.AsNumber() : -1.0;
            if (!std::isfinite(number) || number < 0.0)
            {
                outError = path + ": sky_radiance_rgb に負・非有限の値があります";
                return false;
            }
            skyRgb[channel] = static_cast<float>(number);
        }
        definition.SkyRadiance = NorvesLib::Math::Vector3(skyRgb[0], skyRgb[1], skyRgb[2]);
        if (!ReadNonNegative(root, "sun_illuminance_normal_lux", definition.SunIlluminanceNormalLux))
        {
            outError = path + ": sun_illuminance_normal_lux が 0 以上の数でありません";
            return false;
        }
        if (!ReadSun(root.FindMember("default_sun"), definition.DefaultSunAltitudeDegrees,
                     definition.DefaultSunAzimuthDegrees))
        {
            outError = path + ": default_sun の altitude_degrees（0 より大きく 90 以下）・azimuth_degrees がありません";
            return false;
        }
        const JsonValue views = root.FindMember("views");
        if (!views.IsArray())
        {
            outError = path + ": views が配列でありません";
            return false;
        }
        for (size_t index = 0; index < views.GetArraySize(); ++index)
        {
            const JsonValue item = views.GetArrayElement(index);
            const JsonValue name = item.FindMember("name");
            ViewSun sun;
            if (!item.IsObject() || !name.IsString() || !ReadSun(item, sun.AltitudeDegrees, sun.AzimuthDegrees))
            {
                outError = path + ": views の項目に name・altitude_degrees・azimuth_degrees が揃っていません";
                return false;
            }
            sun.ViewName = name.AsString();
            definition.Views.push_back(sun);
        }
        const JsonValue plate = root.FindMember("gray_plate");
        if (!plate.IsObject() || !ReadNonNegative(plate, "albedo", definition.PlateAlbedo) ||
            !ReadNonNegative(plate, "roughness", definition.PlateRoughness) ||
            !ReadNonNegative(plate, "size_m", definition.PlateSizeMeters) ||
            !ReadNonNegative(plate, "height_above_ground_m", definition.PlateHeightAboveGroundMeters) ||
            !ReadNonNegative(plate, "forward_m", definition.PlateForwardMeters) ||
            !ReadNonNegative(plate, "camera_height_above_plate_m", definition.CameraHeightAbovePlateMeters) ||
            definition.PlateAlbedo > 1.0f || definition.PlateRoughness > 1.0f || definition.PlateSizeMeters <= 0.0f ||
            definition.PlateForwardMeters <= 0.0f || definition.CameraHeightAbovePlateMeters <= 0.0f)
        {
            outError = path + ": gray_plate の albedo・roughness（0〜1）・size_m・height_above_ground_m・forward_m・"
                              "camera_height_above_plate_m が揃っていません";
            return false;
        }
        outDefinition = std::move(definition);
        return true;
    }

    void FindSun(const Definition &definition, const String &viewName, float &outAltitudeDegrees,
                 float &outAzimuthDegrees)
    {
        for (const ViewSun &sun : definition.Views)
        {
            if (sun.ViewName == viewName)
            {
                outAltitudeDegrees = sun.AltitudeDegrees;
                outAzimuthDegrees = sun.AzimuthDegrees;
                return;
            }
        }
        outAltitudeDegrees = definition.DefaultSunAltitudeDegrees;
        outAzimuthDegrees = definition.DefaultSunAzimuthDegrees;
    }

    bool Enter(GameModeContext &ctx, State &state)
    {
        state.bLoaded = false;
        state.pSun = nullptr;
        state.pPlate = nullptr;
        state.PlateMaterial = MaterialHandle{};
        state.bPlateMeshRegistered = false;
        state.bRTGIOverridden = false;
        if (!state.Opts.bEnabled && !state.Opts.bGrayPlate)
        {
            return true;
        }
        String error;
        if (!LoadDefinition(state.Def, error))
        {
            LOG_ERROR_F("WASTELAND_COMMON_LIGHT_INVALID 共通の照明の定義を読めません（%s）", error.c_str());
            return false;
        }
        state.bLoaded = true;
        if (state.Opts.bGrayPlate)
        {
            if (!CreatePlate(ctx, state))
            {
                return false;
            }
            // 板の値に周りの地形の照り返し・遮蔽を入れない（期待の値は一様な空と太陽だけから求める）
            RenderingCoordinator &coordinator = ctx.EngineRef.GetRenderWorld().GetRenderingCoordinator();
            state.bPreviousRTGI = coordinator.IsRTGIEnabled();
            coordinator.SetRTGIEnabled(false);
            state.bRTGIOverridden = true;
        }
        LOG_INFO_F("WASTELAND_COMMON_LIGHT_LOADED case=%s gray_plate=%d sky=(%.1f,%.1f,%.1f) sun_lux=%.1f "
                   "plate_albedo=%.3f plate_roughness=%.3f",
                   CaseName(state.Opts.Case), state.Opts.bGrayPlate ? 1 : 0,
                   static_cast<double>(state.Def.SkyRadiance.x), static_cast<double>(state.Def.SkyRadiance.y),
                   static_cast<double>(state.Def.SkyRadiance.z), static_cast<double>(state.Def.SunIlluminanceNormalLux),
                   static_cast<double>(state.Def.PlateAlbedo), static_cast<double>(state.Def.PlateRoughness));
        return true;
    }

    void ApplyEnvironment(GameModeContext &ctx, State &state, const String &viewName)
    {
        if (!state.bLoaded)
        {
            return;
        }
        RenderWorld &renderWorld = ctx.EngineRef.GetRenderWorld();
        // 空の大気（散乱・太陽の透過率）を切る。空気遠近と雲は空が有効なときだけ効くが、値も既定へ戻しておく
        SkyAtmosphereParameters sky = MakeDefaultSkyAtmosphereParameters();
        sky.bEnabled = false;
        renderWorld.SetSkyAtmosphere(sky);
        VolumetricFogParameters fog = MakeDefaultVolumetricFogParameters();
        fog.bEnabled = false;
        renderWorld.SetVolumetricFogParameters(fog);
        renderWorld.SetAerialPerspective(AerialPerspectiveParameters{});
        renderWorld.SetCloudLayer(CloudLayerParameters{});
        // 空の光と背景: 一様な空（空だけ・両方）か、放射輝度 0（太陽だけ）
        renderWorld.SetStaticEnvironmentIntensityScale(1.0f);
        renderWorld.SetStaticEnvironmentMaxRadiance(0.0f);
        const bool bSky = state.Opts.Case != LightCase::SunOnly;
        renderWorld.SetStaticEnvironmentUniformRadiance(true, bSky ? state.Def.SkyRadiance : NorvesLib::Math::Vector3::Zero);

        // 太陽: 太陽に垂直な面の照度（大気の透過率は掛けない）で、視点ごとの向きから差す白い方向光
        float altitude = 0.0f;
        float azimuth = 0.0f;
        FindSun(state.Def, viewName, altitude, azimuth);
        const bool bSun = state.Opts.Case != LightCase::SkyOnly;
        if (bSun && state.pSun == nullptr)
        {
            Entity *owner = ctx.ScopeRef.SpawnObject<Entity>();
            state.pSun = owner != nullptr ? ctx.WorldRef.CreateComponent<Component::DirectionalLightComponent>(owner)
                                          : nullptr;
            if (state.pSun == nullptr)
            {
                LOG_ERROR("wasteland: 共通の照明の太陽を作れませんでした");
            }
        }
        if (state.pSun != nullptr)
        {
            const NorvesLib::Math::Vector3 toSun = MakeSunDirectionFromAltitudeAzimuth(altitude, azimuth);
            state.pSun->SetIntensityUnit(Component::LightIntensityUnit::Lux);
            state.pSun->SetIntensity(state.Def.SunIlluminanceNormalLux);
            state.pSun->SetLightColor(1.0f, 1.0f, 1.0f);
            state.pSun->SetLightDirection(-toSun.x, -toSun.y, -toSun.z);
            state.pSun->SetCastShadows(true);
            state.pSun->SetLightVisible(bSun);
        }
        LOG_INFO_F("WASTELAND_COMMON_LIGHT view=%s case=%s sky=(%.1f,%.1f,%.1f) sun_lux=%.1f sun_altitude=%.2f "
                   "sun_azimuth=%.2f",
                   viewName.empty() ? "default" : viewName.c_str(), CaseName(state.Opts.Case),
                   static_cast<double>(bSky ? state.Def.SkyRadiance.x : 0.0f),
                   static_cast<double>(bSky ? state.Def.SkyRadiance.y : 0.0f),
                   static_cast<double>(bSky ? state.Def.SkyRadiance.z : 0.0f),
                   static_cast<double>(bSun ? state.Def.SunIlluminanceNormalLux : 0.0f), static_cast<double>(altitude),
                   static_cast<double>(azimuth));
    }

    void PlacePlate(GameModeContext &ctx, State &state, const Terrain::TerrainHeightField *pHeightField,
                    Game::Scenes::FreeCameraPose &inoutPose)
    {
        (void)ctx;
        if (!state.bLoaded || state.pPlate == nullptr)
        {
            return;
        }
        // 視線の水平の向き（Yaw 0 は +Z、増えると画面の右へ回る: 前は (-sin, 0, cos)）
        const float yaw = inoutPose.Yaw * kDegreesToRadians;
        const float forwardX = -std::sin(yaw);
        const float forwardZ = std::cos(yaw);
        const float centerX = inoutPose.Position[0] + forwardX * state.Def.PlateForwardMeters;
        const float centerZ = inoutPose.Position[2] + forwardZ * state.Def.PlateForwardMeters;

        // 板の下の地面の最も高い所（中心と四隅）から板の高さを決める。地形の格子が無ければ視点の目の高さから測る
        float ground = inoutPose.Position[1] - 1.7f;
        if (pHeightField != nullptr)
        {
            const float half = state.Def.PlateSizeMeters * 0.5f;
            const float offsets[5][2] = {{0.0f, 0.0f}, {-half, -half}, {half, -half}, {-half, half}, {half, half}};
            bool bFound = false;
            for (const auto &offset : offsets)
            {
                const float x = centerX + offset[0];
                const float z = centerZ + offset[1];
                if (pHeightField->Contains(x, z))
                {
                    const float height = pHeightField->SampleHeight(x, z);
                    ground = bFound ? std::max(ground, height) : height;
                    bFound = true;
                }
            }
        }
        const float plateY = ground + state.Def.PlateHeightAboveGroundMeters;
        state.pPlate->SetPosition(centerX, plateY, centerZ);

        // カメラは視点の水平の位置のまま、板の面から決めた高さに上げ、板の中心を画面の中心に見下ろす
        inoutPose.Position[1] = plateY + state.Def.CameraHeightAbovePlateMeters;
        inoutPose.Pitch = -std::atan2(state.Def.CameraHeightAbovePlateMeters, state.Def.PlateForwardMeters) /
                          kDegreesToRadians;
        LOG_INFO_F("WASTELAND_GRAY_PLATE center=(%.2f,%.2f,%.2f) size=%.2f ground=%.2f eye=%.2f pitch=%.2f",
                   static_cast<double>(centerX), static_cast<double>(plateY), static_cast<double>(centerZ),
                   static_cast<double>(state.Def.PlateSizeMeters), static_cast<double>(ground),
                   static_cast<double>(inoutPose.Position[1]), static_cast<double>(inoutPose.Pitch));
    }

    void Leave(GameModeContext &ctx, State &state)
    {
        RenderWorld &renderWorld = ctx.EngineRef.GetRenderWorld();
        renderWorld.SetStaticEnvironmentUniformRadiance(false, NorvesLib::Math::Vector3::Zero);
        if (state.bRTGIOverridden)
        {
            renderWorld.GetRenderingCoordinator().SetRTGIEnabled(state.bPreviousRTGI);
            state.bRTGIOverridden = false;
        }
        // 実体・材質・メッシュは GameModeScope::Cleanup が返す
        state.pSun = nullptr;
        state.pPlate = nullptr;
        state.PlateMaterial = MaterialHandle{};
        state.bPlateMeshRegistered = false;
        state.bLoaded = false;
    }

} // namespace Game::GameModes::WastelandCommonLight

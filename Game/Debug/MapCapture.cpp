#include "Debug/MapCapture.h"

#include "Debug/CapturedFramePng.h"
#include "Core/Public/Component/CameraComponent.h"
#include "Core/Public/Component/DirectionalLightComponent.h"
#include "Core/Public/Container/Span.h"
#include "Core/Public/Container/StringBuilder.h"
#include "Core/Public/Engine/Engine.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Math/QuaternionUtils.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Object/World.h"
#include "Core/Public/Platform/PlatformFile.h"
#include "Core/Public/Rendering/FrameCaptureTypes.h"
#include "Core/Public/Rendering/RenderWorld.h"
#include "Core/Public/Rendering/SceneView.h"
#include "Core/Public/Text/JsonDocument.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace Game::Debug
{
    namespace
    {
        using namespace NorvesLib::Core;
        using Container::Span;
        using Container::String;
        using Container::VariableArray;
        namespace R = NorvesLib::Core::Rendering;
        namespace M = NorvesLib::Math;

        // MultiViewCapture と同じ描画フレーム数で結果待ちを打ち切る。
        constexpr uint64_t kResultTimeoutRenderedFrames = 120;

        bool SetError(String* outError, const char* message)
        {
            if (outError != nullptr)
            {
                *outError = message;
            }
            return false;
        }

        bool InvalidField(String* outError, const char* name)
        {
            if (outError != nullptr)
            {
                Container::StringBuilder message;
                message.AppendFormat("設定項目 %s が無いか、値または型が不正です。", name);
                *outError = message.ToString();
            }
            return false;
        }

        bool ReadNumber(const JsonValue& object, const char* name, double& result, String* outError)
        {
            const JsonValue value = object.FindMember(name);
            if (!value.IsNumber() || !std::isfinite(value.AsNumber()))
            {
                return InvalidField(outError, name);
            }
            result = value.AsNumber();
            return true;
        }

        bool ReadUInt(const JsonValue& object, const char* name, uint32_t& result, String* outError)
        {
            double value = 0.0;
            if (!ReadNumber(object, name, value, outError))
            {
                return false;
            }
            if (value < 0.0 || value > static_cast<double>(UINT32_MAX) || std::trunc(value) != value)
            {
                return InvalidField(outError, name);
            }
            result = static_cast<uint32_t>(value);
            return true;
        }

        bool ReadFloat(const JsonValue& object, const char* name, float& result, String* outError)
        {
            double value = 0.0;
            if (!ReadNumber(object, name, value, outError))
            {
                return false;
            }
            if (std::abs(value) > static_cast<double>((std::numeric_limits<float>::max)()))
            {
                return InvalidField(outError, name);
            }
            result = static_cast<float>(value);
            return true;
        }

        bool ReadOptionalBool(const JsonValue& object, const char* name, bool& result, String* outError)
        {
            const JsonValue value = object.FindMember(name);
            if (!value.IsValid())
            {
                return true;
            }
            if (!value.IsBoolean())
            {
                return InvalidField(outError, name);
            }
            result = value.AsBool();
            return true;
        }

        bool ReadLayers(const JsonValue& root, R::RenderLayer& mask, String* outError)
        {
            const JsonValue layers = root.FindMember("culling_layers");
            if (!layers.IsValid())
            {
                mask = R::RenderLayer::MapVisible;
                return true;
            }
            if (!layers.IsArray())
            {
                return InvalidField(outError, "culling_layers");
            }
            mask = static_cast<R::RenderLayer>(0);
            for (size_t index = 0; index < layers.GetArraySize(); ++index)
            {
                const JsonValue layer = layers.GetArrayElement(index);
                if (!layer.IsString())
                {
                    return InvalidField(outError, "culling_layers");
                }
                const String& name = layer.AsString();
                R::RenderLayer bit;
                if (name == "Default")
                {
                    bit = R::RenderLayer::Default;
                }
                else if (name == "Transparent")
                {
                    bit = R::RenderLayer::Transparent;
                }
                else if (name == "UI")
                {
                    bit = R::RenderLayer::UI;
                }
                else if (name == "PostProcess")
                {
                    bit = R::RenderLayer::PostProcess;
                }
                else if (name == "Shadow")
                {
                    bit = R::RenderLayer::Shadow;
                }
                else if (name == "Debug")
                {
                    bit = R::RenderLayer::Debug;
                }
                else if (name == "MapVisible")
                {
                    bit = R::RenderLayer::MapVisible;
                }
                else if (name == "All")
                {
                    bit = R::RenderLayer::All;
                }
                else
                {
                    if (outError != nullptr)
                    {
                        Container::StringBuilder message;
                        message.AppendFormat("未知の描画レイヤー %s が指定されました。", name.c_str());
                        *outError = message.ToString();
                    }
                    return false;
                }
                mask = mask | bit;
            }
            return true;
        }

        bool IsSeparator(char value)
        {
            return value == '/' || value == '\\';
        }
        bool IsSeparator(wchar_t value)
        {
            return value == L'/' || value == L'\\';
        }

        bool HasEmbeddedNull(const String& value)
        {
            for (char character : value)
            {
                if (character == '\0')
                {
                    return true;
                }
            }
            return false;
        }

        bool ResolveOutputDirectory(const String& specPath, const String& output, String& result)
        {
            if (specPath.empty() || output.empty() || HasEmbeddedNull(specPath) || HasEmbeddedNull(output))
            {
                return false;
            }
            const bool bDrivePath = output.size() >= 2 && output[1] == ':';
            if (bDrivePath && (output.size() < 3 || !IsSeparator(output[2])))
            {
                return false;
            }
            if (IsSeparator(output[0]) || bDrivePath)
            {
                result = output;
                return true;
            }
            size_t separator = specPath.size();
            while (separator > 0 && !IsSeparator(specPath[separator - 1]))
            {
                --separator;
            }
            if (separator != 0)
            {
                result = specPath.substr(0, separator) + output;
            }
            else if (specPath.size() >= 2 && specPath[1] == ':')
            {
                result = specPath.substr(0, 2) + output;
            }
            else
            {
                result = output;
            }
            return true;
        }

        // EnemySmokeDriver の撮影用フォルダ作成と同じく、UTF-8 の親パスを Win32 で順に作る。
        bool EnsureParentDirectory(const String& filePath)
        {
            if (filePath.empty() || HasEmbeddedNull(filePath) ||
                filePath.size() >= static_cast<size_t>((std::numeric_limits<int>::max)()))
            {
                return false;
            }
            const int wideLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, filePath.c_str(), -1, nullptr, 0);
            if (wideLength <= 0)
            {
                return false;
            }
            VariableArray<wchar_t> widePath(static_cast<size_t>(wideLength));
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, filePath.c_str(), -1,
                                    widePath.data(), wideLength) != wideLength)
            {
                return false;
            }
            const DWORD required = GetFullPathNameW(widePath.data(), 0, nullptr, nullptr);
            if (required == 0)
            {
                return false;
            }
            VariableArray<wchar_t> fullPath(required);
            const DWORD length = GetFullPathNameW(widePath.data(), required, fullPath.data(), nullptr);
            if (length == 0 || length >= required || IsSeparator(fullPath[length - 1]))
            {
                return false;
            }
            size_t lastSeparator = length;
            while (lastSeparator > 0 && !IsSeparator(fullPath[lastSeparator - 1]))
            {
                --lastSeparator;
            }
            if (lastSeparator == 0)
            {
                return true;
            }
            --lastSeparator;
            size_t start = 3;
            if (length >= 2 && IsSeparator(fullPath[0]) && IsSeparator(fullPath[1]))
            {
                const bool bExtended = length >= 8 && fullPath[2] == L'?' && IsSeparator(fullPath[3]);
                const bool bExtendedUnc = bExtended && (fullPath[4] == L'U' || fullPath[4] == L'u') &&
                    (fullPath[5] == L'N' || fullPath[5] == L'n') &&
                    (fullPath[6] == L'C' || fullPath[6] == L'c') && IsSeparator(fullPath[7]);
                start = bExtendedUnc ? 8 : (bExtended ? 7 : 2);
                if (!bExtended || bExtendedUnc)
                {
                    int remaining = 2;
                    while (start < length && remaining > 0)
                    {
                        if (IsSeparator(fullPath[start++]))
                        {
                            --remaining;
                        }
                    }
                }
            }
            for (size_t index = start; index <= lastSeparator; ++index)
            {
                if (!IsSeparator(fullPath[index]))
                {
                    continue;
                }
                const wchar_t separator = fullPath[index];
                fullPath[index] = L'\0';
                const bool bCreated = CreateDirectoryW(fullPath.data(), nullptr) != 0;
                const DWORD error = bCreated ? ERROR_SUCCESS : GetLastError();
                const DWORD attributes = bCreated ? FILE_ATTRIBUTE_DIRECTORY : GetFileAttributesW(fullPath.data());
                fullPath[index] = separator;
                if (!bCreated && (error != ERROR_ALREADY_EXISTS || attributes == INVALID_FILE_ATTRIBUTES ||
                                 (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0))
                {
                    return false;
                }
            }
            return true;
        }

        uint32_t SetDirectionalSun(World& world, float altitude, float azimuth)
        {
            VariableArray<Entity*> entities = world.GetRootEntities();
            const M::Vector3 toSun = R::MakeSunDirectionFromAltitudeAzimuth(altitude, azimuth);
            uint32_t lightCount = 0;
            while (!entities.empty())
            {
                Entity* entity = entities.back();
                entities.pop_back();
                if (entity == nullptr || entity->IsPendingDestroy())
                {
                    continue;
                }
                for (Component::Component* component : entity->GetComponents())
                {
                    auto* light = CastTo<Component::DirectionalLightComponent>(component);
                    if (light != nullptr && !light->IsPendingDestroy())
                    {
                        // BuildLightProxy は Entity の回転ではなく、この光の進行方向を写す。
                        light->SetLightDirection(-toSun.x, -toSun.y, -toSun.z);
                        // World の同期後にも呼べるよう、このフレームの描画用の写しも更新する。
                        R::LightProxy proxy;
                        if (world.GetSceneView() != nullptr && light->BuildLightProxy(proxy))
                        {
                            world.GetSceneView()->UpdateLightProxy(proxy);
                        }
                        ++lightCount;
                    }
                }
                for (Entity* child : entity->GetChildEntities())
                {
                    entities.push_back(child);
                }
            }
            return lightCount;
        }

        bool CropFrame(R::CapturedFrame& frame, const Scene::MapCaptureTile& tile)
        {
            const uint64_t rowBytes = static_cast<uint64_t>(tile.CropWidth) * frame.BytesPerPixel;
            const uint64_t sourceBytes = static_cast<uint64_t>(frame.RowPitchBytes) * frame.Height;
            if (!frame.IsSuccess() || frame.BytesPerPixel == 0 || tile.CropWidth == 0 || tile.CropHeight == 0 ||
                static_cast<uint64_t>(tile.CropX) + tile.CropWidth > frame.Width ||
                static_cast<uint64_t>(tile.CropY) + tile.CropHeight > frame.Height ||
                static_cast<uint64_t>(frame.Width) * frame.BytesPerPixel > frame.RowPitchBytes ||
                sourceBytes > frame.Pixels.size() || rowBytes > static_cast<uint64_t>((std::numeric_limits<int>::max)()) ||
                tile.CropWidth > static_cast<uint32_t>((std::numeric_limits<int>::max)()) ||
                tile.CropHeight > static_cast<uint32_t>((std::numeric_limits<int>::max)()))
            {
                return false;
            }
            const uint64_t cropBytes = rowBytes * tile.CropHeight;
            if (cropBytes > static_cast<uint64_t>((std::numeric_limits<size_t>::max)()))
            {
                return false;
            }
            VariableArray<uint8_t> pixels(static_cast<size_t>(cropBytes));
            for (uint32_t y = 0; y < tile.CropHeight; ++y)
            {
                const size_t source = static_cast<size_t>(tile.CropY + y) * frame.RowPitchBytes +
                                      static_cast<size_t>(tile.CropX) * frame.BytesPerPixel;
                std::memcpy(pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(rowBytes),
                            frame.Pixels.data() + source, static_cast<size_t>(rowBytes));
            }
            frame.Width = tile.CropWidth;
            frame.Height = tile.CropHeight;
            frame.RowPitchBytes = static_cast<uint32_t>(rowBytes);
            frame.Pixels = std::move(pixels);
            return true;
        }
    } // namespace

    bool MapCapture::Configure(const String& specPath, uint64_t settledRenderedFrames, String* outError)
    {
        m_Phase = Phase::Disabled;
        if (outError != nullptr)
        {
            outError->clear();
        }
        VariableArray<uint8_t> bytes;
        if (!Platform::ReadFileBytes(specPath, bytes).bSuccess || bytes.empty())
        {
            return SetError(outError, "撮影設定ファイルを読み込めないか、ファイルが空です。");
        }
        const size_t start = bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf ? 3 : 0;
        JsonDocument document;
        if (!JsonDocument::TryParseUtf8(Span<const uint8_t>(bytes.data() + start, bytes.size() - start), document))
        {
            return SetError(outError, "撮影設定の JSON を読み取れません。");
        }
        const JsonValue root = document.GetRoot();
        if (!root.IsObject())
        {
            return SetError(outError, "撮影設定は JSON オブジェクトで指定してください。");
        }
        const JsonValue rect = root.FindMember("world_rect");
        if (!rect.IsObject())
        {
            return InvalidField(outError, "world_rect");
        }
        Scene::MapCaptureSpec spec;
        if (!ReadNumber(rect, "min_x", spec.WorldMinX, outError) ||
            !ReadNumber(rect, "min_z", spec.WorldMinZ, outError) ||
            !ReadNumber(rect, "max_x", spec.WorldMaxX, outError) ||
            !ReadNumber(rect, "max_z", spec.WorldMaxZ, outError) ||
            !ReadNumber(root, "pixel_size_m", spec.PixelSizeMeters, outError) ||
            !ReadUInt(root, "tile_margin_px", spec.TileMarginPixels, outError) ||
            !ReadUInt(root, "back_buffer_width", spec.BackBufferWidth, outError) ||
            !ReadUInt(root, "back_buffer_height", spec.BackBufferHeight, outError) ||
            !ReadNumber(root, "min_height_m", spec.MinHeightMeters, outError) ||
            !ReadNumber(root, "max_height_m", spec.MaxHeightMeters, outError) ||
            !ReadFloat(root, "ev100", spec.FixedEV100, outError) ||
            !ReadUInt(root, "settle_frames", spec.SettleFrames, outError))
        {
            return false;
        }
        const JsonValue directory = root.FindMember("output_directory");
        if (!directory.IsString() || !ResolveOutputDirectory(specPath, directory.AsString(), spec.OutputDirectory))
        {
            return InvalidField(outError, "output_directory");
        }
        R::RenderLayer mask = R::RenderLayer::MapVisible;
        bool bHeightFog = false;
        bool bAerial = false;
        float sunAltitude = 55.0f;
        float sunAzimuth = 135.0f;
        if (!ReadLayers(root, mask, outError) ||
            !ReadOptionalBool(root, "height_fog", bHeightFog, outError) ||
            !ReadOptionalBool(root, "aerial_perspective", bAerial, outError))
        {
            return false;
        }
        const JsonValue sun = root.FindMember("sun");
        if (sun.IsValid() && (!sun.IsObject() || !ReadFloat(sun, "altitude_deg", sunAltitude, outError) ||
                              !ReadFloat(sun, "azimuth_deg", sunAzimuth, outError)))
        {
            return InvalidField(outError, "sun");
        }
        Scene::MapCapturePlan plan;
        if (!Scene::BuildMapCapturePlan(spec, plan, outError))
        {
            return false;
        }
        const double shutter = std::exp2(-static_cast<double>(spec.FixedEV100));
        const float shutterFloat = static_cast<float>(shutter);
        R::CameraProxy snapshot;
        if (!std::isfinite(shutter) || shutter <= 0.0 || !std::isfinite(shutterFloat) || shutterFloat <= 0.0f ||
            !Component::CameraComponent::TryBuildExposureSnapshot(1.0f, shutterFloat, 100.0f, 0.0f, snapshot))
        {
            return SetError(outError, "指定された EV100 をカメラの露出として表現できません。");
        }
        if (!EnsureParentDirectory(spec.OutputDirectory + "/capture_manifest.json"))
        {
            return SetError(outError, "撮影結果の出力先フォルダを作成できません。");
        }
        m_Plan = std::move(plan);
        m_CullingMask = mask;
        m_Settle.Reset(settledRenderedFrames);
        // 初期読み込みが既に終わっていても、カメラのないシーンの待機を完了できるようにする。
        m_Settle.RestartAfterMove(0, 0);
        m_TileIndex = 0;
        m_WaitStartRenderedFrame = 0;
        m_ShutterSpeed = shutterFloat;
        m_SunAltitudeDegrees = sunAltitude;
        m_SunAzimuthDegrees = sunAzimuth;
        m_bHeightFog = bHeightFog;
        m_bAerialPerspective = bAerial;
        m_bCameraApplied = false;
        m_bFirstMoveStarted = false;
        m_bSunStatusLogged = false;
        m_Phase = Phase::Settling;
        LOG_INFO("MAP_CAPTURE tiles=%zu columns=%u rows=%u ortho_width=%.3f ortho_height=%.3f near=%.3f far=%.3f path=%s",
                   m_Plan.Tiles.size(), m_Plan.Columns, m_Plan.Rows, static_cast<double>(m_Plan.OrthoWidth),
                   static_cast<double>(m_Plan.OrthoHeight), static_cast<double>(m_Plan.NearPlane),
                   static_cast<double>(m_Plan.FarPlane), m_Plan.Spec.OutputDirectory.c_str());
        return true;
    }

    void MapCapture::Fail(const char* reason)
    {
        const char* tile = m_TileIndex < m_Plan.Tiles.size() ? m_Plan.Tiles[m_TileIndex].FileName.c_str() : "";
        LOG_ERROR("MAP_CAPTURE failed reason=%s tile=%s", reason, tile);
        m_Phase = Phase::Failed;
        if (Engine::GEngine)
        {
            Engine::GEngine->RequestExit(1);
        }
    }

    void MapCapture::ApplyToCamera(Entity& cameraEntity, Component::CameraComponent& camera)
    {
        if (!IsControllingCamera())
        {
            return;
        }
        const Scene::MapCaptureTile& tile = m_Plan.Tiles[m_TileIndex];
        M::Transform transform = cameraEntity.GetWorldTransform();
        transform.position = M::Vector3(static_cast<float>(tile.CameraPositionX), static_cast<float>(tile.CameraPositionY),
                                        static_cast<float>(tile.CameraPositionZ));
        // LookRotation はローカルの +Z を前へ、+Y を上へ向ける。
        transform.rotation = M::QuaternionUtils::LookRotation(M::Vector3(0.0f, -1.0f, 0.0f), M::Vector3(0.0f, 0.0f, 1.0f));
        cameraEntity.SetWorldTransform(transform);
        camera.SetProjectionType(R::ProjectionType::Orthographic);
        camera.SetOrthoSize(m_Plan.OrthoWidth, m_Plan.OrthoHeight);
        camera.SetNearPlane(m_Plan.NearPlane);
        camera.SetFarPlane(m_Plan.FarPlane);
        camera.SetCullingMask(m_CullingMask);
        camera.SetExposureMode(R::CameraExposureMode::Manual);
        camera.SetAntiAliasingMode(R::CameraAntiAliasingMode::FXAA);
        if (!camera.SetExposureCompensation(0.0f) || !camera.SetAperture(1.0f) ||
            !camera.SetISO(100.0f) || !camera.SetShutterSpeed(m_ShutterSpeed))
        {
            LOG_ERROR("MAP_CAPTURE カメラに指定の露出を設定できません。");
            Fail("camera_exposure_failed");
        }
    }

    void MapCapture::ApplyToProxy(R::CameraProxy& proxy)
    {
        if (!IsControllingCamera())
        {
            return;
        }
        const R::CameraProxy fixed = Scene::MakeMapCaptureCamera(m_Plan, m_Plan.Tiles[m_TileIndex]);
        proxy.PositionX = fixed.PositionX;
        proxy.PositionY = fixed.PositionY;
        proxy.PositionZ = fixed.PositionZ;
        proxy.ForwardX = fixed.ForwardX;
        proxy.ForwardY = fixed.ForwardY;
        proxy.ForwardZ = fixed.ForwardZ;
        proxy.UpX = fixed.UpX;
        proxy.UpY = fixed.UpY;
        proxy.UpZ = fixed.UpZ;
        proxy.RightX = fixed.RightX;
        proxy.RightY = fixed.RightY;
        proxy.RightZ = fixed.RightZ;
        proxy.Projection = fixed.Projection;
        proxy.AspectRatio = fixed.AspectRatio;
        proxy.OrthoWidth = fixed.OrthoWidth;
        proxy.OrthoHeight = fixed.OrthoHeight;
        proxy.NearPlane = fixed.NearPlane;
        proxy.FarPlane = fixed.FarPlane;
        proxy.Viewport = fixed.Viewport;
        proxy.CullingMask = m_CullingMask;
        proxy.Aperture = 1.0f;
        proxy.ShutterSpeed = m_ShutterSpeed;
        proxy.ISO = 100.0f;
        proxy.ExposureMode = R::CameraExposureMode::Manual;
        proxy.ExposureCompensation = 0.0f;
        proxy.EV100 = fixed.EV100;
        proxy.Exposure = fixed.Exposure;
        proxy.PreExposure = fixed.PreExposure;
        proxy.InvPreExposure = fixed.InvPreExposure;
        proxy.AutoExposureCurve.bEnabled = false;
        proxy.AntiAliasing = R::CameraAntiAliasingMode::FXAA;
        proxy.ProjectionJitterNdcX = 0.0f;
        proxy.ProjectionJitterNdcY = 0.0f;
        if (!proxy.GradingOverride.bEnabled)
        {
            // 標準 SceneView の ToneMappingPass 内のビネットは既に 0。
            // Contrast = 0 はグレーディングの差し替え条件を満たさず、既存の見た目を保つ。
            // VignettePass は同じ上書きのビネット値を独立に使う。
            proxy.GradingOverride.bEnabled = true;
            proxy.GradingOverride.Contrast = 0.0f;
        }
        proxy.GradingOverride.VignetteIntensity = 0.0f;
        if (!std::isfinite(proxy.GradingOverride.VignetteRadius))
        {
            proxy.GradingOverride.VignetteRadius = 0.8f;
        }
        if (!std::isfinite(proxy.GradingOverride.VignetteSoftness))
        {
            proxy.GradingOverride.VignetteSoftness = 0.5f;
        }
        proxy.LensEffects = R::CameraLensEffects{};
        m_bCameraApplied = true;
    }

    void MapCapture::OnPreRender(World* pWorld, R::RenderWorld& renderWorld, bool bOtherPending)
    {
        if (!IsControllingCamera() || !renderWorld.IsInitialized())
        {
            return;
        }
        if (!m_bHeightFog)
        {
            R::VolumetricFogParameters fog = R::MakeDefaultVolumetricFogParameters();
            fog.bEnabled = false;
            renderWorld.SetVolumetricFogParameters(fog);
        }
        if (!m_bAerialPerspective)
        {
            R::AerialPerspectiveParameters aerial;
            aerial.bEnabled = false;
            renderWorld.SetAerialPerspective(aerial);
        }
        R::SkyAtmosphereParameters sky = R::MakeDefaultSkyAtmosphereParameters();
        sky.bEnabled = true;
        sky.SunAltitudeDegrees = m_SunAltitudeDegrees;
        sky.SunAzimuthDegrees = m_SunAzimuthDegrees;
        renderWorld.SetSkyAtmosphere(sky);
        const uint32_t directionalLightCount = pWorld != nullptr
                                                  ? SetDirectionalSun(*pWorld, m_SunAltitudeDegrees, m_SunAzimuthDegrees)
                                                  : 0;
        if (!m_bSunStatusLogged)
        {
            LOG_INFO("MAP_CAPTURE sun altitude=%.1f azimuth=%.1f directional_lights=%u "
                     "撮影の間は空を既定の物理空に置き換え、方向光の部品も同じ向きにそろえる",
                     static_cast<double>(m_SunAltitudeDegrees), static_cast<double>(m_SunAzimuthDegrees),
                     directionalLightCount);
            m_bSunStatusLogged = true;
        }
        if (m_Phase == Phase::Settling)
        {
            m_Settle.OnPreRender(renderWorld, bOtherPending);
        }
    }

    void MapCapture::OnPostRender(R::RenderWorld& renderWorld, bool bOtherPending)
    {
        if (!IsControllingCamera() || !renderWorld.IsInitialized())
        {
            return;
        }
        const uint64_t rendered = renderWorld.GetRenderedFrameCount();
        const Scene::MapCaptureTile& tile = m_Plan.Tiles[m_TileIndex];
        if (m_Phase == Phase::Settling)
        {
            if (m_bCameraApplied && !m_bFirstMoveStarted)
            {
                m_bFirstMoveStarted = true;
                m_Settle.RestartAfterMove(rendered, m_Plan.Spec.SettleFrames);
                return;
            }
            if (!m_Settle.OnPostRender(renderWorld, bOtherPending, rendered, "MAP_CAPTURE", tile.FileName.c_str()))
            {
                return;
            }
            if (!m_bCameraApplied)
            {
                LOG_ERROR("MAP_CAPTURE このシーンは自由飛行カメラ（FreeCameraRig）を持たないので地図を撮れません。");
                Fail("free_camera_unavailable");
                return;
            }
            R::FrameCaptureRequest request;
            request.SourceKind = R::FrameCaptureSourceKind::BackBuffer;
            if (!renderWorld.RequestFrameCapture(request).IsAccepted())
            {
                Fail("capture_request_rejected");
                return;
            }
            m_Phase = Phase::WaitingResult;
            m_WaitStartRenderedFrame = rendered;
            LOG_INFO("MAP_CAPTURE tile=%s requested rendered=%llu", tile.FileName.c_str(),
                       static_cast<unsigned long long>(rendered));
            return;
        }
        R::CapturedFrame frame;
        if (!renderWorld.TryConsumeCapturedFrame(frame))
        {
            if (rendered >= m_WaitStartRenderedFrame && rendered - m_WaitStartRenderedFrame > kResultTimeoutRenderedFrames)
            {
                Fail("no_capture_result");
            }
            return;
        }
        if (!frame.IsSuccess())
        {
            LOG_ERROR("MAP_CAPTURE 撮影結果を取得できませんでした status=%u", static_cast<unsigned int>(frame.Status));
            Fail("capture_result_failed");
            return;
        }
        if (frame.Width != m_Plan.Spec.BackBufferWidth || frame.Height != m_Plan.Spec.BackBufferHeight)
        {
            LOG_ERROR("MAP_CAPTURE バックバッファの寸法が異なります actual=%ux%u --window-size=%ux%u と --render-scale=1 を指定してください。",
                        frame.Width, frame.Height, m_Plan.Spec.BackBufferWidth, m_Plan.Spec.BackBufferHeight);
            Fail("back_buffer_size_mismatch");
            return;
        }
        const String path = m_Plan.Spec.OutputDirectory + "/" + tile.FileName;
        if (!CropFrame(frame, tile) || !SaveCapturedFramePng(frame, path))
        {
            LOG_ERROR("MAP_CAPTURE タイルを保存できません path=%s", path.c_str());
            Fail("save_failed");
            return;
        }
        LOG_INFO("MAP_CAPTURE tile=%s saved width=%u height=%u path=%s", tile.FileName.c_str(),
                   frame.Width, frame.Height, path.c_str());
        ++m_TileIndex;
        if (m_TileIndex < m_Plan.Tiles.size())
        {
            m_Phase = Phase::Settling;
            m_Settle.RestartAfterMove(rendered, m_Plan.Spec.SettleFrames);
            return;
        }
        const String manifest = Scene::WriteMapCaptureManifestJson(m_Plan);
        const String manifestPath = m_Plan.Spec.OutputDirectory + "/capture_manifest.json";
        if (manifest.empty() || !Platform::WriteFileAtomic(
                manifestPath, Span<const uint8_t>(reinterpret_cast<const uint8_t*>(manifest.data()), manifest.size()), false).bSuccess)
        {
            LOG_ERROR("MAP_CAPTURE 目録を保存できません path=%s", manifestPath.c_str());
            Fail("manifest_save_failed");
            return;
        }
        m_Phase = Phase::Done;
        LOG_INFO("MAP_CAPTURE done tiles=%zu", m_Plan.Tiles.size());
        if (Engine::GEngine)
        {
            Engine::GEngine->RequestExit(0);
        }
    }
} // namespace Game::Debug

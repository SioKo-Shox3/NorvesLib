#include "Environment/EnvironmentState.h"

#include "Logging/LogMacros.h"
#include "Text/JsonDocument.h"
#include "Text/JsonWriter.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>

namespace NorvesLib::Core::Environment
{
    namespace
    {
        float ClampFinite(float value, float fallback, float minimum, float maximum)
        {
            if (!std::isfinite(value))
            {
                return fallback;
            }
            return value < minimum ? minimum : (value > maximum ? maximum : value);
        }

        float ClampMinimumFinite(float value, float fallback, float minimum)
        {
            if (!std::isfinite(value))
            {
                return fallback;
            }
            return value < minimum ? minimum : value;
        }

        float WrapFinite(float value, float fallback, float period)
        {
            if (!std::isfinite(value))
            {
                return fallback;
            }
            if (value >= 0.0f && value < period)
            {
                return value;
            }

            float wrapped = std::fmod(value, period);
            if (wrapped < 0.0f)
            {
                wrapped += period;
            }
            return wrapped >= period ? 0.0f : wrapped;
        }

        struct JsonField
        {
            const TCHAR* Group;
            const TCHAR* Key;
            EEnvironmentField Field;
            double Minimum;
            double Maximum;
            bool bExclusiveMaximum = false;
        };

        // 状態とプリセットで同じ検証を使う。float に保持できない数値も受け付けない。
        constexpr double FloatLimit = std::numeric_limits<float>::max();
        constexpr JsonField JsonFields[] = {
            {_T(""), _T("rain"), EEnvironmentField::RainIntensity, 0, 1},
            {_T(""), _T("wetness"), EEnvironmentField::Wetness, 0, 1},
            {_T(""), _T("cloudCover"), EEnvironmentField::CloudCover, 0, 1},
            {_T(""), _T("timeOfDayHours"), EEnvironmentField::TimeOfDayHours, 0, 24, true},
            {_T("wind"), _T("azimuthDeg"), EEnvironmentField::WindDirectionYaw, 0, 360, true},
            {_T("wind"), _T("speed"), EEnvironmentField::WindBaseSpeed, 0, 100},
            {_T("wind"), _T("gust"), EEnvironmentField::WindGustStrength, 0, 1},
            {_T("fog"), _T("density"), EEnvironmentField::FogDensityAtBaseHeight, 0, FloatLimit},
            {_T("fog"), _T("baseHeight"), EEnvironmentField::FogBaseHeight, -FloatLimit, FloatLimit},
            {_T("fog"), _T("heightFalloff"), EEnvironmentField::FogHeightFalloffPerUnit, 0, FloatLimit},
            {_T("fog"), _T("maxDistance"), EEnvironmentField::FogMaxDistanceMeters, 0, FloatLimit},
            {_T("fog"), _T("ambientScale"), EEnvironmentField::FogAmbientScale, 0, FloatLimit},
            {_T("fog"), _T("skyBlend"), EEnvironmentField::FogSkyBlend, 0, 1},
            {_T("fog"), _T("referenceElevationDeg"), EEnvironmentField::FogReferenceElevation, 0, 90},
            {_T("cloudLayer"), _T("coverage"), EEnvironmentField::CloudLayerCoverage, 0, 1},
            {_T("sun"), _T("altitudeDeg"), EEnvironmentField::SunAltitude, -90, 90},
            {_T("sun"), _T("azimuthDeg"), EEnvironmentField::SunAzimuth, -360, 360}
        };

        bool JsonError(Container::String* pOutError, const TCHAR* message)
        {
            if (pOutError)
            {
                *pOutError = message;
            }
            return false;
        }

        void WarnJson(const Container::String& path, const TCHAR* reason)
        {
            const Container::String message = Container::String(_T("EnvironmentState の JSON: '")) +
                                              path + _T("' ") + reason;
#ifdef UNICODE
            LOG_WARNING("%ls", message.c_str());
#else
            LOG_WARNING("%s", message.c_str());
#endif
        }

        void WarnInvalid(const Container::String& path)
        {
            WarnJson(path, _T("の型が異なるか値が範囲外のため既定値にします（プリセットでは遷移の対象から外します）"));
        }

        bool ReadNumber(const JsonValue& value, double minimum, double maximum, bool bExclusiveMaximum = false)
        {
            return value.IsNumber() && std::isfinite(value.AsNumber()) && value.AsNumber() >= minimum &&
                   (bExclusiveMaximum ? value.AsNumber() < maximum : value.AsNumber() <= maximum);
        }

        bool ValidateRoot(const JsonValue& root, Container::String* pOutError)
        {
            if (!root.IsObject())
            {
                return JsonError(pOutError, _T("気象の JSON の根はオブジェクトである必要があります"));
            }
            const JsonValue version = root.FindMember("formatVersion");
            if (!version.IsNumber() || version.AsNumber() != EnvironmentStateFormatVersion)
            {
                return JsonError(pOutError, _T("気象の JSON の formatVersion は 1 が必要です"));
            }
            return true;
        }

        void ReadStateObject(const JsonValue& object, const Container::String& group,
                             EnvironmentTarget& target, bool bPreset, float& seconds, ETransitionCurve& curve)
        {
            for (size_t index = 0; index < object.GetObjectSize(); ++index)
            {
                const Container::String& key = object.GetMemberName(index);
                const JsonValue value = object.GetMemberValue(index);
                const Container::String path = group.empty() ? key : group + _T(".") + key;
                if (group.empty() && key == _T("formatVersion"))
                {
                    continue;
                }
                if ((group.empty() && (key == _T("wetness") || key == _T("gameTimeSeconds") || key == _T("seed"))) ||
                    (group == _T("sun") && key == _T("mode")))
                {
                    if (bPreset)
                    {
                        WarnJson(path, _T("はプリセットでは使わないため無視します"));
                        continue;
                    }
                }
                bool bHandled = false;
                for (const auto& field : JsonFields)
                {
                    if (group != field.Group || key != field.Key)
                    {
                        continue;
                    }
                    bHandled = true;
                    const double number = value.AsNumber();
                    if (ReadNumber(value, field.Minimum, field.Maximum, field.bExclusiveMaximum) &&
                        (!field.bExclusiveMaximum || static_cast<float>(number) < field.Maximum))
                    {
                        SetEnvironmentField(target.Values, field.Field, static_cast<float>(number));
                        target.Mask |= EnvironmentFieldBit(field.Field);
                    }
                    else
                    {
                        WarnInvalid(path);
                    }
                    break;
                }
                if (bHandled)
                {
                    continue;
                }
                if (group.empty() && (key == _T("wind") || key == _T("fog") ||
                                      key == _T("cloudLayer") || key == _T("sun")))
                {
                    if (value.IsObject())
                    {
                        ReadStateObject(value, key, target, bPreset, seconds, curve);
                    }
                    else
                    {
                        WarnInvalid(path);
                    }
                }
                else if (group.empty() && key == _T("gameTimeSeconds"))
                {
                    if (ReadNumber(value, 0, std::numeric_limits<double>::max()))
                    {
                        target.Values.GameTimeSeconds = value.AsNumber();
                    }
                    else
                    {
                        WarnInvalid(path);
                    }
                }
                else if (group.empty() && key == _T("seed"))
                {
                    if (value.IsIntegerLiteral() && ReadNumber(value, 0, 4294967295.0))
                    {
                        target.Values.Seed = static_cast<uint32_t>(value.AsNumber());
                    }
                    else
                    {
                        WarnInvalid(path);
                    }
                }
                else if (group == _T("sun") && key == _T("mode"))
                {
                    if (value.IsString() && (value.AsString() == _T("manual") || value.AsString() == _T("clock")))
                    {
                        target.Values.SunMode = value.AsString() == _T("clock") ? EEnvironmentSunMode::Clock : EEnvironmentSunMode::Manual;
                    }
                    else
                    {
                        WarnInvalid(path);
                    }
                }
                else if (group == _T("sun") && key == _T("timeScale"))
                {
                    if (ReadNumber(value, 0, 3600))
                    {
                        target.Values.ClockTimeScale = static_cast<float>(value.AsNumber());
                    }
                    else
                    {
                        WarnInvalid(path);
                    }
                }
                else if (group == _T("cloudLayer") && key == _T("enabled"))
                {
                    if (value.IsBoolean())
                    {
                        target.Values.bCloudLayerEnabled = value.AsBool();
                        target.Mask |= EnvironmentFieldBit(EEnvironmentField::CloudLayerEnabled);
                    }
                    else
                    {
                        WarnInvalid(path);
                    }
                }
                else if (group == _T("fog") && key == _T("tint"))
                {
                    if (!value.IsArray() || value.GetArraySize() != 3)
                    {
                        WarnInvalid(path);
                        continue;
                    }
                    constexpr EEnvironmentField channels[] = {
                        EEnvironmentField::FogTintR, EEnvironmentField::FogTintG, EEnvironmentField::FogTintB};
                    const TCHAR* channelPaths[] = {_T("fog.tint[0]"), _T("fog.tint[1]"), _T("fog.tint[2]")};
                    for (size_t channel = 0; channel < 3; ++channel)
                    {
                        const JsonValue element = value.GetArrayElement(channel);
                        if (ReadNumber(element, 0, 16))
                        {
                            target.Values.FogTint[channel] = static_cast<float>(element.AsNumber());
                            target.Mask |= EnvironmentFieldBit(channels[channel]);
                        }
                        else
                        {
                            WarnInvalid(Container::String(channelPaths[channel]));
                        }
                    }
                }
                else if (bPreset && group.empty() && key == _T("transitionSeconds"))
                {
                    if (ReadNumber(value, 0, FloatLimit))
                    {
                        seconds = static_cast<float>(value.AsNumber());
                    }
                    else
                    {
                        WarnInvalid(path);
                    }
                }
                else if (bPreset && group.empty() && key == _T("curve"))
                {
                    if (value.IsString() && (value.AsString() == _T("smoothstep") || value.AsString() == _T("linear")))
                    {
                        curve = value.AsString() == _T("linear") ? ETransitionCurve::Linear : ETransitionCurve::SmoothStep;
                    }
                    else
                    {
                        WarnInvalid(path);
                    }
                }
                else
                {
                    WarnJson(path, _T("は未知のキーのため無視します"));
                }
            }
        }

        bool ReadPresetRoot(const JsonValue& root, EnvironmentTarget& outTarget, Container::String* pOutError)
        {
            if (!ValidateRoot(root, pOutError))
            {
                return false;
            }
            EnvironmentTarget target;
            float seconds = 0;
            ETransitionCurve curve = ETransitionCurve::SmoothStep;
            ReadStateObject(root, {}, target, true, seconds, curve);
            target.Values = SanitizeEnvironmentState(target.Values);
            SetEnvironmentTargetDuration(target, seconds, curve);
            outTarget = target;
            return true;
        }
    } // namespace

    Container::String ToJson(const EnvironmentState& state)
    {
        const EnvironmentState value = SanitizeEnvironmentState(state);
        JsonWriter writer(true);
        writer.BeginObject();
        writer.WriteNumber("formatVersion", EnvironmentStateFormatVersion);
        writer.WriteNumber("gameTimeSeconds", value.GameTimeSeconds);
        writer.WriteNumber("timeOfDayHours", value.TimeOfDayHours);
        writer.WriteNumber("rain", value.RainIntensity);
        writer.WriteNumber("wetness", value.Wetness);
        writer.WriteNumber("cloudCover", value.CloudCover);
        writer.BeginObject("wind");
        writer.WriteNumber("azimuthDeg", value.WindDirectionYawDegrees);
        writer.WriteNumber("speed", value.WindBaseSpeed);
        writer.WriteNumber("gust", value.WindGustStrength);
        writer.EndObject();
        writer.BeginObject("fog");
        writer.WriteNumber("density", value.FogDensityAtBaseHeight);
        writer.WriteNumber("baseHeight", value.FogBaseHeight);
        writer.WriteNumber("heightFalloff", value.FogHeightFalloffPerUnit);
        writer.WriteNumber("maxDistance", value.FogMaxDistanceMeters);
        writer.WriteNumber("ambientScale", value.FogAmbientScale);
        writer.BeginArray("tint");
        for (float channel : value.FogTint)
        {
            writer.WriteNumberElement(channel);
        }
        writer.EndArray();
        writer.WriteNumber("skyBlend", value.FogSkyBlend);
        writer.WriteNumber("referenceElevationDeg", value.FogReferenceElevationDegrees);
        writer.EndObject();
        writer.BeginObject("cloudLayer");
        writer.WriteBool("enabled", value.bCloudLayerEnabled);
        writer.WriteNumber("coverage", value.CloudLayerCoverage);
        writer.EndObject();
        writer.BeginObject("sun");
        writer.WriteString("mode", value.SunMode == EEnvironmentSunMode::Clock ? "clock" : "manual");
        writer.WriteNumber("altitudeDeg", value.SunAltitudeDegrees);
        writer.WriteNumber("azimuthDeg", value.SunAzimuthDegrees);
        writer.WriteNumber("timeScale", value.ClockTimeScale);
        writer.EndObject();
        writer.WriteNumber("seed", value.Seed);
        writer.EndObject();
        return writer.IsComplete() ? writer.ToString() : Container::String{};
    }

    bool FromJson(const Container::String& text, EnvironmentState& outState, Container::String* pOutError)
    {
        if (pOutError)
        {
            pOutError->clear();
        }
        JsonDocument document;
        if (!JsonDocument::TryParse(text, document, pOutError) || !ValidateRoot(document.GetRoot(), pOutError))
        {
            return false;
        }
        EnvironmentTarget target;
        float seconds = 0;
        ETransitionCurve curve = ETransitionCurve::SmoothStep;
        ReadStateObject(document.GetRoot(), {}, target, false, seconds, curve);
        outState = SanitizeEnvironmentState(target.Values);
        return true;
    }

    bool EnvironmentPresetFromJson(const Container::String& text, EnvironmentTarget& outTarget,
                                   Container::String* pOutError)
    {
        if (pOutError)
        {
            pOutError->clear();
        }
        JsonDocument document;
        return JsonDocument::TryParse(text, document, pOutError) && ReadPresetRoot(document.GetRoot(), outTarget, pOutError);
    }

    bool MakeEnvironmentPresetPath(const Container::String& name, Container::String& outPath)
    {
        if (name.empty() || name.size() > 64)
        {
            return false;
        }
        for (const auto character : name)
        {
            if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                  (character >= '0' && character <= '9') || character == '_' || character == '-'))
            {
                return false;
            }
        }
        outPath = Container::String(_T("Assets/Data/Environment/")) + name + _T(".env.json");
        return true;
    }

    bool LoadEnvironmentPreset(const Container::String& name, EnvironmentTarget& outTarget, Container::String* pOutError)
    {
        return LoadEnvironmentPreset(Container::String{}, name, outTarget, pOutError);
    }

    bool LoadEnvironmentPreset(const Container::String& rootDirectory, const Container::String& name,
                               EnvironmentTarget& outTarget, Container::String* pOutError)
    {
        if (pOutError)
        {
            pOutError->clear();
        }
        Container::String path;
        if (!MakeEnvironmentPresetPath(name, path))
        {
            return JsonError(pOutError, _T("プリセット名は英数字・'_'・'-' の 1～64 文字にしてください"));
        }
        try
        {
            const std::filesystem::path filePath = rootDirectory.empty() ? std::filesystem::path(path.c_str()) :
                std::filesystem::path(rootDirectory.c_str()) / std::filesystem::path(path.c_str());
            std::error_code error;
            if (!std::filesystem::is_regular_file(filePath, error) || error)
            {
                return JsonError(pOutError, _T("気象のプリセットファイルが見つかりません"));
            }
            std::ifstream file(filePath, std::ios::binary | std::ios::ate);
            if (!file)
            {
                return JsonError(pOutError, _T("気象のプリセットファイルを開けません"));
            }
            const auto length = file.tellg();
            if (length <= 0 || length > 1048576)
            {
                return JsonError(pOutError, _T("気象のプリセットファイルの大きさは 1～1048576 バイトが必要です"));
            }
            Container::VariableArray<uint8_t> bytes(size_t(length), 0);
            file.seekg(0);
            if (!file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size())) ||
                file.peek() != std::char_traits<char>::eof())
            {
                return JsonError(pOutError, _T("気象のプリセットファイルを最後まで読めません"));
            }
            const size_t skip = bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf ? 3 : 0;
            JsonDocument document;
            return JsonDocument::TryParseUtf8({bytes.data() + skip, bytes.size() - skip}, document, pOutError) &&
                   ReadPresetRoot(document.GetRoot(), outTarget, pOutError);
        }
        catch (...)
        {
            return JsonError(pOutError, _T("気象のプリセットファイルの読み込み中に例外が発生しました"));
        }
    }

    void SetEnvironmentTargetDuration(EnvironmentTarget& target, float seconds, ETransitionCurve curve)
    {
        for (uint32_t index = 0; index < EnvironmentFieldCount; ++index)
        {
            target.DurationSeconds[index] = seconds;
            target.Curves[index] = curve;
        }
    }

    EnvironmentState MakeEnvironmentStateFromScene(const Rendering::SkyAtmosphereParameters& sky,
                                                   const Rendering::VolumetricFogParameters& fog,
                                                   const Rendering::CloudLayerParameters& cloudLayer,
                                                   const Rendering::WindParameters& wind)
    {
        EnvironmentState result;
        result.CloudCover = sky.CloudCover;
        result.SunAltitudeDegrees = sky.SunAltitudeDegrees;
        result.SunAzimuthDegrees = sky.SunAzimuthDegrees;
        result.FogDensityAtBaseHeight = fog.DensityAtBaseHeight;
        result.FogBaseHeight = fog.BaseHeight;
        result.FogHeightFalloffPerUnit = fog.HeightFalloffPerUnit;
        result.FogMaxDistanceMeters = fog.MaxDistanceMeters;
        result.FogAmbientScale = fog.AmbientScale;
        for (uint32_t channel = 0; channel < 3; ++channel)
        {
            result.FogTint[channel] = fog.Tint[channel];
        }
        result.FogSkyBlend = fog.SkyBlend;
        result.FogReferenceElevationDegrees = fog.ReferenceElevationDegrees;
        result.bCloudLayerEnabled = cloudLayer.bEnabled;
        result.CloudLayerCoverage = cloudLayer.Coverage;
        result.WindDirectionYawDegrees = wind.DirectionYawDegrees;
        result.WindBaseSpeed = wind.BaseSpeed;
        result.WindGustStrength = wind.GustStrength;
        return result;
    }

    EnvironmentSceneBase MakeEnvironmentSceneBase(const Rendering::SkyAtmosphereParameters& sky,
                                                   const Rendering::VolumetricFogParameters& fog,
                                                   const Rendering::CloudLayerParameters& cloudLayer,
                                                   const Rendering::WindParameters& wind)
    {
        return EnvironmentSceneBase{sky, fog, cloudLayer, wind};
    }

    EnvironmentState SanitizeEnvironmentState(const EnvironmentState& in)
    {
        const EnvironmentState defaults;
        EnvironmentState result = in;
        result.RainIntensity = ClampFinite(in.RainIntensity, defaults.RainIntensity, 0.0f, 1.0f);
        result.Wetness = ClampFinite(in.Wetness, defaults.Wetness, 0.0f, 1.0f);
        result.CloudCover = ClampFinite(in.CloudCover, defaults.CloudCover, 0.0f, 1.0f);
        result.WindDirectionYawDegrees = WrapFinite(in.WindDirectionYawDegrees, defaults.WindDirectionYawDegrees, 360.0f);
        result.WindBaseSpeed = ClampFinite(in.WindBaseSpeed, defaults.WindBaseSpeed, 0.0f, 100.0f);
        result.WindGustStrength = ClampFinite(in.WindGustStrength, defaults.WindGustStrength, 0.0f, 1.0f);
        result.FogDensityAtBaseHeight = ClampMinimumFinite(in.FogDensityAtBaseHeight, defaults.FogDensityAtBaseHeight, 0.0f);
        result.FogBaseHeight = std::isfinite(in.FogBaseHeight) ? in.FogBaseHeight : defaults.FogBaseHeight;
        result.FogHeightFalloffPerUnit = ClampMinimumFinite(in.FogHeightFalloffPerUnit, defaults.FogHeightFalloffPerUnit, 0.0f);
        result.FogMaxDistanceMeters = ClampMinimumFinite(in.FogMaxDistanceMeters, defaults.FogMaxDistanceMeters, 0.0f);
        result.FogAmbientScale = ClampMinimumFinite(in.FogAmbientScale, defaults.FogAmbientScale, 0.0f);
        for (uint32_t channel = 0; channel < 3; ++channel)
        {
            result.FogTint[channel] = ClampFinite(in.FogTint[channel], defaults.FogTint[channel], 0.0f, 16.0f);
        }
        result.FogSkyBlend = ClampFinite(in.FogSkyBlend, defaults.FogSkyBlend, 0.0f, 1.0f);
        result.FogReferenceElevationDegrees = ClampFinite(in.FogReferenceElevationDegrees, defaults.FogReferenceElevationDegrees, 0.0f, 90.0f);
        result.CloudLayerCoverage = ClampFinite(in.CloudLayerCoverage, defaults.CloudLayerCoverage, 0.0f, 1.0f);
        if (in.SunMode != EEnvironmentSunMode::Manual && in.SunMode != EEnvironmentSunMode::Clock)
        {
            result.SunMode = defaults.SunMode;
        }
        result.SunAltitudeDegrees = ClampFinite(in.SunAltitudeDegrees, defaults.SunAltitudeDegrees, -90.0f, 90.0f);
        result.SunAzimuthDegrees = ClampFinite(in.SunAzimuthDegrees, defaults.SunAzimuthDegrees, -360.0f, 360.0f);
        result.TimeOfDayHours = WrapFinite(in.TimeOfDayHours, defaults.TimeOfDayHours, 24.0f);
        result.ClockTimeScale = ClampFinite(in.ClockTimeScale, defaults.ClockTimeScale, 0.0f, 3600.0f);
        result.WetTimeConstantSeconds = ClampMinimumFinite(in.WetTimeConstantSeconds, defaults.WetTimeConstantSeconds, 0.001f);
        result.DryTimeConstantSeconds = ClampMinimumFinite(in.DryTimeConstantSeconds, defaults.DryTimeConstantSeconds, 0.001f);
        if (!std::isfinite(in.GameTimeSeconds))
        {
            result.GameTimeSeconds = defaults.GameTimeSeconds;
        }
        else if (in.GameTimeSeconds < 0.0)
        {
            result.GameTimeSeconds = 0.0;
        }
        return result;
    }

    EnvironmentResolved ResolveEnvironment(const EnvironmentState& state, const EnvironmentSceneBase& base)
    {
        EnvironmentResolved result{base.Sky, base.Fog, base.CloudLayer, base.Wind, {}};
        result.Sky.CloudCover = std::round(ClampFinite(state.CloudCover, 0.0f, 0.0f, 1.0f) * 100.0f) / 100.0f;
        // Clock も現在は状態の高度と方位で解決する。
        result.Sky.SunAltitudeDegrees = state.SunAltitudeDegrees;
        result.Sky.SunAzimuthDegrees = state.SunAzimuthDegrees;
        result.Fog.DensityAtBaseHeight = state.FogDensityAtBaseHeight;
        result.Fog.BaseHeight = state.FogBaseHeight;
        result.Fog.HeightFalloffPerUnit = state.FogHeightFalloffPerUnit;
        result.Fog.MaxDistanceMeters = state.FogMaxDistanceMeters;
        result.Fog.AmbientScale = state.FogAmbientScale;
        for (uint32_t channel = 0; channel < 3; ++channel)
        {
            result.Fog.Tint[channel] = state.FogTint[channel];
        }
        result.Fog.SkyBlend = state.FogSkyBlend;
        result.Fog.ReferenceElevationDegrees = state.FogReferenceElevationDegrees;
        result.CloudLayer.bEnabled = state.bCloudLayerEnabled;
        result.CloudLayer.Coverage = state.CloudLayerCoverage;
        result.Wind.DirectionYawDegrees = state.WindDirectionYawDegrees;
        result.Wind.BaseSpeed = state.WindBaseSpeed;
        result.Wind.GustStrength = state.WindGustStrength;
        result.Environment.bEnabled = true;
        result.Environment.RainIntensity = state.RainIntensity;
        result.Environment.Wetness = state.Wetness;
        result.Environment.CloudCover = state.CloudCover;
        result.Environment.GameTimeSeconds = state.GameTimeSeconds;
        result.Environment.Seed = state.Seed;
        result.Environment.DiscontinuityId = state.DiscontinuityId;
        return result;
    }

    float AdvanceEnvironmentWetness(float wetness, float rainIntensity, double dtSeconds,
                                    float wetTimeConstantSeconds, float dryTimeConstantSeconds)
    {
        if (dtSeconds <= 0.0)
        {
            return wetness;
        }

        const double rain = rainIntensity < 0.0f ? 0.0 : (rainIntensity > 1.0f ? 1.0 : static_cast<double>(rainIntensity));
        const double smoothProgress = (rain - 0.02) / (0.2 - 0.02);
        const double s = smoothProgress < 0.0 ? 0.0 : (smoothProgress > 1.0 ? 1.0 : smoothProgress);
        const double effectiveRain = s * s * (3.0 - 2.0 * s);
        const double wetRate = 3.0 / static_cast<double>(wetTimeConstantSeconds);
        const double dryRate = 3.0 / static_cast<double>(dryTimeConstantSeconds);
        const double k = wetRate * effectiveRain + dryRate * (1.0 - effectiveRain);
        if (k <= 0.0)
        {
            return wetness;
        }

        const double equilibrium = wetRate * effectiveRain / k;
        const double result = equilibrium + (static_cast<double>(wetness) - equilibrium) * std::exp(-dtSeconds * k);
        return static_cast<float>(result < 0.0 ? 0.0 : (result > 1.0 ? 1.0 : result));
    }

    float EvaluateTransitionCurve(ETransitionCurve curve, float progress)
    {
        const float s = progress < 0.0f ? 0.0f : (progress > 1.0f ? 1.0f : progress);
        if (curve == ETransitionCurve::Linear)
        {
            return s;
        }
        return s * s * (3.0f - 2.0f * s);
    }

    float GetEnvironmentField(const EnvironmentState& state, EEnvironmentField field)
    {
        switch (field)
        {
        case EEnvironmentField::RainIntensity: return state.RainIntensity;
        case EEnvironmentField::CloudCover: return state.CloudCover;
        case EEnvironmentField::WindDirectionYaw: return state.WindDirectionYawDegrees;
        case EEnvironmentField::WindBaseSpeed: return state.WindBaseSpeed;
        case EEnvironmentField::WindGustStrength: return state.WindGustStrength;
        case EEnvironmentField::FogDensityAtBaseHeight: return state.FogDensityAtBaseHeight;
        case EEnvironmentField::FogBaseHeight: return state.FogBaseHeight;
        case EEnvironmentField::FogHeightFalloffPerUnit: return state.FogHeightFalloffPerUnit;
        case EEnvironmentField::FogMaxDistanceMeters: return state.FogMaxDistanceMeters;
        case EEnvironmentField::FogAmbientScale: return state.FogAmbientScale;
        case EEnvironmentField::CloudLayerEnabled: return state.bCloudLayerEnabled ? 1.0f : 0.0f;
        case EEnvironmentField::CloudLayerCoverage: return state.CloudLayerCoverage;
        case EEnvironmentField::SunAltitude: return state.SunAltitudeDegrees;
        case EEnvironmentField::SunAzimuth: return state.SunAzimuthDegrees;
        case EEnvironmentField::TimeOfDayHours: return state.TimeOfDayHours;
        case EEnvironmentField::Wetness: return state.Wetness;
        case EEnvironmentField::FogTintR: return state.FogTint[0];
        case EEnvironmentField::FogTintG: return state.FogTint[1];
        case EEnvironmentField::FogTintB: return state.FogTint[2];
        case EEnvironmentField::FogSkyBlend: return state.FogSkyBlend;
        case EEnvironmentField::FogReferenceElevation: return state.FogReferenceElevationDegrees;
        default: return 0.0f;
        }
    }

    void SetEnvironmentField(EnvironmentState& state, EEnvironmentField field, float value)
    {
        switch (field)
        {
        case EEnvironmentField::RainIntensity: state.RainIntensity = value; break;
        case EEnvironmentField::CloudCover: state.CloudCover = value; break;
        case EEnvironmentField::WindDirectionYaw: state.WindDirectionYawDegrees = value; break;
        case EEnvironmentField::WindBaseSpeed: state.WindBaseSpeed = value; break;
        case EEnvironmentField::WindGustStrength: state.WindGustStrength = value; break;
        case EEnvironmentField::FogDensityAtBaseHeight: state.FogDensityAtBaseHeight = value; break;
        case EEnvironmentField::FogBaseHeight: state.FogBaseHeight = value; break;
        case EEnvironmentField::FogHeightFalloffPerUnit: state.FogHeightFalloffPerUnit = value; break;
        case EEnvironmentField::FogMaxDistanceMeters: state.FogMaxDistanceMeters = value; break;
        case EEnvironmentField::FogAmbientScale: state.FogAmbientScale = value; break;
        case EEnvironmentField::CloudLayerEnabled: state.bCloudLayerEnabled = value >= 0.5f; break;
        case EEnvironmentField::CloudLayerCoverage: state.CloudLayerCoverage = value; break;
        case EEnvironmentField::SunAltitude: state.SunAltitudeDegrees = value; break;
        case EEnvironmentField::SunAzimuth: state.SunAzimuthDegrees = value; break;
        case EEnvironmentField::TimeOfDayHours: state.TimeOfDayHours = value; break;
        case EEnvironmentField::Wetness: state.Wetness = value; break;
        case EEnvironmentField::FogTintR: state.FogTint[0] = value; break;
        case EEnvironmentField::FogTintG: state.FogTint[1] = value; break;
        case EEnvironmentField::FogTintB: state.FogTint[2] = value; break;
        case EEnvironmentField::FogSkyBlend: state.FogSkyBlend = value; break;
        case EEnvironmentField::FogReferenceElevation: state.FogReferenceElevationDegrees = value; break;
        default: break;
        }
    }
} // namespace NorvesLib::Core::Environment

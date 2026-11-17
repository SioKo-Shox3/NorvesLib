// 太陽の高度・方位と夜の光（月明かりと露出の下限）を計算し、設定を JSON で読み書きする。
#include "Environment/SunPath.h"

#include "EnvironmentDataFile.h"
#include "Logging/LogMacros.h"
#include "Rendering/AutoExposure.h"
#include "Text/JsonDocument.h"
#include "Text/JsonWriter.h"

#include <algorithm>
#include <cmath>

namespace NorvesLib::Core::Environment
{
    namespace
    {
        constexpr double Pi = 3.14159265358979323846;
        constexpr double DegreesToRadians = Pi / 180.0;
        constexpr double RadiansToDegrees = 180.0 / Pi;

        float WrapAzimuth(double degrees)
        {
            double wrapped = std::fmod(degrees, 360.0);
            if (wrapped < 0.0)
            {
                wrapped += 360.0;
            }
            const float result = static_cast<float>(wrapped);
            return result >= 360.0f ? 0.0f : result;
        }

        bool JsonError(Container::String* pOutError, const TCHAR* message)
        {
            if (pOutError)
            {
                *pOutError = message;
            }
            return false;
        }

        bool IsNumberInRange(const JsonValue& value, double minimum, double maximum)
        {
            return value.IsNumber() && std::isfinite(value.AsNumber()) &&
                   value.AsNumber() >= minimum && value.AsNumber() <= maximum;
        }

        bool ReadSunPathRoot(const JsonValue& root, SunPathSettings& inOutSettings, Container::String* pOutError)
        {
            if (!root.IsObject())
            {
                return JsonError(pOutError, _T("太陽軌道の JSON の根はオブジェクトである必要があります"));
            }

            SunPathSettings settings;
            for (size_t index = 0; index < root.GetObjectSize(); ++index)
            {
                const Container::String& key = root.GetMemberName(index);
                const JsonValue value = root.GetMemberValue(index);
                if (key == _T("formatVersion"))
                {
                    if (!IsNumberInRange(value, SunPathFormatVersion, SunPathFormatVersion))
                    {
                        return JsonError(pOutError, _T("太陽軌道の JSON の formatVersion は数値の 1 が必要です"));
                    }
                }
                else if (key == _T("latitudeDegrees"))
                {
                    if (!IsNumberInRange(value, -90.0, 90.0))
                    {
                        return JsonError(pOutError, _T("latitudeDegrees は -90～90 度の有限な数値が必要です"));
                    }
                    settings.LatitudeDegrees = static_cast<float>(value.AsNumber());
                }
                else if (key == _T("dayOfYear"))
                {
                    if (!value.IsIntegerLiteral() || !IsNumberInRange(value, 1.0, 366.0))
                    {
                        return JsonError(pOutError, _T("dayOfYear は 1～366 の整数リテラルが必要です"));
                    }
                    settings.DayOfYear = static_cast<int32_t>(value.AsNumber());
                }
                else if (key == _T("northAzimuthOffsetDegrees"))
                {
                    if (!IsNumberInRange(value, -360.0, 360.0))
                    {
                        return JsonError(pOutError, _T("northAzimuthOffsetDegrees は -360～360 度の有限な数値が必要です"));
                    }
                    settings.NorthAzimuthOffsetDegrees = static_cast<float>(value.AsNumber());
                }
                else
                {
                    const Container::String message = Container::String(_T("太陽軌道の JSON: '")) +
                                                      key + _T("' は未知のキーのため無視します");
#ifdef UNICODE
                    LOG_WARNING("%ls", message.c_str());
#else
                    LOG_WARNING("%s", message.c_str());
#endif
                }
            }
            inOutSettings = settings;
            return true;
        }

        bool ReadNightLightRoot(const JsonValue& root, NightLightSettings& inOutSettings, Container::String* pOutError)
        {
            if (!root.IsObject())
            {
                return JsonError(pOutError, _T("夜の光の JSON の根はオブジェクトである必要があります"));
            }

            NightLightSettings settings;
            for (size_t index = 0; index < root.GetObjectSize(); ++index)
            {
                const Container::String& key = root.GetMemberName(index);
                const JsonValue value = root.GetMemberValue(index);
                if (key == _T("formatVersion"))
                {
                    if (!IsNumberInRange(value, NightLightFormatVersion, NightLightFormatVersion))
                    {
                        return JsonError(pOutError, _T("夜の光の JSON の formatVersion は数値の 1 が必要です"));
                    }
                }
                else if (key == _T("moonIlluminanceLux"))
                {
                    if (!IsNumberInRange(value, 0.0, 10.0))
                    {
                        return JsonError(pOutError, _T("moonIlluminanceLux は 0～10 の有限な数値が必要です"));
                    }
                    settings.MoonIlluminanceLux = static_cast<float>(value.AsNumber());
                }
                else if (key == _T("moonColor"))
                {
                    if (!value.IsArray() || value.GetArraySize() != 3)
                    {
                        return JsonError(pOutError, _T("moonColor は 0～4 の数値を 3 つ持つ配列が必要です"));
                    }
                    for (size_t channel = 0; channel < 3; ++channel)
                    {
                        const JsonValue component = value.GetArrayElement(channel);
                        if (!IsNumberInRange(component, 0.0, 4.0))
                        {
                            return JsonError(pOutError, _T("moonColor は 0～4 の有限な数値を 3 つ持つ配列が必要です"));
                        }
                        settings.MoonColor[channel] = static_cast<float>(component.AsNumber());
                    }
                }
                else if (key == _T("moonCastShadows"))
                {
                    if (!value.IsBoolean())
                    {
                        return JsonError(pOutError, _T("moonCastShadows は真偽値が必要です"));
                    }
                    settings.bMoonCastShadows = value.AsBool();
                }
                else if (key == _T("moonRampStartSunAltitudeDegrees"))
                {
                    if (!IsNumberInRange(value, -20.0, 10.0))
                    {
                        return JsonError(pOutError, _T("moonRampStartSunAltitudeDegrees は -20～10 度の有限な数値が必要です"));
                    }
                    settings.MoonRampStartSunAltitudeDegrees = static_cast<float>(value.AsNumber());
                }
                else if (key == _T("moonRampEndSunAltitudeDegrees"))
                {
                    if (!IsNumberInRange(value, -20.0, 10.0))
                    {
                        return JsonError(pOutError, _T("moonRampEndSunAltitudeDegrees は -20 度以上の有限な数値が必要です"));
                    }
                    settings.MoonRampEndSunAltitudeDegrees = static_cast<float>(value.AsNumber());
                }
                else if (key == _T("moonMinimumAltitudeDegrees"))
                {
                    if (!IsNumberInRange(value, 0.0, 90.0))
                    {
                        return JsonError(pOutError, _T("moonMinimumAltitudeDegrees は 0～90 度の有限な数値が必要です"));
                    }
                    settings.MoonMinimumAltitudeDegrees = static_cast<float>(value.AsNumber());
                }
                else if (key == _T("nightMinEV100"))
                {
                    if (!IsNumberInRange(value, -10.0, 20.0))
                    {
                        return JsonError(pOutError, _T("nightMinEV100 は -10～20 の有限な数値が必要です"));
                    }
                    settings.NightMinEV100 = static_cast<float>(value.AsNumber());
                }
                else
                {
                    const Container::String message = Container::String(_T("夜の光の JSON の '")) +
                                                      key + _T("' は未知のキーです");
                    return JsonError(pOutError, message.c_str());
                }
            }
            if (!(settings.MoonRampEndSunAltitudeDegrees < settings.MoonRampStartSunAltitudeDegrees))
            {
                return JsonError(pOutError, _T("moonRampEndSunAltitudeDegrees は moonRampStartSunAltitudeDegrees より小さい値が必要です"));
            }
            inOutSettings = settings;
            return true;
        }
    } // 無名名前空間

    SunAltAz ComputeSunAltAz(float hours, float dayOfYear, float latitudeDegrees, float northAzimuthOffsetDegrees)
    {
        if (!std::isfinite(hours) || !std::isfinite(dayOfYear) ||
            !std::isfinite(latitudeDegrees) || !std::isfinite(northAzimuthOffsetDegrees))
        {
            return {};
        }

        const double latitude = latitudeDegrees < -90.0f ? -90.0 :
                                (latitudeDegrees > 90.0f ? 90.0 : static_cast<double>(latitudeDegrees));
        const double phi = latitude * DegreesToRadians;
        const double declination = 23.44 * DegreesToRadians *
                                   std::sin(2.0 * Pi * (284.0 + static_cast<double>(dayOfYear)) / 365.0);
        const double hourAngle = 15.0 * (static_cast<double>(hours) - 12.0) * DegreesToRadians;
        const double sinPhi = std::sin(phi);
        const double cosPhi = std::cos(phi);
        const double sinDeclination = std::sin(declination);
        const double cosDeclination = std::cos(declination);
        const double cosHourAngle = std::cos(hourAngle);
        const double sinAltitude = sinPhi * sinDeclination + cosPhi * cosDeclination * cosHourAngle;
        const double clampedSinAltitude = sinAltitude < -1.0 ? -1.0 : (sinAltitude > 1.0 ? 1.0 : sinAltitude);
        const double azimuth = std::atan2(-std::sin(hourAngle) * cosDeclination,
                                          sinDeclination * cosPhi - cosDeclination * sinPhi * cosHourAngle) *
                               RadiansToDegrees;
        // 大きな有限オフセットでも方位の小数部分を保つため、加算前に周期を取り除く。
        const double offset = std::fmod(static_cast<double>(northAzimuthOffsetDegrees), 360.0);
        return {static_cast<float>(std::asin(clampedSinAltitude) * RadiansToDegrees),
                WrapAzimuth(azimuth + offset)};
    }

    SunAltAz ComputeSunAltAz(float hours, const SunPathSettings& settings)
    {
        return ComputeSunAltAz(hours, static_cast<float>(settings.DayOfYear),
                               settings.LatitudeDegrees, settings.NorthAzimuthOffsetDegrees);
    }

    Container::String SunPathToJson(const SunPathSettings& settings)
    {
        JsonWriter writer(true);
        writer.BeginObject();
        writer.WriteNumber("formatVersion", SunPathFormatVersion);
        writer.WriteNumber("latitudeDegrees", settings.LatitudeDegrees);
        writer.WriteInt64("dayOfYear", settings.DayOfYear);
        writer.WriteNumber("northAzimuthOffsetDegrees", settings.NorthAzimuthOffsetDegrees);
        writer.EndObject();
        return writer.IsComplete() ? writer.ToString() : Container::String{};
    }

    bool SunPathFromJson(const Container::String& text, SunPathSettings& inOutSettings, Container::String* pOutError)
    {
        if (pOutError)
        {
            pOutError->clear();
        }
        JsonDocument document;
        if (!JsonDocument::TryParse(text, document))
        {
            return JsonError(pOutError, _T("太陽軌道の JSON の構文が不正です"));
        }
        return ReadSunPathRoot(document.GetRoot(), inOutSettings, pOutError);
    }

    bool SunPathFromJsonUtf8(Container::Span<const uint8_t> text, SunPathSettings& inOutSettings,
                             Container::String* pOutError)
    {
        if (pOutError)
        {
            pOutError->clear();
        }
        JsonDocument document;
        if (!JsonDocument::TryParseUtf8(text, document))
        {
            return JsonError(pOutError, _T("太陽軌道の UTF-8 または JSON の構文が不正です"));
        }
        return ReadSunPathRoot(document.GetRoot(), inOutSettings, pOutError);
    }

    bool LoadSunPath(const Container::String& rootDirectory, SunPathSettings& inOutSettings,
                     Container::String* pOutError)
    {
        Container::VariableArray<uint8_t> bytes;
        return ReadEnvironmentDataFile(rootDirectory, SunPathRelativePath, bytes, pOutError) &&
               SunPathFromJsonUtf8({bytes.data(), bytes.size()}, inOutSettings, pOutError);
    }

    Container::String NightLightToJson(const NightLightSettings& settings)
    {
        JsonWriter writer(true);
        writer.BeginObject();
        writer.WriteNumber("formatVersion", NightLightFormatVersion);
        writer.WriteNumber("moonIlluminanceLux", settings.MoonIlluminanceLux);
        writer.BeginArray("moonColor");
        for (float channel : settings.MoonColor)
        {
            writer.WriteNumberElement(channel);
        }
        writer.EndArray();
        writer.WriteBool("moonCastShadows", settings.bMoonCastShadows);
        writer.WriteNumber("moonRampStartSunAltitudeDegrees", settings.MoonRampStartSunAltitudeDegrees);
        writer.WriteNumber("moonRampEndSunAltitudeDegrees", settings.MoonRampEndSunAltitudeDegrees);
        writer.WriteNumber("moonMinimumAltitudeDegrees", settings.MoonMinimumAltitudeDegrees);
        writer.WriteNumber("nightMinEV100", settings.NightMinEV100);
        writer.EndObject();
        return writer.IsComplete() ? writer.ToString() : Container::String{};
    }

    bool NightLightFromJson(const Container::String& text, NightLightSettings& inOutSettings,
                            Container::String* pOutError)
    {
        if (pOutError)
        {
            pOutError->clear();
        }
        JsonDocument document;
        if (!JsonDocument::TryParse(text, document))
        {
            return JsonError(pOutError, _T("夜の光の JSON の構文が不正です"));
        }
        return ReadNightLightRoot(document.GetRoot(), inOutSettings, pOutError);
    }

    bool NightLightFromJsonUtf8(Container::Span<const uint8_t> text, NightLightSettings& inOutSettings,
                                Container::String* pOutError)
    {
        if (pOutError)
        {
            pOutError->clear();
        }
        JsonDocument document;
        if (!JsonDocument::TryParseUtf8(text, document))
        {
            return JsonError(pOutError, _T("夜の光の UTF-8 または JSON の構文が不正です"));
        }
        return ReadNightLightRoot(document.GetRoot(), inOutSettings, pOutError);
    }

    bool LoadNightLight(const Container::String& rootDirectory, NightLightSettings& inOutSettings,
                        Container::String* pOutError)
    {
        Container::VariableArray<uint8_t> bytes;
        return ReadEnvironmentDataFile(rootDirectory, NightLightRelativePath, bytes, pOutError) &&
               NightLightFromJsonUtf8({bytes.data(), bytes.size()}, inOutSettings, pOutError);
    }

    NightLightState EvaluateNightLight(const NightLightSettings& settings, float sunAltitudeDegrees,
                                       float sunAzimuthDegrees)
    {
        if (!std::isfinite(sunAltitudeDegrees) || !std::isfinite(sunAzimuthDegrees) ||
            !std::isfinite(settings.MoonIlluminanceLux) ||
            !std::isfinite(settings.MoonColor[0]) || !std::isfinite(settings.MoonColor[1]) ||
            !std::isfinite(settings.MoonColor[2]) ||
            !std::isfinite(settings.MoonRampStartSunAltitudeDegrees) ||
            !std::isfinite(settings.MoonRampEndSunAltitudeDegrees) ||
            !std::isfinite(settings.MoonMinimumAltitudeDegrees) || !std::isfinite(settings.NightMinEV100) ||
            !(settings.MoonRampEndSunAltitudeDegrees < settings.MoonRampStartSunAltitudeDegrees) ||
            settings.MoonMinimumAltitudeDegrees < 0.0f || settings.MoonMinimumAltitudeDegrees > 90.0f)
        {
            return {};
        }

        const double start = static_cast<double>(settings.MoonRampStartSunAltitudeDegrees);
        const double end = static_cast<double>(settings.MoonRampEndSunAltitudeDegrees);
        const double x = std::clamp((start - static_cast<double>(sunAltitudeDegrees)) / (start - end), 0.0, 1.0);
        NightLightState state;
        state.Weight = static_cast<float>(x * x * (3.0 - 2.0 * x));
        state.MoonIlluminanceLux = settings.MoonIlluminanceLux * state.Weight;
        state.MoonAltitudeDegrees = static_cast<float>(
            std::clamp(-static_cast<double>(sunAltitudeDegrees),
                       static_cast<double>(settings.MoonMinimumAltitudeDegrees), 90.0));
        const double sunAzimuth = std::fmod(static_cast<double>(sunAzimuthDegrees), 360.0);
        state.MoonAzimuthDegrees = WrapAzimuth(sunAzimuth + 180.0);
        for (size_t channel = 0; channel < 3; ++channel)
        {
            state.MoonColor[channel] = settings.MoonColor[channel];
        }
        state.bMoonCastShadows = settings.bMoonCastShadows;
        state.bOverrideMinEV100 = state.Weight > 0.0f;
        const float dayMinEV100 = Rendering::AutoExposureSettings{}.MinEV100;
        state.MinEV100 = dayMinEV100 + (settings.NightMinEV100 - dayMinEV100) * state.Weight;
        return state;
    }
} // 名前空間 NorvesLib::Core::Environment

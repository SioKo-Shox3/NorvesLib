// 太陽の高度・方位の純計算と軌道設定の JSON 読み書き。
#include "Environment/SunPath.h"

#include "EnvironmentDataFile.h"
#include "Logging/LogMacros.h"
#include "Text/JsonDocument.h"
#include "Text/JsonWriter.h"

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
} // 名前空間 NorvesLib::Core::Environment

// 地方太陽時・日付・緯度から太陽の高度と方位を求め、軌道設定を JSON で読み書きする。
#pragma once

#include "Container/Span.h"
#include "Container/String.h"

#include <cstdint>

namespace NorvesLib::Core::Environment
{
    /** @brief 太陽の高度とエンジン座標での方位。 */
    struct SunAltAz
    {
        float AltitudeDegrees = 0.0f; // 地平からの高度（度、[-90, 90]）。地平の下は負のまま返す。
        float AzimuthDegrees = 0.0f;  // SkyAtmosphereParameters::SunAzimuthDegrees と同じ方位（度、[0, 360)）。
    };

    /**
     * @brief 地方太陽時の時刻（時間）・年内の日番号・緯度から太陽の位置を返す。
     *
     * 方位は北から東回りの角度に northAzimuthOffsetDegrees を加え、[0, 360) に巻く。
     * 北オフセットは北を指すエンジンの方位で、既定の 0 度では北 = +X、東 = +Z（Y 上）。
     * 方向は (cos 高度 cos 方位, sin 高度, cos 高度 sin 方位) となる。
     * 緯度は [-90, 90] に挟み、地平下の高度も返す。入力に非有限値があれば {0, 0} を返す。
     */
    SunAltAz ComputeSunAltAz(float hours, float dayOfYear, float latitudeDegrees, float northAzimuthOffsetDegrees);

    /** @brief 太陽の軌道を決める日付・緯度と北の向き。 */
    struct SunPathSettings
    {
        float LatitudeDegrees = 55.0f;          // 仮の既定は北緯 55 度。地域に応じてデータで指定する。
        int32_t DayOfYear = 172;                // 年内の日番号（1～366）。
        float NorthAzimuthOffsetDegrees = 0.0f; // 北がエンジンの方位で何度にあるか。
    };

    inline constexpr uint32_t SunPathFormatVersion = 1;
    inline constexpr const TCHAR* SunPathRelativePath = _T("Assets/Data/Environment/SunPath.json");

    SunAltAz ComputeSunAltAz(float hours, const SunPathSettings& settings);
    Container::String SunPathToJson(const SunPathSettings& settings);

    /** @brief 欠けた項目は既定値で補う。不正な項目があれば設定を変更せず失敗する。 */
    bool SunPathFromJson(const Container::String& text, SunPathSettings& inOutSettings,
                         Container::String* pOutError = nullptr);
    bool SunPathFromJsonUtf8(Container::Span<const uint8_t> text, SunPathSettings& inOutSettings,
                             Container::String* pOutError = nullptr);

    /** @brief 根が空なら作業ディレクトリから読み込む。失敗時は設定を変更しない。 */
    bool LoadSunPath(const Container::String& rootDirectory, SunPathSettings& inOutSettings,
                     Container::String* pOutError = nullptr);
} // 名前空間 NorvesLib::Core::Environment

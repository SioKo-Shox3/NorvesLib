// 地方太陽時・日付・緯度から太陽の位置を求め、軌道と夜の光（月明かりと露出の下限）を扱う。
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

    /** @brief 太陽が地平線の下にあるときの月明かりと露出の下限。 */
    struct NightLightSettings
    {
        float MoonIlluminanceLux = 0.2f;              // 光に垂直な面の照度（lux、[0, 10]）。
        float MoonColor[3] = {0.75f, 0.85f, 1.0f};  // 青白（線形 RGB、各 [0, 4]）。
        bool bMoonCastShadows = false;
        float MoonRampStartSunAltitudeDegrees = 0.0f; // この太陽高度で強さ 0（度、[-20, 10]）。
        float MoonRampEndSunAltitudeDegrees = -6.0f;  // この太陽高度で既定の強さ（度、[-20, start)）。
        float MoonMinimumAltitudeDegrees = 30.0f;     // 月明かりの来る向きの最低の高度（度、[0, 90]）。
        float NightMinEV100 = -1.0f;                 // 夜の自動露出の下限（EV100、[-10, 20]。仮の値）。
    };

    inline constexpr uint32_t NightLightFormatVersion = 1;
    inline constexpr const TCHAR* NightLightRelativePath = _T("Assets/Data/Environment/NightLight.json");

    Container::String NightLightToJson(const NightLightSettings& settings);
    /** @brief 欠けた項目は既定値で補う。不正な項目があれば設定を変更せず失敗する。 */
    bool NightLightFromJson(const Container::String& text, NightLightSettings& inOutSettings,
                            Container::String* pOutError = nullptr);
    bool NightLightFromJsonUtf8(Container::Span<const uint8_t> text, NightLightSettings& inOutSettings,
                                Container::String* pOutError = nullptr);
    /** @brief 根が空なら作業ディレクトリから読み込む。失敗時は設定を変更しない。 */
    bool LoadNightLight(const Container::String& rootDirectory, NightLightSettings& inOutSettings,
                        Container::String* pOutError = nullptr);

    /** @brief 太陽高度・方位から求めた月明かりと露出の下限。 */
    struct NightLightState
    {
        float Weight = 0.0f;              // 太陽高度から求めた夜の重み（[0, 1]）。
        float MoonIlluminanceLux = 0.0f;  // 既定の照度に重みを掛けた値。
        float MoonAltitudeDegrees = 0.0f;
        float MoonAzimuthDegrees = 0.0f;
        float MoonColor[3] = {1.0f, 1.0f, 1.0f};
        bool bMoonCastShadows = false;
        bool bOverrideMinEV100 = false;   // 重みが 0 より大きいとき true。
        float MinEV100 = -4.0f;           // 昼の既定の下限から夜の下限へ重みで寄せた値。
    };

    NightLightState EvaluateNightLight(const NightLightSettings& settings, float sunAltitudeDegrees,
                                       float sunAzimuthDegrees);
} // 名前空間 NorvesLib::Core::Environment

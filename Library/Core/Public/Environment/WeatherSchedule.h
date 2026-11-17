// 天候のプリセットと重みつき遷移表を検証し、JSON で読み書きする。
#pragma once

#include "Container/Span.h"
#include "Container/String.h"
#include "Container/VariableArray.h"

#include <cstdint>

namespace NorvesLib::Core::Environment
{
    /// 24 時間を 15 分のセルに分けた数。
    inline constexpr uint32_t WeatherCellsPerDay = 96;
    inline constexpr float WeatherCellMinutes = 15.0f;
    inline constexpr uint32_t WeatherScheduleMaxPresets = 16;
    inline constexpr uint32_t WeatherScheduleFormatVersion = 1;
    inline constexpr const TCHAR* WeatherScheduleRelativePath = _T("Assets/Data/Environment/WeatherSchedule.json");

    /** @brief 前のセルから次のセルへの重みつき遷移。自分への遷移も 1 本として持つ。 */
    struct WeatherTransition
    {
        uint32_t From = 0;
        uint32_t To = 0;
        uint32_t Weight = 1; // 1 以上の整数。
    };

    /** @brief プリセット名と遷移表、境界での補間の長さ。 */
    struct WeatherScheduleSettings
    {
        Container::VariableArray<Container::String> PresetNames;
        Container::VariableArray<WeatherTransition> Transitions; // (From, To) の昇順。
        float BlendMinutes = 10.0f; // セルの境界を中心とする補間の長さ（分、(0, 15]）。
    };

    /// 仮の天候プリセットと遷移表を返す。
    WeatherScheduleSettings MakeDefaultWeatherScheduleSettings();

    /// 名前・遷移表・96 歩で戻る条件・補間の長さを検証する。
    bool ValidateWeatherScheduleSettings(const WeatherScheduleSettings& settings, Container::String* pOutError = nullptr);

    /// 範囲内の 2 つのプリセット間に遷移があれば true を返す。
    bool IsWeatherTransitionAllowed(const WeatherScheduleSettings& settings, uint32_t from, uint32_t to);

    /// 名前に一致するプリセットの番号を探す。
    bool FindWeatherPresetIndex(const WeatherScheduleSettings& settings, const Container::String& name, uint32_t& outIndex);

    /// 設定を JSON v1 の文字列にする（整形つき）。不正な設定や重みが JSON の上限を超える場合は空文字列を返す。
    Container::String WeatherScheduleToJson(const WeatherScheduleSettings& settings);

    /// 欠けた項目は既定値で補う。不正な項目があれば設定を変更せず失敗する。
    bool WeatherScheduleFromJson(const Container::String& text, WeatherScheduleSettings& inOutSettings,
                                 Container::String* pOutError = nullptr);
    bool WeatherScheduleFromJsonUtf8(Container::Span<const uint8_t> text, WeatherScheduleSettings& inOutSettings,
                                     Container::String* pOutError = nullptr);

    /// 根が空なら作業ディレクトリから読み込む。失敗時は設定を変更しない。
    bool LoadWeatherSchedule(const Container::String& rootDirectory, WeatherScheduleSettings& inOutSettings,
                             Container::String* pOutError = nullptr);
} // 名前空間 NorvesLib::Core::Environment

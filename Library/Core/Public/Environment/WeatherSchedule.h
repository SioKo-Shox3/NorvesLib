// 天候のプリセットと重みつき遷移表を検証し、JSON で読み書きする。
#pragma once

#include "Container/Span.h"
#include "Container/String.h"
#include "Container/VariableArray.h"
#include "Environment/EnvironmentState.h"

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
        uint32_t Weight = 1; // 1～1000000 の整数。
    };

    /** @brief プリセット名と遷移表、境界での補間の長さ。 */
    struct WeatherScheduleSettings
    {
        Container::VariableArray<Container::String> PresetNames;
        Container::VariableArray<WeatherTransition> Transitions; // (From, To) の昇順。
        float BlendMinutes = 10.0f; // セルの境界を中心とする補間の長さ（分、(0, 15]）。
    };

    /** 種から作った 24 時間分の天候の並び（15 分のセル 96 個。値はプリセットの番号）。 */
    struct WeatherTrack
    {
        uint8_t Cells[WeatherCellsPerDay] = {};
    };

    /** ある時刻の天候。FromPreset と ToPreset を Blend（0～1）で混ぜ、境界の外では From == To・Blend = 0。 */
    struct WeatherSample
    {
        uint32_t FromPreset = 0;
        uint32_t ToPreset = 0;
        float Blend = 0.0f;
    };

    /// 設定と種から環状の並びを作る。不正な設定では出力を変えず false を返す。
    bool MakeWeatherTrack(const WeatherScheduleSettings& settings, uint64_t seed, WeatherTrack& outTrack);

    /// 時刻を 24 時間に巻き、セルの境界を中心に補間した天候を返す。
    WeatherSample SampleWeatherTrack(const WeatherScheduleSettings& settings, const WeatherTrack& track, double timeHours);

    /// 種と時刻から天候を返す。不正な設定では既定の WeatherSample を返す。
    WeatherSample WeatherAt(const WeatherScheduleSettings& settings, uint64_t seed, double timeHours);

    /// 設定の名前順にプリセットを読む。失敗時は出力を変えない。
    bool LoadWeatherPresets(const Container::String& rootDirectory, const WeatherScheduleSettings& settings,
                            Container::VariableArray<EnvironmentTarget>& outPresets, Container::String* pOutError = nullptr);

    /// 濡れを除く欄を混ぜ、即時に適用できる遷移先を作る。
    void BlendWeatherPresets(const EnvironmentTarget& from, const EnvironmentTarget& to, float blend,
                             EnvironmentTarget& outTarget);

    /// 設定とプリセット数を検証し、種と時刻から混ぜた遷移先を返す。失敗時は出力を変えない。
    bool EvaluateWeather(const WeatherScheduleSettings& settings, const Container::VariableArray<EnvironmentTarget>& presets,
                         uint64_t seed, double timeHours, EnvironmentTarget& outTarget);

    /// 仮の天候プリセットと遷移表を返す。
    WeatherScheduleSettings MakeDefaultWeatherScheduleSettings();

    /// 名前・遷移表・96 歩で戻る条件・補間の長さを検証する。
    bool ValidateWeatherScheduleSettings(const WeatherScheduleSettings& settings, Container::String* pOutError = nullptr);

    /// 範囲内の 2 つのプリセット間に遷移があれば true を返す。
    bool IsWeatherTransitionAllowed(const WeatherScheduleSettings& settings, uint32_t from, uint32_t to);

    /// 名前に一致するプリセットの番号を探す。
    bool FindWeatherPresetIndex(const WeatherScheduleSettings& settings, const Container::String& name, uint32_t& outIndex);

    /// 設定を JSON v1 の文字列にする（整形つき）。不正な設定は空文字列を返す。
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

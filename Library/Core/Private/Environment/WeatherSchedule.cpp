// 天候の予定表の検証と JSON 読み書き。
#include "Environment/WeatherSchedule.h"

#include "Environment/EnvironmentState.h"
#include "EnvironmentDataFile.h"
#include "Logging/LogMacros.h"
#include "Text/JsonDocument.h"
#include "Text/JsonWriter.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace NorvesLib::Core::Environment
{
    namespace
    {
        constexpr uint32_t MaxJsonTransitionWeight = 1000000;

        // セル番号と用途を混ぜ、呼び出し順に依存しない SplitMix64 の値を作る。
        uint64_t HashWeatherCell(uint64_t seed, uint32_t cell, uint64_t purpose)
        {
            uint64_t z = seed ^ (static_cast<uint64_t>(cell) * 0xD1B54A32D192ED03ULL) ^ purpose;
            z += 0x9E3779B97F4A7C15ULL;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
            z ^= z >> 31;
            return z;
        }

        float BlendWeatherYaw(float from, float to, float blend)
        {
            double delta = std::fmod(static_cast<double>(to) - from, 360.0);
            if (delta > 180.0)
            {
                delta -= 360.0;
            }
            else if (delta < -180.0)
            {
                delta += 360.0;
            }
            double yaw = std::fmod(static_cast<double>(from) + delta * blend, 360.0);
            if (yaw < 0.0)
            {
                yaw += 360.0;
            }
            const float result = static_cast<float>(yaw);
            return result >= 360.0f ? 0.0f : result;
        }

        bool ReportError(Container::String* pOutError, const TCHAR* message)
        {
            if (pOutError)
            {
                *pOutError = message;
            }
            return false;
        }

        void WarnUnknownKey(const Container::String& key)
        {
            const Container::String message = Container::String(_T("天候予定表の JSON: '")) +
                                              key + _T("' は未知のキーのため無視します");
#ifdef UNICODE
            LOG_WARNING("%ls", message.c_str());
#else
            LOG_WARNING("%s", message.c_str());
#endif
        }

        bool TransitionLess(const WeatherTransition& left, const WeatherTransition& right)
        {
            return left.From < right.From || (left.From == right.From && left.To < right.To);
        }

        // 検証済みの遷移表から、指定したプリセットの 1 歩後に到達できる集合を返す。
        uint32_t GetWeatherSuccessorMask(const WeatherScheduleSettings& settings, uint32_t from)
        {
            uint32_t mask = 0;
            for (const WeatherTransition& transition : settings.Transitions)
            {
                if (transition.From == from)
                {
                    mask |= uint32_t{1} << transition.To;
                }
            }
            return mask;
        }

        bool ReadPresetNames(const JsonValue& value, WeatherScheduleSettings& settings, Container::String* pOutError)
        {
            if (!value.IsArray() || value.GetArraySize() == 0 ||
                value.GetArraySize() > WeatherScheduleMaxPresets)
            {
                return ReportError(pOutError, _T("presets は 1～16 個の名前の配列が必要です"));
            }

            settings.PresetNames.clear();
            for (size_t index = 0; index < value.GetArraySize(); ++index)
            {
                const JsonValue name = value.GetArrayElement(index);
                if (!name.IsString())
                {
                    return ReportError(pOutError, _T("presets の各要素は文字列が必要です"));
                }
                settings.PresetNames.push_back(name.AsString());
            }
            return true;
        }

        bool ReadTransitions(const JsonValue& value, WeatherScheduleSettings& settings, Container::String* pOutError)
        {
            if (!value.IsObject())
            {
                return ReportError(pOutError, _T("transitions はオブジェクトが必要です"));
            }

            settings.Transitions.clear();
            uint32_t seenFrom = 0;
            for (size_t row = 0; row < value.GetObjectSize(); ++row)
            {
                uint32_t from = 0;
                if (!FindWeatherPresetIndex(settings, value.GetMemberName(row), from))
                {
                    return ReportError(pOutError, _T("遷移元の名前が presets にありません"));
                }
                const uint32_t fromBit = uint32_t{1} << from;
                if ((seenFrom & fromBit) != 0)
                {
                    return ReportError(pOutError, _T("同じ遷移元が transitions に重複しています"));
                }
                seenFrom |= fromBit;

                const JsonValue destinations = value.GetMemberValue(row);
                if (!destinations.IsObject())
                {
                    return ReportError(pOutError, _T("遷移元の値は遷移先と重みのオブジェクトが必要です"));
                }
                uint32_t seenTo = 0;
                for (size_t column = 0; column < destinations.GetObjectSize(); ++column)
                {
                    uint32_t to = 0;
                    if (!FindWeatherPresetIndex(settings, destinations.GetMemberName(column), to))
                    {
                        return ReportError(pOutError, _T("遷移先の名前が presets にありません"));
                    }
                    const uint32_t toBit = uint32_t{1} << to;
                    if ((seenTo & toBit) != 0)
                    {
                        return ReportError(pOutError, _T("同じ遷移元と遷移先の組が重複しています"));
                    }
                    seenTo |= toBit;

                    const JsonValue weight = destinations.GetMemberValue(column);
                    if (!weight.IsIntegerLiteral() || !std::isfinite(weight.AsNumber()) ||
                        weight.AsNumber() < 1.0 || weight.AsNumber() > MaxJsonTransitionWeight)
                    {
                        return ReportError(pOutError, _T("遷移の重みは 1～1000000 の整数リテラルが必要です"));
                    }
                    settings.Transitions.push_back({from, to, static_cast<uint32_t>(weight.AsNumber())});
                }
            }
            return true;
        }

        bool RemapDefaultTransitions(const WeatherScheduleSettings& defaults, WeatherScheduleSettings& settings,
                                     Container::String* pOutError)
        {
            settings.Transitions.clear();
            for (const WeatherTransition& transition : defaults.Transitions)
            {
                uint32_t from = 0;
                uint32_t to = 0;
                if (!FindWeatherPresetIndex(settings, defaults.PresetNames[transition.From], from) ||
                    !FindWeatherPresetIndex(settings, defaults.PresetNames[transition.To], to))
                {
                    return ReportError(pOutError, _T("既定の遷移に必要なプリセット名が presets にありません"));
                }
                settings.Transitions.push_back({from, to, transition.Weight});
            }
            return true;
        }

        bool ReadWeatherScheduleRoot(const JsonValue& root, WeatherScheduleSettings& inOutSettings,
                                     Container::String* pOutError)
        {
            if (!root.IsObject())
            {
                return ReportError(pOutError, _T("天候予定表の JSON の根はオブジェクトが必要です"));
            }

            const WeatherScheduleSettings defaults = MakeDefaultWeatherScheduleSettings();
            WeatherScheduleSettings settings = defaults;
            JsonValue presets;
            JsonValue transitions;
            bool bHasVersion = false;
            bool bHasBlend = false;
            bool bHasPresets = false;
            bool bHasTransitions = false;
            for (size_t index = 0; index < root.GetObjectSize(); ++index)
            {
                const Container::String& key = root.GetMemberName(index);
                const JsonValue value = root.GetMemberValue(index);
                if (key == _T("formatVersion"))
                {
                    if (bHasVersion || !value.IsNumber() || value.AsNumber() != WeatherScheduleFormatVersion)
                    {
                        return ReportError(pOutError, _T("formatVersion は重複しない数値の 1 が必要です"));
                    }
                    bHasVersion = true;
                }
                else if (key == _T("blendMinutes"))
                {
                    if (bHasBlend || !value.IsNumber() || !std::isfinite(value.AsNumber()) ||
                        value.AsNumber() <= 0.0 || value.AsNumber() > WeatherCellMinutes)
                    {
                        return ReportError(pOutError, _T("blendMinutes は重複せず 0 より大きく 15 以下の有限な数値が必要です"));
                    }
                    bHasBlend = true;
                    settings.BlendMinutes = static_cast<float>(value.AsNumber());
                }
                else if (key == _T("presets"))
                {
                    if (bHasPresets)
                    {
                        return ReportError(pOutError, _T("presets が重複しています"));
                    }
                    bHasPresets = true;
                    presets = value;
                }
                else if (key == _T("transitions"))
                {
                    if (bHasTransitions)
                    {
                        return ReportError(pOutError, _T("transitions が重複しています"));
                    }
                    bHasTransitions = true;
                    transitions = value;
                }
                else
                {
                    WarnUnknownKey(key);
                }
            }

            if (bHasPresets && !ReadPresetNames(presets, settings, pOutError))
            {
                return false;
            }
            if (bHasTransitions ? !ReadTransitions(transitions, settings, pOutError) :
                                  !RemapDefaultTransitions(defaults, settings, pOutError))
            {
                return false;
            }
            std::sort(settings.Transitions.begin(), settings.Transitions.end(), TransitionLess);
            if (!ValidateWeatherScheduleSettings(settings, pOutError))
            {
                return false;
            }
            inOutSettings = std::move(settings);
            return true;
        }

        // 検証済みの名前は ASCII のみなので、JSON ライターのキーへ写せる。
        void CopyPresetNameToKey(const Container::String& name, char (&key)[65])
        {
            for (size_t index = 0; index < name.size(); ++index)
            {
                key[index] = static_cast<char>(name[index]);
            }
            key[name.size()] = '\0';
        }
    } // 無名名前空間

    WeatherScheduleSettings MakeDefaultWeatherScheduleSettings()
    {
        WeatherScheduleSettings settings;
        settings.PresetNames.push_back(_T("clear"));
        settings.PresetNames.push_back(_T("overcast"));
        settings.PresetNames.push_back(_T("rain"));
        settings.PresetNames.push_back(_T("heavy_rain"));
        constexpr WeatherTransition transitions[] = {
            {0, 0, 12}, {0, 1, 2},
            {1, 0, 1}, {1, 1, 24}, {1, 2, 2},
            {2, 1, 2}, {2, 2, 10}, {2, 3, 1},
            {3, 2, 2}, {3, 3, 4}
        };
        for (const WeatherTransition& transition : transitions)
        {
            settings.Transitions.push_back(transition);
        }
        return settings;
    }

    bool ValidateWeatherScheduleSettings(const WeatherScheduleSettings& settings, Container::String* pOutError)
    {
        if (pOutError)
        {
            pOutError->clear();
        }
        if (settings.PresetNames.empty() || settings.PresetNames.size() > WeatherScheduleMaxPresets)
        {
            return ReportError(pOutError, _T("プリセットは 1～16 個が必要です"));
        }
        for (size_t index = 0; index < settings.PresetNames.size(); ++index)
        {
            Container::String path;
            if (!MakeEnvironmentPresetPath(settings.PresetNames[index], path))
            {
                return ReportError(pOutError, _T("プリセット名は英数字・'_'・'-' の 1～64 文字が必要です"));
            }
            for (size_t previous = 0; previous < index; ++previous)
            {
                if (settings.PresetNames[previous] == settings.PresetNames[index])
                {
                    return ReportError(pOutError, _T("プリセット名が重複しています"));
                }
            }
        }

        for (size_t index = 0; index < settings.Transitions.size(); ++index)
        {
            const WeatherTransition& transition = settings.Transitions[index];
            if (transition.From >= settings.PresetNames.size() || transition.To >= settings.PresetNames.size())
            {
                return ReportError(pOutError, _T("遷移元または遷移先の番号が範囲外です"));
            }
            if (transition.Weight == 0 || transition.Weight > MaxJsonTransitionWeight)
            {
                return ReportError(pOutError, _T("遷移の重みは 1～1000000 が必要です"));
            }
            if (index > 0 && !TransitionLess(settings.Transitions[index - 1], transition))
            {
                return ReportError(pOutError, _T("遷移は重複せず遷移元・遷移先の昇順に並べる必要があります"));
            }
        }

        uint32_t successors[WeatherScheduleMaxPresets] = {};
        const uint32_t presetCount = static_cast<uint32_t>(settings.PresetNames.size());
        for (uint32_t preset = 0; preset < presetCount; ++preset)
        {
            successors[preset] = GetWeatherSuccessorMask(settings, preset);
            if (successors[preset] == 0)
            {
                return ReportError(pOutError, _T("各プリセットに出ていく遷移が必要です"));
            }
        }
        for (uint32_t preset = 0; preset < presetCount; ++preset)
        {
            const uint32_t startBit = uint32_t{1} << preset;
            uint32_t reachable = startBit;
            for (uint32_t step = 0; step < WeatherCellsPerDay; ++step)
            {
                uint32_t next = 0;
                for (uint32_t current = 0; current < presetCount; ++current)
                {
                    if ((reachable & (uint32_t{1} << current)) != 0)
                    {
                        next |= successors[current];
                    }
                }
                reachable = next;
            }
            if ((reachable & startBit) == 0)
            {
                return ReportError(pOutError, _T("各プリセットからちょうど 96 歩で元へ戻る経路が必要です"));
            }
        }
        if (!std::isfinite(settings.BlendMinutes) || settings.BlendMinutes <= 0.0f ||
            settings.BlendMinutes > WeatherCellMinutes)
        {
            return ReportError(pOutError, _T("補間の長さは 0 より大きく 15 分以下の有限値が必要です"));
        }
        return true;
    }

    bool MakeWeatherTrack(const WeatherScheduleSettings& settings, uint64_t seed, WeatherTrack& outTrack)
    {
        if (!ValidateWeatherScheduleSettings(settings))
        {
            return false;
        }

        const uint32_t presetCount = static_cast<uint32_t>(settings.PresetNames.size());
        uint32_t weights[WeatherScheduleMaxPresets][WeatherScheduleMaxPresets] = {};
        uint32_t successors[WeatherScheduleMaxPresets] = {};
        for (const WeatherTransition& transition : settings.Transitions)
        {
            weights[transition.From][transition.To] = transition.Weight;
            successors[transition.From] |= uint32_t{1} << transition.To;
        }

        WeatherTrack track;
        track.Cells[0] = static_cast<uint8_t>(HashWeatherCell(seed, 0, 0xA0761D6478BD642FULL) % presetCount);
        // 残りの歩数でセル 0 へ戻れる集合だけを、次のセルの候補にする。
        uint32_t returnMasks[WeatherCellsPerDay + 1] = {};
        returnMasks[0] = uint32_t{1} << track.Cells[0];
        for (uint32_t steps = 1; steps <= WeatherCellsPerDay; ++steps)
        {
            for (uint32_t preset = 0; preset < presetCount; ++preset)
            {
                if ((successors[preset] & returnMasks[steps - 1]) != 0)
                {
                    returnMasks[steps] |= uint32_t{1} << preset;
                }
            }
        }
        for (uint32_t cell = 1; cell < WeatherCellsPerDay; ++cell)
        {
            const uint32_t from = track.Cells[cell - 1];
            const uint32_t candidates = successors[from] & returnMasks[WeatherCellsPerDay - cell];
            uint64_t totalWeight = 0;
            for (uint32_t to = 0; to < presetCount; ++to)
            {
                if ((candidates & (uint32_t{1} << to)) != 0)
                {
                    totalWeight += weights[from][to];
                }
            }
            if (totalWeight == 0)
            {
                return false;
            }
            uint64_t choice = HashWeatherCell(seed, cell, 0xE7037ED1A0B428DBULL) % totalWeight;
            for (uint32_t to = 0; to < presetCount; ++to)
            {
                if ((candidates & (uint32_t{1} << to)) == 0)
                {
                    continue;
                }
                if (choice < weights[from][to])
                {
                    track.Cells[cell] = static_cast<uint8_t>(to);
                    break;
                }
                choice -= weights[from][to];
            }
        }
        outTrack = track;
        return true;
    }

    WeatherSample SampleWeatherTrack(const WeatherScheduleSettings& settings, const WeatherTrack& track, double timeHours)
    {
        constexpr double minutesPerDay = WeatherCellsPerDay * static_cast<double>(WeatherCellMinutes);
        double minutes = timeHours * 60.0;
        if (std::isfinite(minutes))
        {
            minutes = std::fmod(minutes, minutesPerDay);
            if (minutes < 0.0)
            {
                minutes += minutesPerDay;
            }
            if (minutes >= minutesPerDay)
            {
                minutes = 0.0;
            }
        }
        else
        {
            minutes = 0.0;
        }

        const uint32_t cell = static_cast<uint32_t>(std::clamp(std::floor(minutes / WeatherCellMinutes),
                                                             0.0, static_cast<double>(WeatherCellsPerDay - 1)));
        WeatherSample sample{track.Cells[cell], track.Cells[cell], 0.0f};
        const double blendMinutes = settings.BlendMinutes;
        if (!std::isfinite(blendMinutes) || blendMinutes <= 0.0 || blendMinutes > WeatherCellMinutes)
        {
            return sample;
        }
        const double localMinutes = minutes - WeatherCellMinutes * cell;
        const double half = blendMinutes * 0.5;
        double progress = 0.0;
        if (localMinutes < half)
        {
            sample.FromPreset = track.Cells[(cell + WeatherCellsPerDay - 1) % WeatherCellsPerDay];
            progress = (localMinutes + half) / blendMinutes;
        }
        else if (localMinutes > WeatherCellMinutes - half)
        {
            sample.ToPreset = track.Cells[(cell + 1) % WeatherCellsPerDay];
            progress = (localMinutes - (WeatherCellMinutes - half)) / blendMinutes;
        }
        if (sample.FromPreset != sample.ToPreset)
        {
            sample.Blend = EvaluateTransitionCurve(ETransitionCurve::SmoothStep,
                                                  static_cast<float>(std::clamp(progress, 0.0, 1.0)));
        }
        return sample;
    }

    WeatherSample WeatherAt(const WeatherScheduleSettings& settings, uint64_t seed, double timeHours)
    {
        WeatherTrack track;
        return MakeWeatherTrack(settings, seed, track) ? SampleWeatherTrack(settings, track, timeHours) : WeatherSample{};
    }

    bool LoadWeatherPresets(const Container::String& rootDirectory, const WeatherScheduleSettings& settings,
                            Container::VariableArray<EnvironmentTarget>& outPresets, Container::String* pOutError)
    {
        if (pOutError)
        {
            pOutError->clear();
        }
        Container::VariableArray<EnvironmentTarget> presets;
        for (const Container::String& name : settings.PresetNames)
        {
            EnvironmentTarget target;
            if (!LoadEnvironmentPreset(rootDirectory, name, target, pOutError))
            {
                return false;
            }
            presets.push_back(target);
        }
        outPresets = std::move(presets);
        return true;
    }

    void BlendWeatherPresets(const EnvironmentTarget& from, const EnvironmentTarget& to, float blend,
                             EnvironmentTarget& outTarget)
    {
        blend = std::isfinite(blend) ? std::clamp(blend, 0.0f, 1.0f) : 0.0f;
        EnvironmentTarget target;
        target.Mask = (from.Mask | to.Mask) & ~EnvironmentFieldBit(EEnvironmentField::Wetness);
        for (uint32_t index = 0; index < EnvironmentFieldCount; ++index)
        {
            const EEnvironmentField field = static_cast<EEnvironmentField>(index);
            const EnvironmentFieldMask bit = EnvironmentFieldBit(field);
            if ((target.Mask & bit) == 0)
            {
                continue;
            }
            const bool bHasFrom = (from.Mask & bit) != 0;
            const bool bHasTo = (to.Mask & bit) != 0;
            const float fromValue = GetEnvironmentField(from.Values, field);
            const float toValue = GetEnvironmentField(to.Values, field);
            float value = bHasFrom ? fromValue : toValue;
            if (bHasFrom && bHasTo)
            {
                if (field == EEnvironmentField::WindDirectionYaw)
                {
                    value = BlendWeatherYaw(fromValue, toValue, blend);
                }
                else if (field == EEnvironmentField::CloudLayerEnabled)
                {
                    value = blend < 0.5f ? fromValue : toValue;
                }
                else
                {
                    value = fromValue + (toValue - fromValue) * blend;
                }
            }
            SetEnvironmentField(target.Values, field, value);
        }
        outTarget = target;
    }

    bool EvaluateWeather(const WeatherScheduleSettings& settings, const Container::VariableArray<EnvironmentTarget>& presets,
                         uint64_t seed, double timeHours, EnvironmentTarget& outTarget)
    {
        if (presets.size() != settings.PresetNames.size() || !ValidateWeatherScheduleSettings(settings))
        {
            return false;
        }
        const WeatherSample sample = WeatherAt(settings, seed, timeHours);
        BlendWeatherPresets(presets[sample.FromPreset], presets[sample.ToPreset], sample.Blend, outTarget);
        return true;
    }

    bool IsWeatherTransitionAllowed(const WeatherScheduleSettings& settings, uint32_t from, uint32_t to)
    {
        if (from >= settings.PresetNames.size() || to >= settings.PresetNames.size())
        {
            return false;
        }
        for (const WeatherTransition& transition : settings.Transitions)
        {
            if (transition.From == from && transition.To == to)
            {
                return true;
            }
        }
        return false;
    }

    bool FindWeatherPresetIndex(const WeatherScheduleSettings& settings, const Container::String& name, uint32_t& outIndex)
    {
        for (size_t index = 0; index < settings.PresetNames.size(); ++index)
        {
            if (settings.PresetNames[index] == name)
            {
                outIndex = static_cast<uint32_t>(index);
                return true;
            }
        }
        return false;
    }

    Container::String WeatherScheduleToJson(const WeatherScheduleSettings& settings)
    {
        if (!ValidateWeatherScheduleSettings(settings))
        {
            return {};
        }

        JsonWriter writer(true);
        writer.BeginObject();
        writer.WriteUInt64(_T("formatVersion"), WeatherScheduleFormatVersion);
        writer.WriteNumber(_T("blendMinutes"), settings.BlendMinutes);
        writer.BeginArray(_T("presets"));
        for (const Container::String& name : settings.PresetNames)
        {
            writer.WriteStringElement(name);
        }
        writer.EndArray();
        writer.BeginObject(_T("transitions"));
        size_t transitionIndex = 0;
        for (uint32_t from = 0; from < settings.PresetNames.size(); ++from)
        {
            char fromKey[65] = {};
            CopyPresetNameToKey(settings.PresetNames[from], fromKey);
            writer.BeginObject(fromKey);
            while (transitionIndex < settings.Transitions.size() && settings.Transitions[transitionIndex].From == from)
            {
                const WeatherTransition& transition = settings.Transitions[transitionIndex++];
                char toKey[65] = {};
                CopyPresetNameToKey(settings.PresetNames[transition.To], toKey);
                writer.WriteUInt64(toKey, transition.Weight);
            }
            writer.EndObject();
        }
        writer.EndObject();
        writer.EndObject();
        return writer.IsComplete() ? writer.ToString() : Container::String{};
    }

    bool WeatherScheduleFromJson(const Container::String& text, WeatherScheduleSettings& inOutSettings,
                                 Container::String* pOutError)
    {
        if (pOutError)
        {
            pOutError->clear();
        }
        JsonDocument document;
        if (!JsonDocument::TryParse(text, document))
        {
            return ReportError(pOutError, _T("天候予定表の JSON の構文が不正です"));
        }
        return ReadWeatherScheduleRoot(document.GetRoot(), inOutSettings, pOutError);
    }

    bool WeatherScheduleFromJsonUtf8(Container::Span<const uint8_t> text, WeatherScheduleSettings& inOutSettings,
                                     Container::String* pOutError)
    {
        if (pOutError)
        {
            pOutError->clear();
        }
        JsonDocument document;
        if (!JsonDocument::TryParseUtf8(text, document))
        {
            return ReportError(pOutError, _T("天候予定表の UTF-8 または JSON の構文が不正です"));
        }
        return ReadWeatherScheduleRoot(document.GetRoot(), inOutSettings, pOutError);
    }

    bool LoadWeatherSchedule(const Container::String& rootDirectory, WeatherScheduleSettings& inOutSettings,
                             Container::String* pOutError)
    {
        Container::VariableArray<uint8_t> bytes;
        return ReadEnvironmentDataFile(rootDirectory, WeatherScheduleRelativePath, bytes, pOutError) &&
               WeatherScheduleFromJsonUtf8({bytes.data(), bytes.size()}, inOutSettings, pOutError);
    }
} // 名前空間 NorvesLib::Core::Environment

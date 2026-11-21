// 表面の定義を検査し、既存の添字を保った候補だけを登録簿へ反映する。
#include "Scene/SurfaceRegistry.h"
#include "Asset/AssetFileReader.h"
#include "Logging/LogMacros.h"
#include "Text/IdentityPool.h"
#include "Text/JsonDocument.h"
#include <cmath>
#include <limits>
#include <utility>

namespace NorvesLib::Core::Scene
{
    namespace
    {
        using Error = ESurfaceRegistryError;

        bool Equal(Container::StringView left, Container::StringView right)
        {
            if (left.size() != right.size())
            {
                return false;
            }
            for (size_t i = 0; i < left.size(); ++i)
            {
                if (left[i] != right[i])
                {
                    return false;
                }
            }
            return true;
        }

        SurfaceRegistryLoadResult Fail(Error error, const char* message,
            Container::StringView surface = {}, Container::StringView key = {}) noexcept
        {
            SurfaceRegistryLoadResult result;
            result.Error = error;
            try
            {
                if (!surface.empty())
                {
                    result.Detail.append("表面「");
                    result.Detail.append(surface.data(), surface.size());
                    result.Detail.append("」: ");
                }
                result.Detail.append(message);
                if (!key.empty())
                {
                    result.Detail.append("（キー: ");
                    result.Detail.append(key.data(), key.size());
                    result.Detail.append("）");
                }
            }
            catch (...)
            {
                // 診断用の確保にも失敗した場合は、エラー種別だけを返します。
            }
            return result;
        }

        SurfaceRegistryLoadResult FailRead(const char* message, const Container::AnsiString& path) noexcept
        {
            auto result = Fail(Error::ReadFailed, message);
            try
            {
                result.Detail.append(": ");
                result.Detail.append(path);
            }
            catch (...)
            {
                // 診断の確保に失敗しても、読み込み失敗を返します。
            }
            return result;
        }

        SurfaceRegistryLoadResult CheckKeys(JsonValue object, const char* const* allowed, size_t allowedCount,
            Container::StringView surface, Container::StringView group = {})
        {
            for (size_t i = 0; i < object.GetObjectSize(); ++i)
            {
                const auto& key = object.GetMemberName(i);
                Container::String fullKey;
                if (!group.empty())
                {
                    fullKey.append(group.data(), group.size());
                    fullKey.append(".");
                }
                fullKey.append(key);
                for (size_t j = 0; j < i; ++j)
                {
                    if (Equal(key, object.GetMemberName(j)))
                    {
                        return Fail(Error::DuplicateKey, "キーが重複しています", surface, fullKey);
                    }
                }
                bool bAllowed = false;
                for (size_t j = 0; j < allowedCount; ++j)
                {
                    if (Equal(key, allowed[j]))
                    {
                        bAllowed = true;
                        break;
                    }
                }
                if (!bAllowed)
                {
                    return Fail(Error::UnknownKey, "未知のキーです", surface, fullKey);
                }
            }
            return {};
        }

        bool IsValidName(Container::StringView name)
        {
            if (name.empty() || name.size() > SurfaceRegistry::MaximumNameBytes)
            {
                return false;
            }
            for (size_t i = 0; i < name.size(); ++i)
            {
                const char ch = name[i];
                if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                    (ch >= '0' && ch <= '9') || ch == '_'))
                {
                    return false;
                }
            }
            return true;
        }

        struct StringField
        {
            const char* Key;
            Container::String* Target;
        };

        struct NumberField
        {
            const char* Key;
            float* Target;
            double Minimum;
            double Maximum;
        };

        SurfaceRegistryLoadResult LoadGroup(JsonValue object, Container::StringView surface,
            Container::StringView group,
            const char* const* allowed, size_t allowedCount,
            const StringField* strings, size_t stringCount, const NumberField* numbers, size_t numberCount)
        {
            if (!object.IsObject())
            {
                return Fail(Error::InvalidType, "オブジェクトではありません", surface, group);
            }
            auto result = CheckKeys(object, allowed, allowedCount, surface, group);
            if (!result.Succeeded())
            {
                return result;
            }
            for (size_t i = 0; i < object.GetObjectSize(); ++i)
            {
                const auto& key = object.GetMemberName(i);
                const auto value = object.GetMemberValue(i);
                Container::String fullKey;
                fullKey.append(group.data(), group.size());
                fullKey.append(".");
                fullKey.append(key);
                for (size_t j = 0; j < stringCount; ++j)
                {
                    if (Equal(key, strings[j].Key))
                    {
                        if (!value.IsString())
                        {
                            return Fail(Error::InvalidType, "文字列ではありません", surface, fullKey);
                        }
                        // TString の通常のコピーは途中の NUL を保持できないため、文字ごとに移します。
                        auto& target = *strings[j].Target;
                        const auto& source = value.AsString();
                        target.clear();
                        target.reserve(source.size());
                        for (size_t character = 0; character < source.size(); ++character)
                        {
                            target.push_back(source[character]);
                        }
                        break;
                    }
                }
                for (size_t j = 0; j < numberCount; ++j)
                {
                    if (!Equal(key, numbers[j].Key))
                    {
                        continue;
                    }
                    if (!value.IsNumber())
                    {
                        return Fail(Error::InvalidType, "数ではありません", surface, fullKey);
                    }
                    const double number = value.AsNumber();
                    if (!std::isfinite(number))
                    {
                        return Fail(Error::NonFiniteNumber, "有限の数ではありません", surface, fullKey);
                    }
                    if (number < numbers[j].Minimum || number > numbers[j].Maximum)
                    {
                        return Fail(Error::OutOfRange, "数値が範囲外です", surface, fullKey);
                    }
                    const float convertedValue = static_cast<float>(number);
                    if (!std::isfinite(convertedValue))
                    {
                        return Fail(Error::NonFiniteNumber, "float の範囲に収まりません", surface, fullKey);
                    }
                    *numbers[j].Target = convertedValue;
                    break;
                }
            }
            return {};
        }

        SurfaceRegistryLoadResult LoadGroups(JsonValue surfaceValue, SurfaceDesc& desc)
        {
            const double unlimited = std::numeric_limits<double>::max();
            if (const auto audio = surfaceValue.FindMember("audio"); audio.IsValid())
            {
                static constexpr const char* keys[] = {"footstepSet", "noiseLevelDb", "transmissionLossDb"};
                const StringField strings[] = {{"footstepSet", &desc.Audio.FootstepSet}};
                const NumberField numbers[] = {
                    {"noiseLevelDb", &desc.Audio.NoiseLevelDb, -unlimited, unlimited},
                    {"transmissionLossDb", &desc.Audio.TransmissionLossDb, -unlimited, unlimited}};
                auto result = LoadGroup(audio, desc.Name, "audio", keys, 3, strings, 1, numbers, 2);
                if (!result.Succeeded())
                {
                    return result;
                }
            }
            if (const auto visual = surfaceValue.FindMember("visual"); visual.IsValid())
            {
                static constexpr const char* keys[] = {"footprint", "splash", "ripple", "grassTrample", "wetFurTransfer"};
                const StringField strings[] = {{"footprint", &desc.Visual.Footprint},
                    {"splash", &desc.Visual.Splash}, {"ripple", &desc.Visual.Ripple}};
                const NumberField numbers[] = {{"grassTrample", &desc.Visual.GrassTrample, 0.0, 1.0},
                    {"wetFurTransfer", &desc.Visual.WetFurTransfer, 0.0, 1.0}};
                auto result = LoadGroup(visual, desc.Name, "visual", keys, 5, strings, 3, numbers, 2);
                if (!result.Succeeded())
                {
                    return result;
                }
            }
            if (const auto physical = surfaceValue.FindMember("physical"); physical.IsValid())
            {
                static constexpr const char* keys[] = {"friction", "restitution", "footSinkCm"};
                const NumberField numbers[] = {{"friction", &desc.Physical.Friction, 0.0, unlimited},
                    {"restitution", &desc.Physical.Restitution, 0.0, 1.0},
                    {"footSinkCm", &desc.Physical.FootSinkCm, 0.0, unlimited}};
                auto result = LoadGroup(physical, desc.Name, "physical", keys, 3, nullptr, 0, numbers, 3);
                if (!result.Succeeded())
                {
                    return result;
                }
            }
            return {};
        }

        SurfaceRegistryLoadResult LoadFlags(JsonValue flags, SurfaceDesc& desc)
        {
            if (!flags.IsArray())
            {
                return Fail(Error::InvalidType, "配列ではありません", desc.Name, "flags");
            }
            for (size_t i = 0; i < flags.GetArraySize(); ++i)
            {
                const auto value = flags.GetArrayElement(i);
                if (!value.IsString())
                {
                    return Fail(Error::InvalidType, "要素が文字列ではありません", desc.Name, "flags");
                }
                const auto& name = value.AsString();
                if (Equal(name, "Soft"))
                {
                    desc.Flags |= SurfaceFlags::Soft;
                }
                else if (Equal(name, "Hard"))
                {
                    desc.Flags |= SurfaceFlags::Hard;
                }
                else if (Equal(name, "Liquid"))
                {
                    desc.Flags |= SurfaceFlags::Liquid;
                }
                else if (Equal(name, "Vegetated"))
                {
                    desc.Flags |= SurfaceFlags::Vegetated;
                }
                else if (Equal(name, "Wettable"))
                {
                    desc.Flags |= SurfaceFlags::Wettable;
                }
                else
                {
                    Container::String detail("未知のフラグです: ");
                    detail.append(name);
                    return Fail(Error::UnknownFlag, detail.c_str(), desc.Name, "flags");
                }
            }
            return {};
        }
    }

    SurfaceRegistry::SurfaceRegistry()
    {
        SurfaceEntry entry;
        entry.Desc.Name = DefaultSurfaceName;
        entry.bActive = true;
        m_Surfaces.push_back(std::move(entry));
    }

    SurfaceRegistryLoadResult SurfaceRegistry::LoadFromJson(const Container::String& json)
    {
        try
        {
            if (json.empty())
            {
                return Fail(Error::Empty, "表面の JSON が空です");
            }
            if (json.size() > MaximumTextBytes)
            {
                return Fail(Error::TooLarge, "表面の JSON が 1 MiB を超えています");
            }
            const bool bHasBom = json.size() >= 3 && static_cast<unsigned char>(json[0]) == 0xEF &&
                static_cast<unsigned char>(json[1]) == 0xBB && static_cast<unsigned char>(json[2]) == 0xBF;
            JsonDocument document;
            const Container::String jsonWithoutBom = bHasBom ? json.substr(3) : Container::String();
            const Container::String& text = bHasBom ? jsonWithoutBom : json;
            Container::String parseError;
            if (!JsonDocument::TryParse(text, document, &parseError))
            {
                Container::String detail("表面の JSON を解析できません: ");
                detail.append(parseError);
                return Fail(Error::ParseError, detail.c_str());
            }
            const auto root = document.GetRoot();
            if (!root.IsObject())
            {
                return Fail(Error::InvalidRoot, "ルートがオブジェクトではありません");
            }
            static constexpr const char* rootKeys[] = {"version", "surfaces"};
            auto result = CheckKeys(root, rootKeys, 2, {});
            if (!result.Succeeded())
            {
                return result;
            }
            const auto version = root.FindMember("version");
            if (!version.IsIntegerLiteral())
            {
                return Fail(Error::InvalidType, "整数が必要です", {}, "version");
            }
            if (version.AsNumber() != static_cast<double>(SupportedVersion))
            {
                return Fail(Error::UnsupportedVersion, "未対応のバージョンです", {}, "version");
            }
            const auto surfaces = root.FindMember("surfaces");
            if (!surfaces.IsArray())
            {
                return Fail(Error::InvalidType, "配列が必要です", {}, "surfaces");
            }

            Container::VariableArray<SurfaceEntry> candidate = m_Surfaces;
            Container::VariableArray<JsonValue> parents(candidate.size());
            for (auto& entry : candidate)
            {
                entry.bActive = false;
            }
            candidate[0].Desc = SurfaceDesc{};
            candidate[0].Desc.Name = DefaultSurfaceName;
            candidate[0].bActive = true;
            bool bDefaultSpecified = false;
            static constexpr const char* thresholdKey = "vegetationDensityThreshold";
            static constexpr const char* surfaceKeys[] = {"name", "parent", "flags", "audio", "visual", "physical", thresholdKey};
            for (size_t i = 0; i < surfaces.GetArraySize(); ++i)
            {
                const auto value = surfaces.GetArrayElement(i);
                if (!value.IsObject())
                {
                    return Fail(Error::InvalidType, "要素がオブジェクトではありません", {}, "surfaces");
                }
                const auto nameValue = value.FindMember("name");
                const Container::StringView displayName = nameValue.IsString()
                    ? Container::StringView(nameValue.AsString()) : Container::StringView("未指定");
                result = CheckKeys(value, surfaceKeys, 7, displayName);
                if (!result.Succeeded())
                {
                    return result;
                }
                if (!nameValue.IsString())
                {
                    return Fail(Error::InvalidType, "文字列が必要です", displayName, "name");
                }
                const auto& name = nameValue.AsString();
                if (!IsValidName(name))
                {
                    return Fail(Error::InvalidName, "名前は 1 から 64 バイトの英数字と _ にしてください",
                        name.empty() ? Container::StringView("空") : Container::StringView(name), "name");
                }
                size_t index = 0;
                while (index < candidate.size() && !Equal(candidate[index].Desc.Name, name))
                {
                    ++index;
                }
                const bool bDefault = Equal(name, DefaultSurfaceName);
                if ((bDefault && bDefaultSpecified) || (!bDefault && index < candidate.size() && candidate[index].bActive))
                {
                    return Fail(Error::DuplicateName, "名前が重複しています", name, "name");
                }
                if (index == candidate.size())
                {
                    if (candidate.size() >= MaximumSurfaceCount)
                    {
                        return Fail(Error::TooManySurfaces, "引退した添字を含む表面数が上限を超えています", name, "name");
                    }
                    candidate.push_back({});
                    parents.push_back({});
                }
                SurfaceDesc desc;
                desc.Name = name;
                const auto parent = value.FindMember("parent");
                if (parent.IsValid() && !parent.IsString())
                {
                    return Fail(Error::InvalidType, "文字列が必要です", name, "parent");
                }
                if (bDefault && parent.IsValid() && !parent.AsString().empty() &&
                    !Equal(parent.AsString(), DefaultSurfaceName))
                {
                    return Fail(Error::InvalidDefault, "Default の親は Default である必要があります", name, "parent");
                }
                if (const auto flags = value.FindMember("flags"); flags.IsValid())
                {
                    result = LoadFlags(flags, desc);
                    if (!result.Succeeded())
                    {
                        return result;
                    }
                }
                result = LoadGroups(value, desc);
                if (!result.Succeeded())
                {
                    return result;
                }
                if (const auto threshold = value.FindMember(thresholdKey); threshold.IsValid())
                {
                    if (!threshold.IsNumber())
                    {
                        return Fail(Error::InvalidType, "数ではありません", name, thresholdKey);
                    }
                    const double number = threshold.AsNumber();
                    if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max())
                    {
                        return Fail(Error::NonFiniteNumber, "有限の float に収まりません", name, thresholdKey);
                    }
                    if (number < 0.0 || number > 1.0)
                    {
                        return Fail(Error::OutOfRange, "数値が範囲外です", name, thresholdKey);
                    }
                    desc.VegetationDensityThreshold = static_cast<float>(number);
                }
                candidate[index].Desc = std::move(desc);
                candidate[index].bActive = true;
                parents[index] = parent;
                bDefaultSpecified = bDefaultSpecified || bDefault;
            }

            for (size_t i = 1; i < candidate.size(); ++i)
            {
                if (!candidate[i].bActive || !parents[i].IsValid() || parents[i].AsString().empty())
                {
                    continue;
                }
                const auto& parentName = parents[i].AsString();
                size_t parentIndex = 0;
                while (parentIndex < candidate.size() &&
                    (!candidate[parentIndex].bActive || !Equal(candidate[parentIndex].Desc.Name, parentName)))
                {
                    ++parentIndex;
                }
                if (parentIndex == candidate.size())
                {
                    Container::String detail("親の表面が見つかりません: ");
                    detail.append(parentName);
                    return Fail(Error::UnknownParent, detail.c_str(), candidate[i].Desc.Name, "parent");
                }
                candidate[i].Desc.Parent = SurfaceId(static_cast<uint16_t>(parentIndex));
            }
            for (size_t i = 1; i < candidate.size(); ++i)
            {
                if (!candidate[i].bActive)
                {
                    continue;
                }
                size_t current = i;
                size_t visited = 0;
                while (current != 0 && visited < candidate.size())
                {
                    current = candidate[current].Desc.Parent.Value;
                    ++visited;
                }
                if (current != 0)
                {
                    return Fail(Error::CyclicParent, "親の連鎖が循環しています", candidate[i].Desc.Name, "parent");
                }
            }
            {
                Thread::ScopedLock lock(m_WarningMutex);
                m_Surfaces.swap(candidate);
                ++m_Revision;
                m_WarnedNames.clear();
            }
            return {};
        }
        catch (...)
        {
            return Fail(Error::ParseError, "表面の読み込み中に例外が発生しました");
        }
    }

    SurfaceRegistryLoadResult SurfaceRegistry::LoadFromAssetFile(const Container::AnsiString& inputPath)
    {
        try
        {
            Asset::AssetFileReader reader;
            Asset::AssetReadRequest request;
            request.InputPath = inputPath;
            request.bAllowAbsolutePath = false;
            request.MaxReadBytes = MaximumTextBytes;
            const auto asset = reader.Read(request);
            if (!asset.Succeeded())
            {
                return FailRead("表面ファイルを読めません", inputPath);
            }
            Container::String text;
            if (!asset.Blob.IsEmpty())
            {
                text.append(reinterpret_cast<const char*>(asset.Blob.GetData()), asset.Blob.GetSize());
            }
            return LoadFromJson(text);
        }
        catch (...)
        {
            return FailRead("表面ファイルの読み込み中に例外が発生しました", inputPath);
        }
    }

    bool SurfaceRegistry::TryFindByName(Container::StringView name, SurfaceId& outId) const
    {
        outId = SurfaceId::Default();
        if (name.empty())
        {
            return false;
        }
        for (size_t i = 0; i < m_Surfaces.size(); ++i)
        {
            if (m_Surfaces[i].bActive && Equal(m_Surfaces[i].Desc.Name, name))
            {
                outId = SurfaceId(static_cast<uint16_t>(i));
                return true;
            }
        }
        return false;
    }

    SurfaceId SurfaceRegistry::FindByName(Container::StringView name) const
    {
        SurfaceId id;
        if (TryFindByName(name, id) || name.empty())
        {
            return id;
        }
#if NORVES_ENABLE_LOGGING
        bool bShouldWarn = false;
        {
            Thread::ScopedLock lock(m_WarningMutex);
            bShouldWarn = m_WarnedNames.insert(IdentityHash(name.data(), name.size())).second;
        }
        if (bShouldWarn)
        {
            const Container::String nameText(name);
            NORVES_LOG_WARNING("Surface", "未知の表面の名前です。Default として扱います: %s", nameText.c_str());
        }
#endif
        return SurfaceId::Default();
    }

    const SurfaceDesc& SurfaceRegistry::GetDesc(SurfaceId id) const
    {
        return id.Value < m_Surfaces.size() && m_Surfaces[id.Value].bActive
            ? m_Surfaces[id.Value].Desc : m_Surfaces[0].Desc;
    }

    const Container::String& SurfaceRegistry::GetName(SurfaceId id) const
    {
        return id.Value < m_Surfaces.size() ? m_Surfaces[id.Value].Desc.Name : m_Surfaces[0].Desc.Name;
    }

    uint64_t SurfaceRegistry::GetRevision() const
    {
        return m_Revision;
    }

    size_t SurfaceRegistry::GetSurfaceCount() const
    {
        return m_Surfaces.size();
    }
} // namespace NorvesLib::Core::Scene

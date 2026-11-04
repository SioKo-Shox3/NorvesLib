#include "Vegetation/GrassSpecies.h"

#include "FileStream/FileStream.h"
#include "Text/JsonDocument.h"
#include <cmath>
#include <limits>
#include <utility>

namespace NorvesLib::Core::Vegetation
{
    namespace
    {
        bool Fail(Container::String* pOutError, const char* message, const char* field = nullptr)
        {
            if (pOutError != nullptr)
            {
                *pOutError = message;
                if (field != nullptr)
                {
                    *pOutError += ": ";
                    *pOutError += field;
                }
            }
            return false;
        }

        bool ReadNumber(JsonValue object, const char* field, float& outValue, Container::String* pOutError)
        {
            if (!object.HasMember(field))
            {
                return true;
            }
            const JsonValue value = object.FindMember(field);
            const double number = value.AsNumber();
            if (!value.IsNumber() || !std::isfinite(number) ||
                number < -std::numeric_limits<float>::max() || number > std::numeric_limits<float>::max())
            {
                return Fail(pOutError, "有限の浮動小数点数を指定してください", field);
            }
            outValue = static_cast<float>(number);
            return true;
        }

        int HexDigit(Container::String::value_type value)
        {
            if (value >= '0' && value <= '9')
            {
                return value - '0';
            }
            if (value >= 'a' && value <= 'f')
            {
                return value - 'a' + 10;
            }
            if (value >= 'A' && value <= 'F')
            {
                return value - 'A' + 10;
            }
            return -1;
        }

        float SrgbToLinear(int value)
        {
            const float srgb = static_cast<float>(value) / 255.0f;
            return srgb <= 0.04045f ? srgb / 12.92f : std::pow((srgb + 0.055f) / 1.055f, 2.4f);
        }

        bool ReadColor(JsonValue object, const char* field, Math::Vector3& outColor, Container::String* pOutError)
        {
            if (!object.HasMember(field))
            {
                return true;
            }
            const JsonValue value = object.FindMember(field);
            const Container::String& text = value.AsString();
            if (!value.IsString() || text.size() != 7 || text[0] != '#')
            {
                return Fail(pOutError, "色は #RRGGBB 形式で指定してください", field);
            }
            float channels[3];
            for (uint32_t channel = 0; channel < 3; ++channel)
            {
                const int high = HexDigit(text[1 + channel * 2]);
                const int low = HexDigit(text[2 + channel * 2]);
                if (high < 0 || low < 0)
                {
                    return Fail(pOutError, "色には 16 進数の 6 桁を指定してください", field);
                }
                channels[channel] = SrgbToLinear(high * 16 + low);
            }
            outColor = Math::Vector3(channels[0], channels[1], channels[2]);
            return true;
        }

        bool ReadSpecies(JsonValue object, GrassSpecies& outSpecies, Container::String* pOutError)
        {
            if (!object.IsObject())
            {
                return Fail(pOutError, "species の各要素はオブジェクトで指定してください");
            }
            if (object.HasMember("name"))
            {
                const JsonValue name = object.FindMember("name");
                if (!name.IsString())
                {
                    return Fail(pOutError, "name は文字列で指定してください");
                }
                outSpecies.Name = name.AsString();
            }

            struct NumberField
            {
                const char* Name;
                float GrassSpecies::* Member;
            };
            const NumberField fields[] = {
                {"heightMinMeters", &GrassSpecies::HeightMinMeters},
                {"heightMaxMeters", &GrassSpecies::HeightMaxMeters},
                {"bladeWidthMeters", &GrassSpecies::BladeWidthMeters},
                {"bend", &GrassSpecies::Bend},
                {"stiffness", &GrassSpecies::Stiffness},
                {"roughness", &GrassSpecies::Roughness},
                {"windResponse", &GrassSpecies::WindResponse},
                {"trampleResponse", &GrassSpecies::TrampleResponse},
                {"wetRoughness", &GrassSpecies::WetRoughness},
                {"wetDroop", &GrassSpecies::WetDroop},
                {"opacity", &GrassSpecies::Opacity},
                {"bladesPerSquareMeter", &GrassSpecies::BladesPerSquareMeter}
            };
            for (const NumberField& field : fields)
            {
                if (!ReadNumber(object, field.Name, outSpecies.*(field.Member), pOutError))
                {
                    return false;
                }
            }
            if (outSpecies.HeightMinMeters > outSpecies.HeightMaxMeters)
            {
                return Fail(pOutError, "heightMinMeters は heightMaxMeters 以下にしてください");
            }
            if (!ReadColor(object, "rootColor", outSpecies.RootColor, pOutError) ||
                !ReadColor(object, "tipColor", outSpecies.TipColor, pOutError) ||
                !ReadColor(object, "wetColor", outSpecies.WetColor, pOutError))
            {
                return false;
            }
            if (object.HasMember("lod"))
            {
                const JsonValue lod = object.FindMember("lod");
                if (!lod.IsArray() || lod.GetArraySize() != GrassLodPointCount)
                {
                    return Fail(pOutError, "lod は 4 個の配列で指定してください");
                }
                for (uint32_t index = 0; index < GrassLodPointCount; ++index)
                {
                    const JsonValue point = lod.GetArrayElement(index);
                    GrassLodPoint& output = outSpecies.Lod[index];
                    if (!point.IsObject())
                    {
                        return Fail(pOutError, "lod の各要素はオブジェクトで指定してください");
                    }
                    if (!ReadNumber(point, "distanceMeters", output.DistanceMeters, pOutError) ||
                        !ReadNumber(point, "keepFraction", output.KeepFraction, pOutError))
                    {
                        return false;
                    }
                    if (index > 0 && output.DistanceMeters <= outSpecies.Lod[index - 1].DistanceMeters)
                    {
                        return Fail(pOutError, "lod の距離は重複のない昇順にしてください");
                    }
                }
            }
            return true;
        }
    }

    const GrassSpecies* GrassSpeciesTable::Find(uint32_t speciesId) const
    {
        return speciesId < m_Species.size() ? &m_Species[speciesId] : nullptr;
    }

    uint32_t GrassSpeciesTable::GetCount() const
    {
        return static_cast<uint32_t>(m_Species.size());
    }

    bool GrassSpeciesTable::Add(const GrassSpecies& species)
    {
        if (m_Species.size() >= GrassSpeciesMaxCount)
        {
            return false;
        }
        m_Species.push_back(species);
        return true;
    }

    const Container::VariableArray<GrassSpecies>& GrassSpeciesTable::GetAll() const
    {
        return m_Species;
    }

    bool TryParseGrassSpeciesTable(const Container::String& jsonText, GrassSpeciesTable& outTable,
                                  Container::String* pOutError)
    {
        if (pOutError != nullptr)
        {
            pOutError->clear();
        }
        JsonDocument document;
        if (!JsonDocument::TryParse(jsonText, document))
        {
            return Fail(pOutError, "草種別の JSON を解析できません");
        }
        const JsonValue root = document.GetRoot();
        if (!root.IsObject())
        {
            return Fail(pOutError, "草種別の JSON のルートはオブジェクトで指定してください");
        }
        if (root.HasMember("version"))
        {
            const JsonValue version = root.FindMember("version");
            if (!version.IsNumber() || version.AsNumber() != 0.0)
            {
                return Fail(pOutError, "草種別の version は 0 を指定してください");
            }
        }
        const JsonValue species = root.FindMember("species");
        if (!species.IsArray() || species.GetArraySize() == 0 || species.GetArraySize() > GrassSpeciesMaxCount)
        {
            return Fail(pOutError, "species は 1 個以上 64 個以下の配列で指定してください");
        }
        GrassSpeciesTable parsedTable;
        for (size_t index = 0; index < species.GetArraySize(); ++index)
        {
            GrassSpecies entry;
            if (!ReadSpecies(species.GetArrayElement(index), entry, pOutError) || !parsedTable.Add(entry))
            {
                return false;
            }
        }
        outTable = std::move(parsedTable);
        return true;
    }

    bool TryLoadGrassSpeciesTable(const char* filePath, GrassSpeciesTable& outTable, Container::String* pOutError)
    {
        if (filePath == nullptr || filePath[0] == '\0')
        {
            return Fail(pOutError, "草種別のファイルパスを指定してください");
        }
        auto stream = FileStream::FileStream::CreateUnique(Container::String(filePath),
            FileStream::FileMode::Read, FileStream::FileAccess::Read);
        if (!stream || !stream->IsOpen())
        {
            return Fail(pOutError, "草種別のファイルを開けません");
        }
        const int64_t size = stream->GetSize();
        Container::String text = stream->ReadString();
        if (size < 0 || static_cast<uint64_t>(size) != text.size())
        {
            return Fail(pOutError, "草種別のファイル全体を読み込めません");
        }
        // 同梱 JSON を含め、UTF-8 の BOM は JSON 解析の前に除去する。
        if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
            static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
        {
            text = text.substr(3);
        }
        return TryParseGrassSpeciesTable(text, outTable, pOutError);
    }
} // namespace NorvesLib::Core::Vegetation

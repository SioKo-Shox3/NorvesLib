#include "Scene/PhysicsLayerTable.h"
#include "Text/JsonDocument.h"
#include <cmath>

namespace NorvesLib::Core::Scene
{
    namespace
    {
        bool Fail(Container::String* error, const char* message) noexcept
        {
            if (error)
            {
                try
                {
                    *error = Container::String(message);
                }
                catch (...)
                {
                    // 診断のメモリ確保も失敗した場合は、例外を伝播せず元の診断を保持します。
                }
            }
            return false;
        }
    }

    PhysicsLayerTable::PhysicsLayerTable()
    {
        ResetToDefault();
    }

    void PhysicsLayerTable::ResetToDefault()
    {
        Container::VariableArray<PhysicsLayerEntry> candidate;
        candidate.push_back({Identity(DefaultLayerName), 0});
        m_Layers.swap(candidate);
    }

    bool PhysicsLayerTable::LoadFromJson(const Container::String& json, Container::String* error)
    {
        try
        {
            if (json.empty())
            {
                return Fail(error, "コリジョンレイヤーのJSONが空です");
            }
            if (json.size() > MaximumTextBytes)
            {
                return Fail(error, "コリジョンレイヤーのJSONが1MiBを超えています");
            }

            const bool bHasBom = json.size() >= 3 &&
                static_cast<unsigned char>(json[0]) == 0xEF &&
                static_cast<unsigned char>(json[1]) == 0xBB &&
                static_cast<unsigned char>(json[2]) == 0xBF;
            JsonDocument document;
            if (!(bHasBom ? JsonDocument::TryParse(json.substr(3), document) :
                JsonDocument::TryParse(json, document)))
            {
                return Fail(error, "コリジョンレイヤーのJSONを解析できません");
            }

            const auto root = document.GetRoot();
            if (!root.IsObject())
            {
                return Fail(error, "コリジョンレイヤーのルートがオブジェクトではありません");
            }
            const auto schema = root.FindMember("schema");
            if (!schema.IsString() || schema.AsString() != SchemaName)
            {
                return Fail(error, "コリジョンレイヤーのschemaが未指定または未対応です");
            }
            const auto layers = root.FindMember("layers");
            if (!layers.IsArray())
            {
                return Fail(error, "コリジョンレイヤーのlayersが配列ではありません");
            }
            if (layers.GetArraySize() > 32)
            {
                return Fail(error, "コリジョンレイヤー数が32を超えています");
            }

            Container::VariableArray<PhysicsLayerEntry> candidate;
            candidate.reserve(layers.GetArraySize());
            PhysicsCollisionMask usedBits = 0;
            bool bHasDefault = false;
            for (size_t index = 0; index < layers.GetArraySize(); ++index)
            {
                const auto value = layers.GetArrayElement(index);
                if (!value.IsObject())
                {
                    return Fail(error, "コリジョンレイヤーの要素がオブジェクトではありません");
                }
                const auto name = value.FindMember("name");
                if (!name.IsString() || name.AsString().empty())
                {
                    return Fail(error, "コリジョンレイヤーのnameが未指定・空、または文字列ではありません");
                }
                const auto bit = value.FindMember("bit");
                if (!bit.IsIntegerLiteral())
                {
                    return Fail(error, "コリジョンレイヤーのbitが整数ではありません");
                }
                const double bitValue = bit.AsNumber();
                if (!std::isfinite(bitValue) || std::signbit(bitValue) || bitValue > 31.0)
                {
                    return Fail(error, "コリジョンレイヤーのbitが0から31の範囲外です");
                }

                const auto bitIndex = static_cast<uint8_t>(bitValue);
                const PhysicsCollisionMask mask = PhysicsCollisionMask{1u} << bitIndex;
                if ((usedBits & mask) != 0)
                {
                    return Fail(error, "コリジョンレイヤーのbitが重複しています");
                }
                const Identity layerName(name.AsString());
                for (const auto& entry : candidate)
                {
                    if (entry.Name == layerName)
                    {
                        return Fail(error, "コリジョンレイヤーのnameが重複しています");
                    }
                }
                if (name.AsString() == DefaultLayerName)
                {
                    if (bitIndex != 0)
                    {
                        return Fail(error, "Defaultのbitは0である必要があります");
                    }
                    bHasDefault = true;
                }
                candidate.push_back({layerName, bitIndex});
                usedBits |= mask;
            }
            if (!bHasDefault)
            {
                return Fail(error, "コリジョンレイヤーにDefaultがありません");
            }

            if (error)
            {
                error->clear();
            }
            m_Layers.swap(candidate);
            return true;
        }
        catch (...)
        {
            return Fail(error, "コリジョンレイヤーの読み込み中に例外が発生しました");
        }
    }

    bool PhysicsLayerTable::TryGetLayerMask(const Identity& name, PhysicsCollisionMask& outMask) const
    {
        if (!name.IsValid())
        {
            return false;
        }
        for (const auto& entry : m_Layers)
        {
            if (entry.Name == name)
            {
                outMask = PhysicsCollisionMask{1u} << entry.Bit;
                return true;
            }
        }
        return false;
    }

    bool PhysicsLayerTable::TryBuildMask(Container::Span<const Identity> names, PhysicsCollisionMask& outMask) const
    {
        PhysicsCollisionMask candidate = 0;
        for (size_t index = 0; index < names.size(); ++index)
        {
            PhysicsCollisionMask mask = 0;
            if (!TryGetLayerMask(names[index], mask))
            {
                return false;
            }
            candidate |= mask;
        }
        outMask = candidate;
        return true;
    }

    Container::Span<const PhysicsLayerEntry> PhysicsLayerTable::GetLayers() const
    {
        return {m_Layers.data(), m_Layers.size()};
    }

    size_t PhysicsLayerTable::GetLayerCount() const
    {
        return m_Layers.size();
    }
} // namespace NorvesLib::Core::Scene

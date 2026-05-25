#pragma once

#include "Container/Containers.h"
#include <cstdint>

namespace NorvesLib::Core
{
    /**
     * @brief JSON value type.
     */
    enum class JsonType : uint8_t
    {
        Invalid,
        Null,
        Object,
        Array,
        String,
        Number,
        Boolean
    };

    class JsonDocument;
    class JsonDocumentParser;

    /**
     * @brief Read-only JSON value view inside a JsonDocument.
     */
    class JsonValue
    {
    public:
        JsonValue() = default;

        /**
         * @brief Returns whether the value is valid.
         */
        bool IsValid() const;

        /**
         * @brief Returns the value type.
         */
        JsonType GetType() const;

        bool IsNull() const;
        bool IsObject() const;
        bool IsArray() const;
        bool IsString() const;
        bool IsNumber() const;
        bool IsBoolean() const;

        /**
         * @brief Returns the array size.
         */
        size_t GetArraySize() const;

        /**
         * @brief Returns an array element.
         * @param index Element index.
         */
        JsonValue GetArrayElement(size_t index) const;

        /**
         * @brief Finds an object member by key.
         * @param key Member key.
         */
        JsonValue FindMember(const char* key) const;

        /**
         * @brief Returns whether an object member exists.
         * @param key Member key.
         */
        bool HasMember(const char* key) const;

        /**
         * @brief Returns the string value.
         * @return Empty string when the value is not a string.
         */
        const Container::String& AsString() const;

        /**
         * @brief Returns the number value.
         * @param defaultValue Fallback value when the type does not match.
         */
        double AsNumber(double defaultValue = 0.0) const;

        /**
         * @brief Returns the value as a signed 64-bit integer.
         * @param defaultValue Fallback value when the type does not match.
         */
        int64_t AsInt64(int64_t defaultValue = 0) const;

        /**
         * @brief Returns the value as an unsigned 32-bit integer.
         * @param defaultValue Fallback value when the type does not match.
         */
        uint32_t AsUInt32(uint32_t defaultValue = 0) const;

        /**
         * @brief Returns the value as bool.
         * @param defaultValue Fallback value when the type does not match.
         */
        bool AsBool(bool defaultValue = false) const;

    private:
        friend class JsonDocument;

        JsonValue(const JsonDocument* pDocument, size_t nodeIndex);

        const JsonDocument* m_pDocument = nullptr;
        size_t m_NodeIndex = static_cast<size_t>(-1);
    };

    /**
     * @brief Read-only JSON document.
     */
    class JsonDocument
    {
        friend class JsonValue;
        friend class JsonDocumentParser;

    public:
        /**
         * @brief Parses a JSON string.
         * @param text JSON source text.
         * @param outDocument Output document.
         * @param pOutError Optional parse error output.
         * @return True on success.
         */
        static bool TryParse(const Container::String& text, JsonDocument& outDocument,
                             Container::String* pOutError = nullptr);

        /**
         * @brief Resets the document.
         */
        void Reset();

        /**
         * @brief Returns the root value.
         */
        JsonValue GetRoot() const;

        /**
         * @brief Returns whether the document has a root value.
         */
        bool HasRoot() const { return m_RootNodeIndex != InvalidNodeIndex; }

    private:
        static constexpr size_t InvalidNodeIndex = static_cast<size_t>(-1);

        struct JsonObjectEntry
        {
            Container::String Key;
            size_t NodeIndex = InvalidNodeIndex;
        };

        struct JsonNode
        {
            JsonType Type = JsonType::Invalid;
            Container::String StringValue;
            double NumberValue = 0.0;
            bool bBoolValue = false;
            Container::VariableArray<size_t> ArrayChildren;
            Container::VariableArray<JsonObjectEntry> ObjectChildren;
        };

        size_t CreateNode(JsonType type);

        Container::VariableArray<JsonNode> m_Nodes;
        size_t m_RootNodeIndex = InvalidNodeIndex;
    };

    inline JsonValue::JsonValue(const JsonDocument* pDocument, size_t nodeIndex)
        : m_pDocument(pDocument), m_NodeIndex(nodeIndex)
    {
    }

    inline bool JsonValue::IsValid() const
    {
        return m_pDocument != nullptr &&
               m_NodeIndex < m_pDocument->m_Nodes.size() &&
               m_NodeIndex != JsonDocument::InvalidNodeIndex;
    }

    inline JsonType JsonValue::GetType() const
    {
        if (!IsValid())
        {
            return JsonType::Invalid;
        }

        return m_pDocument->m_Nodes[m_NodeIndex].Type;
    }

    inline bool JsonValue::IsNull() const
    {
        return GetType() == JsonType::Null;
    }

    inline bool JsonValue::IsObject() const
    {
        return GetType() == JsonType::Object;
    }

    inline bool JsonValue::IsArray() const
    {
        return GetType() == JsonType::Array;
    }

    inline bool JsonValue::IsString() const
    {
        return GetType() == JsonType::String;
    }

    inline bool JsonValue::IsNumber() const
    {
        return GetType() == JsonType::Number;
    }

    inline bool JsonValue::IsBoolean() const
    {
        return GetType() == JsonType::Boolean;
    }

    inline size_t JsonValue::GetArraySize() const
    {
        if (!IsArray())
        {
            return 0;
        }

        return m_pDocument->m_Nodes[m_NodeIndex].ArrayChildren.size();
    }

    inline JsonValue JsonValue::GetArrayElement(size_t index) const
    {
        if (!IsArray())
        {
            return {};
        }

        const auto& children = m_pDocument->m_Nodes[m_NodeIndex].ArrayChildren;
        if (index >= children.size())
        {
            return {};
        }

        return JsonValue(m_pDocument, children[index]);
    }

    inline JsonValue JsonValue::FindMember(const char* key) const
    {
        if (!IsObject() || key == nullptr)
        {
            return {};
        }

        const auto& members = m_pDocument->m_Nodes[m_NodeIndex].ObjectChildren;
        for (const auto& member : members)
        {
            if (member.Key == key)
            {
                return JsonValue(m_pDocument, member.NodeIndex);
            }
        }

        return {};
    }

    inline bool JsonValue::HasMember(const char* key) const
    {
        return FindMember(key).IsValid();
    }

    inline const Container::String& JsonValue::AsString() const
    {
        static const Container::String EmptyString;

        if (!IsString())
        {
            return EmptyString;
        }

        return m_pDocument->m_Nodes[m_NodeIndex].StringValue;
    }

    inline double JsonValue::AsNumber(double defaultValue) const
    {
        if (!IsNumber())
        {
            return defaultValue;
        }

        return m_pDocument->m_Nodes[m_NodeIndex].NumberValue;
    }

    inline int64_t JsonValue::AsInt64(int64_t defaultValue) const
    {
        if (!IsNumber())
        {
            return defaultValue;
        }

        return static_cast<int64_t>(m_pDocument->m_Nodes[m_NodeIndex].NumberValue);
    }

    inline uint32_t JsonValue::AsUInt32(uint32_t defaultValue) const
    {
        if (!IsNumber())
        {
            return defaultValue;
        }

        double value = m_pDocument->m_Nodes[m_NodeIndex].NumberValue;
        if (value < 0.0 || value > static_cast<double>(UINT32_MAX))
        {
            return defaultValue;
        }

        return static_cast<uint32_t>(value);
    }

    inline bool JsonValue::AsBool(bool defaultValue) const
    {
        if (!IsBoolean())
        {
            return defaultValue;
        }

        return m_pDocument->m_Nodes[m_NodeIndex].bBoolValue;
    }

} // namespace NorvesLib::Core

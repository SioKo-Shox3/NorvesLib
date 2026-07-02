#pragma once

#include "Container/Containers.h"
#include <cstdint>

namespace NorvesLib::Core
{
    /**
     * @brief JSONテキストを構築する書き出し器。JsonDocument::TryParseと対になる。
     *
     * 使用順序が不正な呼び出し（配列内でキー付き書き込み、種別不一致のEnd等）は
     * エラー状態を立てて以降の書き込みを無視する。完成判定はIsComplete()。
     * 文字列は "、\、制御文字(< 0x20)をエスケープする。それ以外のバイトは
     * そのまま透過する（UTF-8前提）。
     */
    class JsonWriter
    {
    public:
        explicit JsonWriter(bool bPretty = false);

        void BeginObject();
        void BeginObject(const char* key);
        void EndObject();
        void BeginArray();
        void BeginArray(const char* key);
        void EndArray();

        void WriteString(const char* key, const Container::String& value);
        void WriteString(const char* key, const char* value);
        void WriteStringElement(const Container::String& value);
        void WriteNumber(const char* key, double value);
        void WriteNumberElement(double value);
        void WriteInt64(const char* key, int64_t value);
        void WriteUInt64(const char* key, uint64_t value);
        void WriteBool(const char* key, bool value);
        void WriteBoolElement(bool value);
        void WriteNull(const char* key);
        void WriteNullElement();

        bool HasError() const { return m_bError; }
        bool IsComplete() const { return !m_bError && m_bRootWritten && m_Stack.empty(); }
        Container::String ToString() const { return m_Buffer; }

    private:
        enum class ScopeKind : uint8_t
        {
            Object,
            Array
        };

        struct Scope
        {
            ScopeKind Kind = ScopeKind::Object;
            bool bHasElements = false;
        };

        bool PrepareKeyedValue(const char* key);
        bool PrepareElementValue();
        void BeginContainer(ScopeKind kind);
        void BeginContainer(const char* key, ScopeKind kind);
        void EndContainer(ScopeKind kind);
        void AppendSeparatorAndIndent();
        void AppendIndent(size_t depth);
        void AppendKey(const char* key);
        void AppendEscapedString(const char* value, size_t length);
        void AppendDouble(double value);
        void SetError();

        Container::String m_Buffer;
        Container::VariableArray<Scope> m_Stack;
        bool m_bPretty = false;
        bool m_bError = false;
        bool m_bRootWritten = false;
    };

} // namespace NorvesLib::Core
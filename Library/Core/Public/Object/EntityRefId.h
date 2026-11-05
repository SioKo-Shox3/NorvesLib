#pragma once

#include "Object/RuntimeSchema.h"
#include <cstdint>
#include <limits>
#include <type_traits>

namespace NorvesLib::Core
{
    /**
     * @brief Entity の ObjectId を保持する参照。0 は参照なし。
     * シーンファイルでは SceneSerializer が alias 参照へ書き換え、読み込み後に同じ Entity の
     * 新しい ObjectId へ張り替える。BeginPlay の時点ではまだ張り替わっていない。
     */
    class EntityRefId
    {
    public:
        static constexpr TypeKind SchemaTypeKind = TypeKind::Integer;
        static constexpr const char* SchemaTypeName = "EntityRefId";

        constexpr EntityRefId() = default;
        explicit constexpr EntityRefId(uint64_t id) noexcept : m_Id(id) {}

        [[nodiscard]] constexpr uint64_t Get() const noexcept { return m_Id; }
        [[nodiscard]] constexpr bool IsValid() const noexcept { return m_Id != 0; }
        constexpr operator uint64_t() const noexcept { return m_Id; }
        [[nodiscard]] constexpr bool operator==(const EntityRefId& other) const noexcept = default;

        bool SerializeToString(Container::String& out) const
        {
            TCHAR digits[20];
            size_t start = 20;
            uint64_t remaining = m_Id;
            do
            {
                digits[--start] = static_cast<TCHAR>('0' + remaining % 10);
                remaining /= 10;
            } while (remaining != 0);
            out = Container::String(Container::StringView(digits + start, 20 - start));
            return true;
        }

        bool DeserializeFromString(const Container::String& in)
        {
            size_t position = 0;
            while (position < in.size() && IsWhitespace(in[position]))
            {
                ++position;
            }
            const size_t digitsStart = position;
            uint64_t value = 0;
            constexpr uint64_t maxValue = (std::numeric_limits<uint64_t>::max)();
            while (position < in.size() && in[position] >= '0' && in[position] <= '9')
            {
                const uint64_t digit = static_cast<uint64_t>(in[position] - '0');
                if (value > (maxValue - digit) / 10)
                {
                    return false;
                }
                value = value * 10 + digit;
                ++position;
            }
            if (position == digitsStart)
            {
                return false;
            }
            while (position < in.size() && IsWhitespace(in[position]))
            {
                ++position;
            }
            if (position != in.size())
            {
                return false;
            }
            m_Id = value;
            return true;
        }

    private:
        static constexpr bool IsWhitespace(TCHAR ch) noexcept
        {
            return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\v' || ch == '\f';
        }

        uint64_t m_Id = 0;
    };

    static_assert(std::is_trivially_copyable_v<EntityRefId>);
    static_assert(std::is_trivially_destructible_v<EntityRefId>);
} // namespace NorvesLib::Core

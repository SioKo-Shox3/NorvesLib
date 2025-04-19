#include "Text/TextField.h"

namespace NorvesLib::Core
{
    TextField::TextField(const char* str)
        : m_Data(str ? str : "")
    {
    }

    TextField::TextField(const Container::String& str)
        : m_Data(str)
    {
    }

    TextField::TextField(Container::StringView view)
        : m_Data(view)
    {
    }

    TextField::TextField(const TextField& other)
        : m_Data(other.m_Data)
    {
    }

    TextField::TextField(TextField&& other) noexcept
        : m_Data(std::move(other.m_Data))
    {
    }

    TextField& TextField::operator=(const TextField& other)
    {
        if (this != &other)
        {
            m_Data = other.m_Data;
        }
        return *this;
    }

    TextField& TextField::operator=(TextField&& other) noexcept
    {
        if (this != &other)
        {
            m_Data = std::move(other.m_Data);
        }
        return *this;
    }

    TextField::~TextField() = default;

    size_t TextField::Length() const
    {
        return m_Data.length();
    }

    bool TextField::IsEmpty() const
    {
        return m_Data.empty();
    }

    const char* TextField::CStr() const
    {
        return m_Data.data();
    }

    Container::StringView TextField::View() const
    {
        return Container::StringView(m_Data);
    }

    Container::String TextField::ToString() const
    {
        return m_Data;
    }

    bool TextField::operator==(const TextField& other) const
    {
        return m_Data == other.m_Data;
    }

    bool TextField::operator!=(const TextField& other) const
    {
        return m_Data != other.m_Data;
    }
} // namespace NorvesLib::Core
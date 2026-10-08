#include "Container/String.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <limits>
using namespace NorvesLib::Core::Container;
namespace
{
    template <typename Char> TString<Char> Text(const char* literal)
    {
        TString<Char> value;
        while (*literal)
            value.push_back(static_cast<Char>(*literal++));
        return value;
    }
    template <typename Char> void Run()
    {
        using String = TString<Char>;
        auto check = [](const String& text, const char* expected) {
            const auto value = Text<Char>(expected);
            assert(text == value && text.size() == value.size());
            for (size_t n = 0; n <= text.size(); ++n)
                assert(text.data()[n] == value.data()[n]);
        };
        String empty;
        empty.replace(0, 0, String{});
        check(empty, "");
        auto value = Text<Char>("abcdef");
        value.replace(2, 2, Text<Char>("XY"));
        check(value, "abXYef");
        value = Text<Char>("abcdef");
        value.replace(2, 1, Text<Char>("XYZ"));
        check(value, "abXYZdef");
        value = Text<Char>("abcdef");
        value.replace(2, 3, Text<Char>("X"));
        check(value, "abXf");
        value = Text<Char>("abcdef");
        value.replace(2, 3, String{});
        check(value, "abf");
        value = Text<Char>("abcdef");
        value.replace(2, String::npos, Text<Char>("X"));
        check(value, "abX");
        value = Text<Char>("abcdef");
        value.replace(6, 0, Text<Char>("XYZ"));
        check(value, "abcdefXYZ");
        value = Text<Char>("abcdef");
        value.replace(2, 2, value);
        check(value, "ababcdefef");
        value = Text<Char>("abcdef");
        value.replace(2, 2, value.c_str() + 1);
        check(value, "abbcdefef");
        value = Text<Char>("abcdef");
        value.replace(0, String::npos, value);
        check(value, "abcdef");
        {
            String binary{static_cast<Char>('A'), Char{}, static_cast<Char>('B')};
            binary.replace(0, 3, binary);
            assert(binary.size() == 3 && binary[0] == static_cast<Char>('A') && binary[1] == Char{} &&
                   binary[2] == static_cast<Char>('B'));
            binary.replace(2, 0, Text<Char>("0123456789"));
            assert(binary.size() == 13 && binary[1] == Char{} && binary[12] == static_cast<Char>('B') &&
                   binary[13] == Char{});
        }
        bool threw = false;
        try
        {
            value.replace(value.size() + 1, 0, String{});
        }
        catch (const std::out_of_range&)
        {
            threw = true;
        }
        assert(threw);
        check(value, "abcdef");
        for (size_t pos = 0; pos <= 6; ++pos)
            for (size_t count = 0; count <= 8; ++count)
            {
                const auto source = Text<Char>("abcdef");
                auto expected = source.substr(0, pos);
                expected.append(source.data(), source.size());
                const size_t tail = pos + std::min(count, source.size() - pos);
                expected.append(source.data() + tail, source.size() - tail);
                value = source;
                value.replace(pos, count, value);
                assert(value == expected && value.size() == expected.size());
                for (size_t n = 0; n <= value.size(); ++n)
                    assert(value.data()[n] == expected.data()[n]);
            }
    }
} // namespace
int main()
{
    Run<char>();
    Run<wchar_t>();
    std::cout << "StringReplaceTest PASS\n";
    return 0;
}

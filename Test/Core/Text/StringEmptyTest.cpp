// 空文字のC文字列境界を、未確保・解放後・ムーブ元で検証する。
#include "Container/String.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
namespace
{
    namespace Container = NorvesLib::Core::Container;
    int Failures = 0;
    void Check(bool bCondition, const char* message)
    {
        if (!bCondition)
        {
            std::fprintf(stderr, "StringEmptyTest: %s\n", message);
            ++Failures;
        }
    }
    template <typename CharT> bool CheckEmpty(const Container::TString<CharT>& value)
    {
        Check(value.empty() && value.size() == 0, "empty_size");
        const auto* data = value.data();
        const auto* text = value.c_str();
        // 旧実装でもまず1要素だけ検査し、不正な非終端領域を走査しない。
        const bool bTerminated = data && text && data[0] == CharT{} && text[0] == CharT{};
        Check(bTerminated, "empty_zero_terminator");
        if (!bTerminated)
        {
            return false;
        }
        Check(std::char_traits<CharT>::length(data) == 0, "data_length");
        Check(std::char_traits<CharT>::length(text) == 0, "c_str_length");
        return true;
    }
    template <typename CharT> void CheckValue(const Container::TString<CharT>& value)
    {
        const bool bContent = value.size() == 2 && value.data() && value.c_str() &&
                              value.data()[0] == static_cast<CharT>('a') &&
                              value.data()[1] == static_cast<CharT>('b') && value.data()[2] == CharT{};
        Check(bContent, "nonempty_content");
        if (bContent)
        {
            Check(value.c_str()[0] == static_cast<CharT>('a') && value.c_str()[1] == static_cast<CharT>('b') &&
                      value.c_str()[2] == CharT{},
                  "nonempty_c_str");
        }
    }
    template <typename CharT> void RunType()
    {
        using Text = Container::TString<CharT>;
        const CharT empty[] = {CharT{}};
        const CharT sample[] = {static_cast<CharT>('a'), static_cast<CharT>('b'), CharT{}};
        Text value;
        CheckEmpty(value);
        Check(value.capacity() == 0, "default_does_not_allocate");
        CheckEmpty(Text(nullptr));
        CheckEmpty(Text(empty));
        Text cleared(sample);
        CheckValue(cleared);
        cleared.clear();
        CheckEmpty(cleared);
        cleared.shrink_to_fit();
        CheckEmpty(cleared);
        Check(cleared.capacity() == 0, "shrink_releases_storage");
        Text source(sample);
        Text moved(std::move(source));
        CheckEmpty(source);
        CheckValue(moved);
        source.append(sample);
        CheckValue(source);
        Text assigned(sample);
        assigned = std::move(moved);
        CheckEmpty(moved);
        CheckValue(assigned);
        moved.append(sample);
        CheckValue(moved);
        Text emptySource;
        assigned = std::move(emptySource);
        CheckEmpty(assigned);
        CheckEmpty(emptySource);
    }
} // 名前空間
int main()
{
    RunType<char>();
    RunType<wchar_t>();
    RunType<char8_t>();
    RunType<char16_t>();
    RunType<char32_t>();
    CheckEmpty(Container::String{});
    CheckEmpty(Container::WideString{});
    const Container::AnsiString empty;
    if (CheckEmpty(empty))
    {
        char buffer[8] = {};
        const int length = std::snprintf(buffer, sizeof(buffer), "[%s]", empty.c_str());
        Check(length == 2 && std::strcmp(buffer, "[]") == 0, "empty_printf");
    }
    if (Failures != 0)
    {
        return 1;
    }
    std::puts("STRING_EMPTY result=pass types=5 default_clear_shrink_move_reuse_printf");
    return 0;
}

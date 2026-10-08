// 実JsonDocumentのraw/escape/native文字と失敗時Resetを検証する。
#include "Text/JsonDocument.h"
#include "Text/UnicodeText.h"
#include "Text/JsonUnicodeScalar.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::abort(); } } while(false)

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::TextDetail;
bool Parse(const char* text,JsonDocument& document)
{
    return JsonDocument::TryParseUtf8({reinterpret_cast<const uint8_t*>(text),std::strlen(text)},document);
}
Container::String Native(const char* text)
{
    Container::String result;
    CHECK(ForEachUnicodeScalar<char>({text,std::strlen(text)},[&](uint32_t scalar)
    {
        const auto units=EncodeJsonUnicodeScalar<Container::String::value_type>(scalar);
        for (size_t i=0;i<units.Count;++i)
        {
            result.push_back(units.Units[i]);
        }
    }));
    return result;
}
void Equal(const Container::String& a,const Container::String& b)
{
    CHECK(a.size()==b.size());
    for (size_t i=0;i<a.size();++i)
    {
        CHECK(a[i]==b[i]);
    }
}
int main()
{
    JsonDocument raw,escaped,native;
    const char* text=reinterpret_cast<const char*>(u8R"({"name":"骨🐺é丢Ċ","n":-1.25e2})");
    CHECK(Parse(text,raw));
    CHECK(Parse("{\"name\":\"\\u9aa8\\ud83d\\udc3a\\u00e9\\u4e22\\u010a\",\"n\":-1.25e2}",escaped));
    CHECK(JsonDocument::TryParse(Native(text),native));
    Equal(raw.GetRoot().FindMember("name").AsString(),Native(reinterpret_cast<const char*>(u8"骨🐺é丢Ċ")));
    Equal(raw.GetRoot().FindMember("name").AsString(),escaped.GetRoot().FindMember("name").AsString());
    Equal(raw.GetRoot().FindMember("name").AsString(),native.GetRoot().FindMember("name").AsString());
    CHECK(raw.GetRoot().FindMember("n").AsNumber()==-125);
    CHECK(Parse("\"A\\u0000B\"",raw));
    const auto& nul=raw.GetRoot().AsString();CHECK(nul.size()==3 && nul[0]=='A' && nul[1]==0 && nul[2]=='B');
    const char* longNulJson=R"({"A\u0000BCDEFGHIJKLMNOPQRSTUVWXYZ":"A\u0000BCDEFGHIJKLMNOPQRSTUVWXYZ","escaped":"\\\"tail","growth":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15]})";
    CHECK(Parse(longNulJson,raw));
    const char expectedNul[]="A\0BCDEFGHIJKLMNOPQRSTUVWXYZ";
    const auto& longKey=raw.GetRoot().GetMemberName(0);
    const auto& longValue=raw.GetRoot().GetMemberValue(0).AsString();
    CHECK(longKey.size()==sizeof(expectedNul)-1 && longValue.size()==sizeof(expectedNul)-1);
    for (size_t i=0;i<sizeof(expectedNul)-1;++i)
    {
        CHECK(longKey[i]==expectedNul[i] && longValue[i]==expectedNul[i]);
    }
    Equal(raw.GetRoot().FindMember("escaped").AsString(),Native("\\\"tail"));
    CHECK(Parse("{}",raw));
    const uint8_t bom[]={0xef,0xbb,0xbf,'{','}'};
    CHECK(!JsonDocument::TryParseUtf8(bom,raw) && !raw.GetRoot().IsValid());
    CHECK(Parse("{}",raw));
    Container::String invalidNative;
    if constexpr (sizeof(Container::String::value_type)==1)
    {
        invalidNative.push_back(static_cast<Container::String::value_type>(0xff));
    }
    else
    {
        invalidNative.push_back(static_cast<Container::String::value_type>(0xd800));
    }
    CHECK(!JsonDocument::TryParse(invalidNative,raw) && !raw.GetRoot().IsValid());
    const char invalid[]={'"',static_cast<char>(0xc0),static_cast<char>(0x80),'"',0};
    CHECK(!Parse(invalid,raw) && !raw.GetRoot().IsValid());
    CHECK(!Parse("\"\\ud800\"",raw) && !raw.GetRoot().IsValid());
    CHECK(!Parse("\v{}",raw));CHECK(!Parse("{}\f",raw));
    CHECK(Parse("{}",raw));
    const uint8_t literalNul[]={'"','A',0,'B','"'};
    CHECK(!JsonDocument::TryParseUtf8(literalNul,raw) && !raw.GetRoot().IsValid());
    CHECK(!JsonDocument::TryParseUtf8({nullptr,1},raw));
    CHECK(Parse(" [ true, false, null, 0, -0.1, 1e20 ] \r\n",raw));
    std::puts("JsonUnicodeInputTest PASS: native_raw_escape_reset_number_json_whitespace");
    return 0;
}

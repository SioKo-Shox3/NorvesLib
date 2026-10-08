#include "Resource/SkeletalInfluenceAttributes.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <cwchar>
#include <iostream>
using namespace NorvesLib::Core::Skeletal;
int main()
{
    const auto parse=[](const char* name) { return ClassifyInfluenceAttributeName<char>({name,std::strlen(name)}); };
    assert(parse("JOINTS_0").Kind==InfluenceAttributeKind::Joints && parse("JOINTS_0").SetIndex==0);
    assert(parse("WEIGHTS_0").Kind==InfluenceAttributeKind::Weights && parse("WEIGHTS_0").SetIndex==0);
    assert(parse("JOINTS_1").Kind==InfluenceAttributeKind::Joints && parse("JOINTS_1").SetIndex==1);
    assert(parse("WEIGHTS_42").Kind==InfluenceAttributeKind::Weights && parse("WEIGHTS_42").SetIndex==42);
    assert(parse("JOINTS_4294967295").SetIndex==4294967295u);
    for (const char* name : {"JOINTS_", "WEIGHTS_", "JOINTS_-1", "JOINTS_01", "WEIGHTS_00", "WEIGHTS_x",
        "JOINTS_1.0", "JOINTS_+1", "JOINTS_4294967296", "WEIGHTS_999999999999999999999999"})
    {
        assert(parse(name).Kind==InfluenceAttributeKind::Invalid);
    }
    for (const char* name : {"", "POSITION", "JOINTS", "_JOINTS_1", "_WEIGHTS_1", "MY_JOINTS_1", "joints_1"})
    {
        assert(parse(name).Kind==InfluenceAttributeKind::Other);
    }
    const wchar_t* wide=L"WEIGHTS_12";
    const auto parsed=ClassifyInfluenceAttributeName<wchar_t>({wide,std::wcslen(wide)});
    assert(parsed.Kind==InfluenceAttributeKind::Weights && parsed.SetIndex==12);
    const char embeddedNul[]={'J','O','I','N','T','S','_','1',0,'2'};
    assert(ClassifyInfluenceAttributeName<char>(embeddedNul).Kind==InfluenceAttributeKind::Invalid);
    assert(ClassifyInfluenceAttributeName<char>({nullptr,size_t{8}}).Kind==InfluenceAttributeKind::Invalid);
    std::cout << "SkeletalInfluenceAttributeTest PASS: base_additional_invalid_custom_wide_overflow\n";
    return 0;
}

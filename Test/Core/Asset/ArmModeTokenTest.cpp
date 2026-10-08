// ARM設定文字列の全消費・有限値・失敗保持を検証する。
#include "Resource/MaterialImportSettings.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#undef assert
#define assert(expression) \
    do \
    { \
        if (!(expression)) \
        { \
            std::fprintf(stderr,"assertion: %s line=%d\n",#expression,__LINE__); \
            std::abort(); \
        } \
    } while (false)
using namespace NorvesLib::Core::AssetImport;
using Status=MaterialSettingsStatus;
int main()
{
    ArmChannelPolicy out;
    struct Case { const char* Text; ArmMode Mode; double Value; };
    const Case cases[]={{"texture",ArmMode::Texture,0},{"ignore",ArmMode::Ignore,0},{"auto",ArmMode::Auto,0},
        {"constant:0",ArmMode::Constant,0},{"constant:-0",ArmMode::Constant,0},{"constant:1",ArmMode::Constant,1},
        {"constant:5e-1",ArmMode::Constant,.5},{"constant:.25",ArmMode::Constant,.25}};
    for (const auto& c:cases)
    {
        assert(ParseArmModeToken({c.Text,std::strlen(c.Text)},out)==Status::Success);
        assert(out.Mode==c.Mode && out.Constant==c.Value && out.AutoWidth==4.0/255.0);
    }
    out={ArmMode::Constant,.42,.37};
    for (const char* bad:{"","AUTO"," texture","auto ","constant:","constant:NaN","constant:inf","constant:1e9999",
        "constant:1e-9999","constant:-.1","constant:1.1","constant:0.2x","constant: 0.2","constant:+0.2","constant:0x1"})
    {
        assert(ParseArmModeToken({bad,std::strlen(bad)},out)==Status::InvalidArmPolicy);
        assert(out.Mode==ArmMode::Constant && out.Constant==.42 && out.AutoWidth==.37);
    }
    const char nul[]={'a','u','t','o',0,'x'};
    assert(ParseArmModeToken({nul,sizeof(nul)},out)==Status::InvalidArmPolicy);
    assert(ParseArmModeToken({nullptr,1},out)==Status::InvalidArmPolicy);
    const char bounded[]={'c','o','n','s','t','a','n','t',':','0','.','5','x'};
    assert(ParseArmModeToken({bounded,sizeof(bounded)-1},out)==Status::Success && out.Constant==.5);
    std::puts("ArmModeTokenTest PASS: exact_tokens_finite_range_full_consumption_atomic");
    return 0;
}

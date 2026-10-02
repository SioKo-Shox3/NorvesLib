#include "Input/InputBindingNames.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <limits>
#include <iostream>
using namespace NorvesLib::Core::Input;
int main()
{
    for(uint16_t kind=0;kind<256;++kind)
    {
        const auto source=static_cast<EInputBindingSource>(kind);
        const auto* sourceName=GetInputSourceName(source);
        assert((sourceName!=nullptr)==(kind<7));
        if(sourceName)
        {
            EInputBindingSource parsed=static_cast<EInputBindingSource>(255);
            assert(TryParseInputSourceName(sourceName,std::strlen(sourceName),parsed) && parsed==source);
        }
        for(const auto& entry:GetInputCodeNames(source))
        {
            uint16_t parsed=65535;
            assert(IsValidPhysicalSource({source,entry.Code,0}));
            assert(TryParseInputCodeName(source,entry.Name,std::strlen(entry.Name),parsed) && parsed==entry.Code);
            for(const auto& other:GetInputCodeNames(source))
                assert(entry.Code==other.Code || std::strcmp(entry.Name,other.Name)!=0);
        }
        if(kind>=7) assert(GetInputCodeNames(source).empty());
    }
    for(uint8_t kind=0;kind<7;++kind)
    {
        const auto source=static_cast<EInputBindingSource>(kind);
        for(uint32_t number=0;number<=65535;++number)
        {
            const auto code=static_cast<uint16_t>(number);
            const bool valid=IsValidPhysicalSource({source,code,0});
            assert((GetInputCodeName(source,code)!=nullptr)==valid);
            uint16_t parsed=43210;
            assert(TryParseInputCodeNumber(source,number,parsed)==valid);
            assert(parsed==(valid?code:43210));
        }
        uint16_t parsed=12345;
        for(double bad:{-1.0,0.5,65536.0,1e100,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
        {
            assert(!TryParseInputCodeNumber(source,bad,parsed) && parsed==12345);
        }
        assert(!TryParseInputCodeName(source,nullptr,0,parsed) && parsed==12345);
        assert(!TryParseInputCodeName(source,"unknown",7,parsed) && parsed==12345);
    }
    uint16_t code=0;
    const char slice[]={'W','x'};
    assert(TryParseInputCodeName(EInputBindingSource::Key,slice,1,code) && code==static_cast<uint16_t>(KeyCode::W));
    assert(!TryParseInputCodeName(EInputBindingSource::Key,slice,2,code));
    assert(!TryParseInputCodeName(EInputBindingSource::Key,"w",1,code));
    for(uint32_t number=0;number<256;++number)
    {
        uint8_t slot=254;
        assert(TryParseInputSlotNumber(number,slot)==(number<4));
        assert(slot==(number<4?number:254));
    }
    for(double bad:{-1.0,0.1,4.0,256.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
    {
        uint8_t slot=254;assert(!TryParseInputSlotNumber(bad,slot) && slot==254);
    }
    EInputBindingSource source=EInputBindingSource::MouseWheel;
    assert(!TryParseInputSourceName("Key",3,source) && source==EInputBindingSource::MouseWheel);
    float scalar=42;
    assert(!TryParseInputFloatNumber(1.000000001,scalar,0,1) && scalar==42);
    assert(!TryParseInputFloatNumber(-1e-100,scalar,0,1) && scalar==42);
    assert(!TryParseInputFloatNumber(-1e-100,scalar,0) && scalar==42);
    assert(!TryParseInputFloatNumber(1e-100,scalar,0,std::numeric_limits<float>::max(),true) && scalar==42);
    assert(!TryParseInputFloatNumber(0.999999999,scalar,0,1,false,true) && scalar==42);
    assert(!TryParseInputFloatNumber(std::numeric_limits<double>::max(),scalar) && scalar==42);
    assert(!TryParseInputFloatNumber(0,scalar,1,0) && scalar==42);
    assert(!TryParseInputFloatNumber(0,scalar,0,std::numeric_limits<double>::infinity()) && scalar==42);
    assert(TryParseInputFloatNumber(0,scalar,0,1) && scalar==0);
    assert(TryParseInputFloatNumber(1,scalar,0,1) && scalar==1);
    assert(TryParseInputFloatNumber(1e-100,scalar,0,1) && scalar==0);
    const float belowOne=std::nextafter(1.0f,0.0f);
    assert(TryParseInputFloatNumber(belowOne,scalar,0,1,false,true) && scalar==belowOne);
    assert(TryParseInputFloatNumber(-1e-100,scalar) && scalar==0 && std::signbit(scalar));
    std::cout << "InputBindingNamesTest passed\n";
    return 0;
}

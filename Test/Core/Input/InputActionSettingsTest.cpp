#include "Application/CursorMode.h"
#include "Input/InputActionSettings.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <limits>
using namespace NorvesLib;
using namespace NorvesLib::Core::Input;
int main()
{
    for(uint16_t code=0;code<256;++code)
        assert(IsValidCursorMode(static_cast<ECursorMode>(code))==(code<4));
    InputActionSettings settings;
    assert(IsValidInputActionSettings(settings));
    assert(IsValidInputActionBindings(settings,{}));
    InputBinding key;
    key.Source={EInputBindingSource::Key,static_cast<uint16_t>(KeyCode::W),0};
    InputBinding bindings[2]={key,key};
    assert(IsValidInputActionBindings(settings,bindings));
    bindings[1].Source.Code=0;
    assert(!IsValidInputActionBindings(settings,bindings));
    bindings[1]=key;
    for(uint16_t type=0;type<256;++type)
        for(uint16_t output=0;output<256;++output)
        {
            settings.Type=static_cast<EInputMappingValueType>(type);
            settings.Output=static_cast<EInputAxisOutput>(output);
            const bool expected=type<3 && output<2 && (type!=0 || output==0);
            assert(IsValidInputActionSettings(settings)==expected);
        }
    settings={};
    const float nan=std::numeric_limits<float>::quiet_NaN();
    const float inf=std::numeric_limits<float>::infinity();
    for(float bad:{nan,inf,-inf,-0.1f})
    {
        settings.MouseSensitivity=bad;
        assert(!IsValidInputActionSettings(settings));
        settings.MouseSensitivity=1;settings.RateSensitivity=bad;
        assert(!IsValidInputActionSettings(settings));
        settings.RateSensitivity=1;
    }
    settings.MouseSensitivity=0;settings.RateSensitivity=0;
    assert(IsValidInputActionSettings(settings));
    settings.ButtonTiming.HoldSeconds=0;
    assert(!IsValidInputActionSettings(settings));
    settings.ButtonTiming={};settings.AxisResponse.DeadZone=1;
    assert(!IsValidInputActionSettings(settings));
    settings.AxisResponse={};settings.Type=EInputMappingValueType::Axis2D;
    settings.Output=EInputAxisOutput::FrameDelta;
    bindings[0].Source={EInputBindingSource::MouseDelta,0,0};
    bindings[1].Source={EInputBindingSource::MouseDelta,1,0};
    bindings[1].Component=EInputAxisComponent::Y;
    assert(IsValidInputActionBindings(settings,bindings));
    settings.Output=EInputAxisOutput::Normalized;
    assert(!IsValidInputActionBindings(settings,bindings));
    std::cout << "InputActionSettingsTest passed\n";
    return 0;
}

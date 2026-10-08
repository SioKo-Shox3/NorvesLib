#include "Input/InputBindingTypes.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <limits>

using namespace NorvesLib::Core::Input;

int main()
{
    const uint16_t buttons[] = {1,2,4,8,16,32,64,128,256,512,4096,8192,16384,32768};
    uint16_t combined=0;
    for(auto button:buttons) combined |= button;
    assert(combined==AllGamepadButtons && GamepadSlotCount==4);
    for(uint32_t code=0; code<=65535; ++code)
    {
        bool expectedButton=false;
        for(auto button:buttons) expectedButton |= code==button;
        assert(IsValidGamepadButton(static_cast<uint16_t>(code))==expectedButton);
        for(uint8_t slot : {uint8_t(0),uint8_t(3),uint8_t(4),uint8_t(255)})
        {
            const auto validPad=slot<4;
            const auto code16=static_cast<uint16_t>(code);
            assert(IsValidPhysicalSource({EInputBindingSource::GamepadButton,code16,slot}) == (validPad && expectedButton));
            assert(IsValidPhysicalSource({EInputBindingSource::GamepadAxis,code16,slot}) == (validPad && code<4));
            assert(IsValidPhysicalSource({EInputBindingSource::GamepadTrigger,code16,slot}) == (validPad && code<2));
            assert(IsValidPhysicalSource({EInputBindingSource::Key,code16,slot}) ==
                (slot==0 && code>0 && code<static_cast<uint32_t>(KeyCode::Count)));
            assert(IsValidPhysicalSource({EInputBindingSource::MouseButton,code16,slot}) ==
                (slot==0 && code<static_cast<uint32_t>(MouseButton::Count)));
            assert(IsValidPhysicalSource({EInputBindingSource::MouseDelta,code16,slot}) == (slot==0 && code<2));
            assert(IsValidPhysicalSource({EInputBindingSource::MouseWheel,code16,slot}) == (slot==0 && code<2));
        }
    }
    assert(!IsValidPhysicalSource({static_cast<EInputBindingSource>(255),0,0}));
    assert(!IsValidPhysicalSource({}));
    GamepadState state;
    assert(IsValidGamepadState(state));
    state.PacketNumber=std::numeric_limits<uint32_t>::max();
    assert(IsValidGamepadState(state));
    state.Connected=true;state.Buttons=AllGamepadButtons;
    state.Axes[0]=-1;state.Axes[1]=1;state.Triggers[0]=0;state.Triggers[1]=1;
    assert(IsValidGamepadState(state));
    state.Buttons|=0x0400;
    assert(!IsValidGamepadState(state));state.Buttons=0;
    const float nan=std::numeric_limits<float>::quiet_NaN();
    const float inf=std::numeric_limits<float>::infinity();
    for(uint8_t index=0;index<4;++index)
    {
        for(float invalid : {nan,inf,-inf,-1.01f,1.01f})
        {
            state.Axes[index]=invalid;assert(!IsValidGamepadState(state));
        }
        state.Axes[index]=0;
    }
    for(uint8_t index=0;index<2;++index)
    {
        for(float invalid : {nan,inf,-inf,-0.01f,1.01f})
        {
            state.Triggers[index]=invalid;assert(!IsValidGamepadState(state));
        }
        state.Triggers[index]=0;
    }
    state.Connected=false;
    assert(IsValidGamepadState(state));
    state.Buttons=1;assert(!IsValidGamepadState(state));state.Buttons=0;
    state.Axes[0]=0.1f;assert(!IsValidGamepadState(state));state.Axes[0]=0;
    state.Triggers[0]=0.1f;assert(!IsValidGamepadState(state));state.Triggers[0]=0;

    InputBinding binding;
    binding.Source={EInputBindingSource::Key,static_cast<uint16_t>(KeyCode::W),0};
    const auto normalized=EInputAxisOutput::Normalized;
    const auto frameDelta=EInputAxisOutput::FrameDelta;
    for(auto type : {EInputMappingValueType::Button,EInputMappingValueType::Axis1D,EInputMappingValueType::Axis2D})
    {
        for(auto component : {EInputAxisComponent::X,EInputAxisComponent::Y})
        {
            binding.Component=component;
            for(uint16_t mask=0;mask<256;++mask)
            {
                binding.RequiredModifiers=static_cast<uint8_t>(mask);
                const bool dimension=component==EInputAxisComponent::X || type==EInputMappingValueType::Axis2D;
                assert(IsValidInputBinding(binding,type,normalized)==(dimension && mask<8));
                assert(IsValidInputBinding(binding,type,frameDelta)==(dimension && mask<8 && type!=EInputMappingValueType::Button));
            }
        }
    }
    binding.Component=EInputAxisComponent::X;binding.RequiredModifiers=0;
    for(auto source : {EInputBindingSource::MouseDelta,EInputBindingSource::MouseWheel})
    {
        binding.Source={source,0,0};
        assert(IsValidInputBinding(binding,EInputMappingValueType::Axis1D,frameDelta));
        assert(!IsValidInputBinding(binding,EInputMappingValueType::Axis1D,normalized));
        assert(IsValidInputBinding(binding,EInputMappingValueType::Button,normalized));
    }
    binding.Source={EInputBindingSource::Key,static_cast<uint16_t>(KeyCode::W),0};
    binding.Scale=-2;binding.Invert=true;
    assert(IsValidInputBinding(binding,EInputMappingValueType::Axis2D,normalized));
    binding.Scale=0;
    assert(IsValidInputBinding(binding,EInputMappingValueType::Axis2D,normalized));
    for(float invalid : {nan,inf,-inf})
    {
        binding.Scale=invalid;
        assert(!IsValidInputBinding(binding,EInputMappingValueType::Axis1D,normalized));
    }
    binding.Scale=1;
    for(float invalid : {nan,inf,-0.1f,1.1f})
    {
        binding.ButtonThreshold=invalid;
        assert(!IsValidInputBinding(binding,EInputMappingValueType::Button,normalized));
    }
    binding.ButtonThreshold=0;
    assert(IsValidInputBinding(binding,EInputMappingValueType::Button,normalized));
    binding.ButtonThreshold=1;
    assert(IsValidInputBinding(binding,EInputMappingValueType::Button,normalized));
    binding.ButtonThreshold=std::nextafter(0.0f,-1.0f);
    assert(!IsValidInputBinding(binding,EInputMappingValueType::Button,normalized));
    binding.ButtonThreshold=std::nextafter(1.0f,2.0f);
    assert(!IsValidInputBinding(binding,EInputMappingValueType::Button,normalized));
    binding.ButtonThreshold=0.5f;
    assert(!IsValidInputBinding(binding,static_cast<EInputMappingValueType>(255),normalized));
    assert(!IsValidInputBinding(binding,EInputMappingValueType::Button,static_cast<EInputAxisOutput>(255)));
    binding.Component=static_cast<EInputAxisComponent>(255);
    assert(!IsValidInputBinding(binding,EInputMappingValueType::Axis2D,normalized));
    std::cout << "InputBindingTypesTest passed\n";
    return 0;
}

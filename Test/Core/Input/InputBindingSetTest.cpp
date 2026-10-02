#include "Input/InputBindingSet.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
using namespace NorvesLib;
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Input;
using namespace NorvesLib::Core::literals;
int main()
{
    InputBindingSet settings;
    assert(!settings.AddContext(Identity{},ECursorMode::Normal));
    assert(!settings.AddContext("Bad"_id,static_cast<ECursorMode>(255)));
    assert(settings.GetContexts().empty());
    assert(settings.AddContext("Gameplay"_id,ECursorMode::Locked));
    assert(settings.AddContext("Menu"_id,ECursorMode::Normal));
    assert(!settings.AddContext("Gameplay"_id,ECursorMode::Normal));
    assert(settings.GetContexts().size()==2);
    assert(!settings.SetContextCursorMode("Unknown"_id,ECursorMode::Normal));
    assert(!settings.SetContextCursorMode("Gameplay"_id,static_cast<ECursorMode>(255)));
    assert(settings.FindContext("Gameplay"_id)->CursorMode==ECursorMode::Locked);
    assert(settings.SetContextCursorMode("Gameplay"_id,ECursorMode::Confined));
    assert(settings.FindContext("Gameplay"_id)->CursorMode==ECursorMode::Confined);
    InputActionDefinition action;
    action.Id="Move"_id;action.Settings.Type=EInputMappingValueType::Axis2D;
    InputBinding forward;
    forward.Source={EInputBindingSource::Key,static_cast<uint16_t>(KeyCode::W),0};
    forward.Component=EInputAxisComponent::Y;
    InputBinding backward=forward;
    backward.Source.Code=static_cast<uint16_t>(KeyCode::S);backward.Scale=-1;
    action.Bindings.push_back(forward);action.Bindings.push_back(backward);
    assert(settings.AddAction("Gameplay"_id,action));
    assert(!settings.AddAction("Gameplay"_id,action));
    assert(!settings.AddAction("Unknown"_id,action));
    action.Bindings.clear();action.Settings.MouseSensitivity=5;
    assert(settings.FindAction("Gameplay"_id,"Move"_id)->Bindings.size()==2);
    assert(settings.FindAction("Gameplay"_id,"Move"_id)->Settings.MouseSensitivity==1);
    // 同じaction名はcontextを跨いで別定義できる。
    assert(settings.AddAction("Menu"_id,action));
    assert(settings.FindAction("Menu"_id,"Move"_id)->Bindings.empty());
    assert(!settings.FindAction("Unknown"_id,"Move"_id));
    assert(!settings.FindAction("Gameplay"_id,"Missing"_id));
    auto invalid=*settings.FindAction("Gameplay"_id,"Move"_id);
    invalid.Settings.Type=EInputMappingValueType::Button;
    assert(!settings.ReplaceAction("Gameplay"_id,invalid));
    assert(settings.FindAction("Gameplay"_id,"Move"_id)->Settings.Type==EInputMappingValueType::Axis2D);
    invalid=*settings.FindAction("Gameplay"_id,"Move"_id);invalid.Id=Identity{};
    assert(!settings.AddAction("Gameplay"_id,invalid));
    Container::VariableArray<InputBinding> badBindings={forward};
    badBindings[0].Source.Code=0;
    assert(!settings.SetBindings("Gameplay"_id,"Move"_id,badBindings));
    assert(settings.FindAction("Gameplay"_id,"Move"_id)->Bindings.size()==2);
    assert(!settings.SetBindings("Gameplay"_id,"Missing"_id,{}));
    // 内部viewを入力に渡しても、先にcopyしてから置き換える。
    const auto* borrowed=settings.FindAction("Gameplay"_id,"Move"_id);
    assert(settings.ReplaceAction("Gameplay"_id,*borrowed));
    borrowed=settings.FindAction("Gameplay"_id,"Move"_id);
    assert(settings.SetBindings("Gameplay"_id,"Move"_id,borrowed->Bindings));
    assert(settings.FindAction("Gameplay"_id,"Move"_id)->Bindings.size()==2);
    InputBindingSet copy=settings;
    assert(copy.SetBindings("Gameplay"_id,"Move"_id,{}));
    assert(copy.FindAction("Gameplay"_id,"Move"_id)->Bindings.empty());
    assert(settings.FindAction("Gameplay"_id,"Move"_id)->Bindings.size()==2);
    auto replacement=*settings.FindAction("Gameplay"_id,"Move"_id);
    replacement.Settings.Output=EInputAxisOutput::FrameDelta;
    replacement.Settings.MouseSensitivity=0.25f;replacement.Settings.RateSensitivity=180;
    replacement.Bindings[0].Source={EInputBindingSource::MouseDelta,0,0};
    replacement.Bindings[0].Component=EInputAxisComponent::X;
    replacement.Bindings[1].Source={EInputBindingSource::MouseDelta,1,0};
    assert(settings.ReplaceAction("Gameplay"_id,replacement));
    assert(settings.FindAction("Gameplay"_id,"Move"_id)->Settings.MouseSensitivity==0.25f);
    assert(copy.FindAction("Gameplay"_id,"Move"_id)->Settings.MouseSensitivity==1);
    settings.Clear();
    assert(settings.GetContexts().empty());
    assert(copy.GetContexts().size()==2 && copy.FindAction("Menu"_id,"Move"_id));
    std::cout << "InputBindingSetTest passed\n";
    return 0;
}

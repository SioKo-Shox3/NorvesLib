#include "Input/MayaCameraController.h"
#include "Input/LightController.h"
#include "Input/InputSystem.h"
#include "Input/InputRouter.h"
#include "Game/Input/PickingController.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>
namespace Game::Input
{
    struct PickingInputResetTestAccess
    {
        static void Run()
        {
            PickingController picker;
            NorvesLib::Core::Input::InputSystem system;NorvesLib::Core::Input::InputRouter router;
            system.SetRouter(&router);router.RegisterController(&picker,0);
            const NorvesLib::Core::EntityHandle selected{3,7};
            picker.m_SelectionHandles.push_back(selected);
            picker.m_bHasSelectionSphere=true;
            picker.m_SelectionSphere.Radius=4.0f;
            picker.m_FallbackSelectionDepth=12.0f;
            picker.m_bLeftPressed=true;picker.m_bBoxSelecting=true;
            system.ReleaseAll();
            assert(!picker.m_bLeftPressed && !picker.m_bBoxSelecting && !picker.m_bSphereSelecting);
            assert(picker.m_SelectionHandles.size()==1 && picker.m_SelectionHandles[0]==selected);
            assert(picker.m_bHasSelectionSphere && picker.m_SelectionSphere.Radius==4 && picker.m_FallbackSelectionDepth==12);
            // reset後の遅延releaseで新たなpickを実行して確定selectionを消さない。
            picker.OnMouseButton({NorvesLib::Core::Input::MouseButton::Left,NorvesLib::Core::Input::InputAction::Released});
            assert(picker.m_SelectionHandles.size()==1 && picker.m_SelectionHandles[0]==selected);
            picker.m_bSphereSelecting=true;picker.m_bHasSelectionSphere=true;
            system.ReleaseAll();system.ReleaseAll();
            assert(!picker.m_bSphereSelecting && !picker.m_bHasSelectionSphere && picker.m_SelectionHandles.size()==1);
            router.UnregisterController(&picker);system.SetRouter(nullptr);
        }
    };
}
int main()
{
    using namespace NorvesLib::Core::Input;
    InputSystem system;InputRouter router;system.SetRouter(&router);
    MayaCameraController camera;router.RegisterController(&camera,0);
    for(auto button:{MouseButton::Left,MouseButton::Middle,MouseButton::Right})
        camera.OnMouseButton({button,InputAction::Pressed});
    const auto target=camera.GetTarget();const float yaw=camera.GetYaw(),pitch=camera.GetPitch(),distance=camera.GetDistance();
    system.ReleaseAll();system.ReleaseAll();
    assert(!camera.OnMouseMove({30,40,10,20}));
    assert(camera.GetYaw()==yaw && camera.GetPitch()==pitch && camera.GetDistance()==distance);
    assert(camera.GetTarget().x==target.x && camera.GetTarget().y==target.y && camera.GetTarget().z==target.z);
    camera.OnMouseButton({MouseButton::Left,InputAction::Pressed});
    assert(camera.OnMouseMove({40,40,10,0}) && camera.GetYaw()!=yaw);
    NorvesLib::Core::Rendering::LightProxy proxy;
    LightController light;light.SetTargetLight(&proxy);router.RegisterController(&light,0);
    for(auto code:{KeyCode::Left,KeyCode::Right,KeyCode::Down,KeyCode::Up,KeyCode::Equal,KeyCode::Minus})
    {
        light.OnKey({code,InputAction::Pressed});light.OnKey({KeyCode::LeftShift,InputAction::Pressed});
        const float lyaw=light.GetYaw(),lpitch=light.GetPitch(),intensity=proxy.CanonicalIntensity;
        system.ReleaseAll();system.ReleaseAll();light.Update(0.25f);
        assert(light.GetYaw()==lyaw && light.GetPitch()==lpitch && proxy.CanonicalIntensity==intensity);
        assert(light.GetTargetLight()==&proxy);
    }
    const auto lyaw=light.GetYaw();
    light.OnKey({KeyCode::Right,InputAction::Pressed});light.Update(0.1f);
    assert(std::abs(light.GetYaw()-lyaw-9.0f)<0.001f); // 前回Shiftの3倍速を持ち越さない。
    router.UnregisterController(&light);router.UnregisterController(&camera);system.SetRouter(nullptr);
    Game::Input::PickingInputResetTestAccess::Run();
    std::cout << "LegacyInputResetTest passed\n";
    return 0;
}

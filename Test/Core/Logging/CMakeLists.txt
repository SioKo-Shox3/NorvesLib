# Link the threading library because the async case spins up JobSystem worker threads.
find_package(Threads REQUIRED)

norves_add_test_bundle(LoggerSinkTest
    FOLDER "Test/Core/Logging"
    MEMBER LoggerSinkTest LoggerSinkTest.cpp
    MEMBER StatsManagerTest ${CMAKE_SOURCE_DIR}/Test/Core/Debug/StatsManagerTest.cpp
    MEMBER DelegateIdentityTest ${CMAKE_SOURCE_DIR}/Test/Core/Delegate/DelegateIdentityTest.cpp
    MEMBER DelegateCopyTest ${CMAKE_SOURCE_DIR}/Test/Core/Delegate/DelegateCopyTest.cpp
    MEMBER InputRouterTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/InputRouterTest.cpp
    MEMBER InputStateReleaseTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/InputStateReleaseTest.cpp
    MEMBER InputAxisMathTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/InputAxisMathTest.cpp
    MEMBER InputButtonStateTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/InputButtonStateTest.cpp
    MEMBER InputArmedStateTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/InputArmedStateTest.cpp
    MEMBER InputRebindCaptureStateTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/InputRebindCaptureStateTest.cpp
    MEMBER InputRebindCaptureManagerTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/InputRebindCaptureManagerTest.cpp
    MEMBER InputDebugOverlayStateTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/InputDebugOverlayStateTest.cpp
    MEMBER InputDebugOverlayControllerTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/InputDebugOverlayControllerTest.cpp
    MEMBER InputBindingTypesTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/InputBindingTypesTest.cpp
    MEMBER GamepadInputStateTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/GamepadInputStateTest.cpp
    MEMBER GamepadStateNormalizeTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/GamepadStateNormalizeTest.cpp
    MEMBER XInputDevicePollingTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/XInputDevicePollingTest.cpp
    MEMBER XInputVibrationStateTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/XInputVibrationStateTest.cpp
    MEMBER HapticsMixerTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/HapticsMixerTest.cpp
    MEMBER HapticsPlaybackTimeTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/HapticsPlaybackTimeTest.cpp
    MEMBER HapticsServiceTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/HapticsServiceTest.cpp
    MEMBER ActiveDeviceKindIntegrationTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/ActiveDeviceKindIntegrationTest.cpp
    MEMBER ActiveDeviceKindTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/ActiveDeviceKindTest.cpp
    MEMBER HapticsJsonTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/HapticsJsonTest.cpp
    MEMBER XInputDeviceIntegrationTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/XInputDeviceIntegrationTest.cpp
    MEMBER InputActionSettingsTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/InputActionSettingsTest.cpp
    MEMBER AbsoluteMouseTrackingTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/AbsoluteMouseTrackingTest.cpp
    MEMBER RawInputStateTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/RawInputStateTest.cpp
    MEMBER InputRoutingExtensionTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/InputRoutingExtensionTest.cpp
    MEMBER InputActionMapTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/InputActionMapTest.cpp
    MEMBER InputActionRuntimeTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/InputActionRuntimeTest.cpp
    MEMBER InputBindingNamesTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/InputBindingNamesTest.cpp
    MEMBER CameraInputResetTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/CameraInputResetTest.cpp
    MEMBER LegacyInputResetTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/LegacyInputResetTest.cpp
    MEMBER GameInputSettingsTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/GameInputSettingsTest.cpp
    MEMBER GameInputRebindTypesTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/GameInputRebindTypesTest.cpp
    MEMBER InputBindingPersistenceTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/InputBindingPersistenceTest.cpp
    MEMBER WorkingDirectoryInputBindingStoreTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/WorkingDirectoryInputBindingStoreTest.cpp
    MEMBER InputBindingJsonTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/InputBindingJsonTest.cpp
    MEMBER InputBindingSetTest ${CMAKE_SOURCE_DIR}/Test/Core/Input/InputBindingSetTest.cpp
    MEMBER ModuleRegistryTest ${CMAKE_SOURCE_DIR}/Test/Core/Module/ModuleRegistryTest.cpp
    MEMBER IdentityLiteralTest ${CMAKE_SOURCE_DIR}/Test/Core/Text/IdentityLiteralTest.cpp
    MEMBER JsonWriterTest ${CMAKE_SOURCE_DIR}/Test/Core/Text/JsonWriterTest.cpp
    MEMBER JsonUnicodeScalarTest ${CMAKE_SOURCE_DIR}/Test/Core/Text/JsonUnicodeScalarTest.cpp
    MEMBER CriticalDampingTest ${CMAKE_SOURCE_DIR}/Test/Core/Math/CriticalDampingTest.cpp
    MEMBER RenderTransformHistoryTest ${CMAKE_SOURCE_DIR}/Test/Core/Object/RenderTransformHistoryTest.cpp
    MEMBER CameraCollisionSolverTest ${CMAKE_SOURCE_DIR}/Test/Core/Camera/CameraCollisionSolverTest.cpp
    MEMBER QuadrupedLocomotionModelTest ${CMAKE_SOURCE_DIR}/Test/Core/Locomotion/QuadrupedLocomotionModelTest.cpp
    MEMBER LocomotionProfileReloadTest ${CMAKE_SOURCE_DIR}/Test/Core/Locomotion/LocomotionProfileReloadTest.cpp
    SOURCES
        ${CMAKE_SOURCE_DIR}/Game/Input/CameraInputCollector.cpp
        ${CMAKE_SOURCE_DIR}/Game/Input/PickingController.cpp
        ${CMAKE_SOURCE_DIR}/Game/Input/GameInputSettings.cpp
        ${CMAKE_SOURCE_DIR}/Game/Input/GameHapticsSettings.cpp
)
target_link_libraries(LoggerSinkTest PRIVATE Core Threads::Threads)
target_include_directories(LoggerSinkTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Game
)

include(CTest)
add_test(NAME LoggerSinkTest COMMAND LoggerSinkTest)

add_test(NAME CriticalDampingTest COMMAND LoggerSinkTest --test=CriticalDampingTest)
add_test(NAME QuadrupedLocomotionModelTest COMMAND LoggerSinkTest --test=QuadrupedLocomotionModelTest)
add_test(NAME LocomotionProfileReloadTest COMMAND LoggerSinkTest --test=LocomotionProfileReloadTest)
add_test(NAME CameraCollisionSolverTest COMMAND LoggerSinkTest --test=CameraCollisionSolverTest)
add_test(NAME RenderTransformHistoryTest COMMAND LoggerSinkTest --test=RenderTransformHistoryTest)

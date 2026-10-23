add_test(NAME BridgeBase64Test COMMAND NorvesLibBridgeAdapterScriptComponentTest --test=BridgeBase64Test)

add_test(NAME NorvesLibBridgeAdapterThumbnailTest COMMAND NorvesLibBridgeAdapterScriptComponentTest --test=NorvesLibBridgeAdapterThumbnailTest)


norves_add_test_bundle(NorvesLibBridgeAdapterScriptComponentTest
    FOLDER "Test/Game/Bridge"
    MEMBER NorvesLibBridgeAdapterScriptComponentTest NorvesLibBridgeAdapterScriptComponentTest.cpp
    MEMBER NorvesLibBridgeAdapterThumbnailTest NorvesLibBridgeAdapterThumbnailTest.cpp
    MEMBER BridgeBase64Test BridgeBase64Test.cpp
    SOURCES
        ${CMAKE_SOURCE_DIR}/Game/Bridge/NorvesLibBridgeAdapter.cpp
        ${CMAKE_SOURCE_DIR}/Game/Bridge/BridgeBase64.cpp
        ${CMAKE_SOURCE_DIR}/Game/Bridge/BridgeServerHost.cpp
        ${CMAKE_SOURCE_DIR}/Game/Bridge/BridgeLogSink.cpp
        ${CMAKE_SOURCE_DIR}/Game/Bridge/ReadyHandshake.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameApplicationHandler.cpp
        ${CMAKE_SOURCE_DIR}/Game/Scenes/SceneCycleSmoke.cpp
        ${CMAKE_SOURCE_DIR}/Game/Scripting/M6ScriptSmokeController.cpp
        ${CMAKE_SOURCE_DIR}/Game/Input/CameraInputCollector.cpp
        ${CMAKE_SOURCE_DIR}/Game/Input/CameraScript.cpp
        ${CMAKE_SOURCE_DIR}/Game/Scenes/FreeCameraRig.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameModes/Sandbox/SandboxRoutine.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameModes/MaterialGallery/MaterialGalleryRoutine.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameModes/InteriorLight/InteriorLightRoutine.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameModes/MegaBananas/MegaBananasRoutine.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameModes/MegaStress/MegaStressRoutine.cpp
        ${CMAKE_SOURCE_DIR}/Game/Input/PickingController.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameModes/Launcher/LauncherMenu.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameModes/Launcher/LauncherRoutine.cpp
        ${CMAKE_SOURCE_DIR}/Game/Pause/PauseMenu.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameModes/MemoryAgingTest/MemoryAgingTestRoutine.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameModes/Rendering3DTest/DirectionalLightEditSubRoutine.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameModes/Rendering3DTest/Rendering3DTestDebugDraw.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameModes/Rendering3DTest/Rendering3DTestDebugInput.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameModes/Rendering3DTest/Rendering3DTestRoutine.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameModes/Rendering3DTest/AnimationDebugScene.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameModes/Rendering3DTest/M8MinimalPhysicsSmoke.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameModes/Rendering3DTest/M9WorldAcceptance.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameModes/Rendering3DTest/M9SkeletalPreparation.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameModes/Rendering3DTest/StartupSkinnedProbe.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameModes/DogMovementSmoke/DogMovementSmokeRoutine.cpp
        ${CMAKE_SOURCE_DIR}/Game/Input/GameInputSettings.cpp
        ${CMAKE_SOURCE_DIR}/Game/Input/GameHapticsSettings.cpp
        ${CMAKE_SOURCE_DIR}/Game/Debug/SequenceFrameCapture.cpp
        ${CMAKE_SOURCE_DIR}/Game/Gameplay/Camera/FollowCameraComponent.cpp
        ${CMAKE_SOURCE_DIR}/Game/Gameplay/Camera/FollowCameraProfile.cpp
        ${CMAKE_SOURCE_DIR}/Game/Gameplay/Locomotion/QuadrupedLocomotionComponent.cpp
)
target_link_libraries(NorvesLibBridgeAdapterScriptComponentTest PRIVATE Core NorvesModule_Dummy NorvesModule_Physics norves_bridge_engine_sdk)
target_include_directories(NorvesLibBridgeAdapterScriptComponentTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Game
    ${CMAKE_SOURCE_DIR}/Library
    ${CMAKE_SOURCE_DIR}/Library/Core/Public
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
    ${CMAKE_SOURCE_DIR}/Library/ThirdParty/stb
    ${CMAKE_SOURCE_DIR}/Library/ThirdParty/angelscript/upstream/sdk/angelscript/include
)
target_compile_definitions(NorvesLibBridgeAdapterScriptComponentTest PRIVATE
    NORVES_BRIDGE_ENABLED
    NORVES_ASSET_DIR="${CMAKE_SOURCE_DIR}/Assets"
    NORVES_BRIDGE_TESTING
)
if(MSVC)
    target_compile_options(NorvesLibBridgeAdapterScriptComponentTest PRIVATE /utf-8)
endif()

add_test(
    NAME NorvesLibBridgeAdapterScriptComponentTest
    COMMAND NorvesLibBridgeAdapterScriptComponentTest
)

add_test(
    NAME NorvesLibBridgeAdapterNumericEntityRegressionTest
    COMMAND NorvesLibBridgeAdapterScriptComponentTest --numeric-regression
)

set_tests_properties(
    NorvesLibBridgeAdapterScriptComponentTest
    NorvesLibBridgeAdapterNumericEntityRegressionTest
    PROPERTIES TIMEOUT 300
)

# ランチャーの入力順の検査。LauncherMenu は入力の型だけに依存するので、Core にはリンクしない小さな実行ファイルにする。
add_executable(LauncherMenuTest
    LauncherMenuTest.cpp
    ${CMAKE_SOURCE_DIR}/Game/GameModes/Launcher/LauncherMenu.cpp
)
set_target_properties(LauncherMenuTest PROPERTIES FOLDER "Test/Game/Bridge")
target_include_directories(LauncherMenuTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Game
    ${CMAKE_SOURCE_DIR}/Library
    ${CMAKE_SOURCE_DIR}/Library/Core
    ${CMAKE_SOURCE_DIR}/Library/Core/Public
)
if(MSVC)
    target_compile_options(LauncherMenuTest PRIVATE /utf-8)
endif()
add_test(NAME LauncherMenuTest COMMAND LauncherMenuTest)

# ポーズメニューの入力の契約の検査（開閉・開いている間の入力の遮断・移動後の決定）。
# Start の押しっぱなしは InputSystem→InputRouter→PauseMenu の実配送で確かめるので Core にリンクする。
add_executable(PauseMenuTest
    PauseMenuTest.cpp
    ${CMAKE_SOURCE_DIR}/Game/Pause/PauseMenu.cpp
)
set_target_properties(PauseMenuTest PROPERTIES FOLDER "Test/Game/Bridge")
target_link_libraries(PauseMenuTest PRIVATE Core)
target_include_directories(PauseMenuTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Game
    ${CMAKE_SOURCE_DIR}/Library
    ${CMAKE_SOURCE_DIR}/Library/Core
    ${CMAKE_SOURCE_DIR}/Library/Core/Public
)
if(MSVC)
    target_compile_options(PauseMenuTest PRIVATE /utf-8)
endif()
add_test(NAME PauseMenuTest COMMAND PauseMenuTest)

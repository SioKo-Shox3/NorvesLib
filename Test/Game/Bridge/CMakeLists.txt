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
        ${CMAKE_SOURCE_DIR}/Game/Scripting/M6ScriptSmokeController.cpp
        ${CMAKE_SOURCE_DIR}/Game/Input/CameraInputCollector.cpp
        ${CMAKE_SOURCE_DIR}/Game/Input/PickingController.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameModes/MemoryAgingTest/MemoryAgingTestRoutine.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameModes/Rendering3DTest/DirectionalLightEditSubRoutine.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameModes/Rendering3DTest/Rendering3DTestDebugDraw.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameModes/Rendering3DTest/Rendering3DTestDebugInput.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameModes/Rendering3DTest/Rendering3DTestRoutine.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameModes/Rendering3DTest/M8MinimalPhysicsSmoke.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameModes/Rendering3DTest/M9WorldAcceptance.cpp
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

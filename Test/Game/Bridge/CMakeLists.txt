add_executable(BridgeBase64Test
    BridgeBase64Test.cpp
    ${CMAKE_SOURCE_DIR}/Game/Bridge/BridgeBase64.cpp
)
target_link_libraries(BridgeBase64Test PRIVATE Core)
target_compile_definitions(BridgeBase64Test PRIVATE NORVES_BRIDGE_ENABLED)
target_include_directories(BridgeBase64Test PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Game
    ${CMAKE_SOURCE_DIR}/Library
    ${CMAKE_SOURCE_DIR}/Library/Core/Public
)
set_target_properties(BridgeBase64Test PROPERTIES FOLDER "Test/Game/Bridge")
add_test(NAME BridgeBase64Test COMMAND BridgeBase64Test)

add_executable(NorvesLibBridgeAdapterThumbnailTest
    NorvesLibBridgeAdapterThumbnailTest.cpp
    ${CMAKE_SOURCE_DIR}/Game/Bridge/NorvesLibBridgeAdapter.cpp
    ${CMAKE_SOURCE_DIR}/Game/Bridge/BridgeBase64.cpp
    ${CMAKE_SOURCE_DIR}/Game/Bridge/BridgeServerHost.cpp
    ${CMAKE_SOURCE_DIR}/Game/Bridge/BridgeLogSink.cpp
    ${CMAKE_SOURCE_DIR}/Game/Bridge/ReadyHandshake.cpp
    ${CMAKE_SOURCE_DIR}/Game/GameApplicationHandler.cpp
    ${CMAKE_SOURCE_DIR}/Game/Input/PickingController.cpp
    ${CMAKE_SOURCE_DIR}/Game/GameModes/MemoryAgingTest/MemoryAgingTestRoutine.cpp
    ${CMAKE_SOURCE_DIR}/Game/GameModes/Rendering3DTest/DirectionalLightEditSubRoutine.cpp
    ${CMAKE_SOURCE_DIR}/Game/GameModes/Rendering3DTest/Rendering3DTestDebugDraw.cpp
    ${CMAKE_SOURCE_DIR}/Game/GameModes/Rendering3DTest/Rendering3DTestDebugInput.cpp
    ${CMAKE_SOURCE_DIR}/Game/GameModes/Rendering3DTest/Rendering3DTestRoutine.cpp
)
target_link_libraries(NorvesLibBridgeAdapterThumbnailTest PRIVATE
    Core
    NorvesModule_Dummy
    norves_bridge_engine_sdk
)
target_compile_definitions(NorvesLibBridgeAdapterThumbnailTest PRIVATE
    NORVES_BRIDGE_ENABLED
    NORVES_BRIDGE_TESTING
)
target_include_directories(NorvesLibBridgeAdapterThumbnailTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Game
    ${CMAKE_SOURCE_DIR}/Library
    ${CMAKE_SOURCE_DIR}/Library/Core/Public
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
    ${CMAKE_SOURCE_DIR}/Library/ThirdParty/stb
)
if(MSVC)
    target_compile_options(NorvesLibBridgeAdapterThumbnailTest PRIVATE /utf-8)
endif()
set_target_properties(NorvesLibBridgeAdapterThumbnailTest PROPERTIES FOLDER "Test/Game/Bridge")
add_test(NAME NorvesLibBridgeAdapterThumbnailTest COMMAND NorvesLibBridgeAdapterThumbnailTest)

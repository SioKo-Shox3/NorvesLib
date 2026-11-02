add_test(NAME BridgeBase64Test COMMAND NorvesLibBridgeAdapterScriptComponentTest --test=BridgeBase64Test)

add_test(NAME NorvesLibBridgeAdapterThumbnailTest COMMAND NorvesLibBridgeAdapterScriptComponentTest --test=NorvesLibBridgeAdapterThumbnailTest)


norves_add_test_bundle(NorvesLibBridgeAdapterScriptComponentTest
    FOLDER "Test/Game/Bridge"
    MEMBER NorvesLibBridgeAdapterScriptComponentTest NorvesLibBridgeAdapterScriptComponentTest.cpp
    MEMBER NorvesLibBridgeAdapterThumbnailTest NorvesLibBridgeAdapterThumbnailTest.cpp
    MEMBER BridgeBase64Test BridgeBase64Test.cpp
)
# Game のソース（NorvesGameSources）を組み込むので、全体のビルドで Game の翻訳単位をもう一度コンパイルし、
# Game のソースが増えるたびに壊れていた。対象と実行から外す（2026-11-01）。
set_target_properties(NorvesLibBridgeAdapterScriptComponentTest PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_link_libraries(NorvesLibBridgeAdapterScriptComponentTest PRIVATE NorvesGameSources)
target_include_directories(NorvesLibBridgeAdapterScriptComponentTest PRIVATE
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
    ${CMAKE_SOURCE_DIR}/Library/ThirdParty/angelscript/upstream/sdk/angelscript/include
)
target_compile_definitions(NorvesLibBridgeAdapterScriptComponentTest PRIVATE
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

set_tests_properties(
    NorvesLibBridgeAdapterScriptComponentTest
    NorvesLibBridgeAdapterNumericEntityRegressionTest
    NorvesLibBridgeAdapterThumbnailTest
    BridgeBase64Test
    PROPERTIES DISABLED TRUE
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

# 資産プロパティの編集・保存・拒否を Game 全体に依存せず検査する。
add_executable(BridgeAssetPropertyEditTest
    BridgeAssetPropertyEditTest.cpp
    ${CMAKE_SOURCE_DIR}/Game/Bridge/BridgeAssetPropertyEditor.cpp
)
set_target_properties(BridgeAssetPropertyEditTest PROPERTIES FOLDER "Test/Game/Bridge")
target_link_libraries(BridgeAssetPropertyEditTest PRIVATE Core)
target_include_directories(BridgeAssetPropertyEditTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Game
    ${CMAKE_SOURCE_DIR}/Library
    ${CMAKE_SOURCE_DIR}/Library/Core
    ${CMAKE_SOURCE_DIR}/Library/Core/Public
)
if(MSVC)
    target_compile_options(BridgeAssetPropertyEditTest PRIVATE /utf-8)
endif()
add_test(NAME BridgeAssetPropertyEditTest COMMAND BridgeAssetPropertyEditTest)

# 決定的な撮影の実 GPU 検査。Game からは撮影セッションの翻訳単位だけを組み込む。
add_executable(BridgeDeterministicCaptureVulkanTest
    BridgeDeterministicCaptureVulkanTest.cpp
    ${CMAKE_SOURCE_DIR}/Game/Bridge/BridgeDeterministicCapture.cpp
)
set_target_properties(BridgeDeterministicCaptureVulkanTest PROPERTIES FOLDER "Test/Game/Bridge")
target_link_libraries(BridgeDeterministicCaptureVulkanTest PRIVATE
    RenderingValidationFixtureSupport
    RenderingGpuTestSupport
    Core
)
target_include_directories(BridgeDeterministicCaptureVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Game
    ${CMAKE_SOURCE_DIR}/Library
    ${CMAKE_SOURCE_DIR}/Library/Core
    ${CMAKE_SOURCE_DIR}/Library/Core/Public
)
if(MSVC)
    target_compile_options(BridgeDeterministicCaptureVulkanTest PRIVATE /utf-8)
endif()
add_test(NAME BridgeDeterministicCaptureVulkanTest COMMAND BridgeDeterministicCaptureVulkanTest)
set_tests_properties(BridgeDeterministicCaptureVulkanTest PROPERTIES
    LABELS "GPU;RenderingValidation"
    RESOURCE_LOCK NorvesLibGPU
    SKIP_RETURN_CODE 125
    TIMEOUT 600
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

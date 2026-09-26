norves_add_test_bundle(AngelScriptBootstrapTest
    FOLDER "Test/Core/Scripting"
    MEMBER AngelScriptBootstrapTest AngelScriptBootstrapTest.cpp
    MEMBER ScriptRuntimeSafetyTest ScriptRuntimeSafetyTest.cpp
)
target_link_libraries(AngelScriptBootstrapTest PRIVATE Core)
target_include_directories(AngelScriptBootstrapTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
    ${CMAKE_SOURCE_DIR}/Library/ThirdParty/angelscript/upstream/sdk/angelscript/include
)
target_compile_definitions(AngelScriptBootstrapTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)

add_executable(ScriptApplicationLifecycleContractTest
    ScriptApplicationLifecycleContractTest.cpp
)
target_link_libraries(ScriptApplicationLifecycleContractTest PRIVATE Core)
target_include_directories(ScriptApplicationLifecycleContractTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
target_compile_definitions(ScriptApplicationLifecycleContractTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
set_target_properties(ScriptApplicationLifecycleContractTest PROPERTIES FOLDER "Test/Core/Scripting")


norves_add_test_bundle(ScriptComponentTickTest
    FOLDER "Test/Core/Scripting"
    MEMBER ScriptComponentTickTest ScriptComponentTickTest.cpp
    MEMBER ScriptComponentSceneRoundTripTest ScriptComponentSceneRoundTripTest.cpp
    MEMBER ScriptPathResolutionTest ScriptPathResolutionTest.cpp
    MEMBER M6AngelScriptAcceptanceTest M6AngelScriptAcceptanceTest.cpp
)
target_link_libraries(ScriptComponentTickTest PRIVATE Core)
target_include_directories(ScriptComponentTickTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
target_compile_definitions(ScriptComponentTickTest PRIVATE
    NORVES_ASSET_DIR="${CMAKE_SOURCE_DIR}/Assets"
)

add_executable(ScriptHotReloadTest
    ScriptHotReloadTest.cpp
)
target_link_libraries(ScriptHotReloadTest PRIVATE Core)
target_include_directories(ScriptHotReloadTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
    ${CMAKE_SOURCE_DIR}/Library/ThirdParty/angelscript/upstream/sdk/angelscript/include
)
target_compile_definitions(ScriptHotReloadTest PRIVATE
    NORVES_ASSET_DIR="${CMAKE_SOURCE_DIR}/Assets"
)
set_target_properties(ScriptHotReloadTest PROPERTIES FOLDER "Test/Core/Scripting")

include(CTest)
add_test(NAME AngelScriptBootstrapTest COMMAND AngelScriptBootstrapTest)
add_test(NAME ScriptRuntimeSafetyTest COMMAND AngelScriptBootstrapTest --test=ScriptRuntimeSafetyTest)
add_test(NAME ScriptApplicationLifecycleContractTest COMMAND ScriptApplicationLifecycleContractTest)
set_tests_properties(
    ScriptRuntimeSafetyTest
    ScriptApplicationLifecycleContractTest
    PROPERTIES TIMEOUT 300
)
add_test(NAME ScriptComponentTickTest COMMAND ScriptComponentTickTest)
add_test(NAME ScriptComponentSceneRoundTripTest COMMAND ScriptComponentTickTest --test=ScriptComponentSceneRoundTripTest)
add_test(NAME ScriptPathResolutionTest COMMAND ScriptComponentTickTest --test=ScriptPathResolutionTest)
add_test(NAME ScriptHotReloadTest COMMAND ScriptHotReloadTest)
add_test(NAME ScriptSourceTrackerContractTest COMMAND ScriptHotReloadTest --tracker-contract)
add_test(NAME M6AngelScriptAcceptanceTest COMMAND ScriptComponentTickTest --test=M6AngelScriptAcceptanceTest)
set_tests_properties(
    ScriptHotReloadTest
    ScriptSourceTrackerContractTest
    PROPERTIES TIMEOUT 300
)
set_tests_properties(M6AngelScriptAcceptanceTest PROPERTIES TIMEOUT 300)

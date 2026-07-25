add_executable(AngelScriptVendorContractTest
    AngelScriptVendorContractTest.cpp
)
target_link_libraries(AngelScriptVendorContractTest PRIVATE
    Core
    bcrypt
)
target_include_directories(AngelScriptVendorContractTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
target_compile_definitions(AngelScriptVendorContractTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
set_target_properties(AngelScriptVendorContractTest PROPERTIES FOLDER "Test/Core/Scripting")

add_executable(AngelScriptBootstrapTest
    AngelScriptBootstrapTest.cpp
)
target_link_libraries(AngelScriptBootstrapTest PRIVATE Core)
target_include_directories(AngelScriptBootstrapTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
    ${CMAKE_SOURCE_DIR}/Library/ThirdParty/angelscript/upstream/sdk/angelscript/include
)
set_target_properties(AngelScriptBootstrapTest PROPERTIES FOLDER "Test/Core/Scripting")

add_executable(ScriptComponentTickTest
    ScriptComponentTickTest.cpp
)
target_link_libraries(ScriptComponentTickTest PRIVATE Core)
target_include_directories(ScriptComponentTickTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(ScriptComponentTickTest PROPERTIES FOLDER "Test/Core/Scripting")

add_executable(ScriptComponentSceneRoundTripTest
    ScriptComponentSceneRoundTripTest.cpp
)
target_link_libraries(ScriptComponentSceneRoundTripTest PRIVATE Core)
target_include_directories(ScriptComponentSceneRoundTripTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(ScriptComponentSceneRoundTripTest PROPERTIES FOLDER "Test/Core/Scripting")

add_executable(ScriptPathResolutionTest
    ScriptPathResolutionTest.cpp
)
target_link_libraries(ScriptPathResolutionTest PRIVATE Core)
target_include_directories(ScriptPathResolutionTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
target_compile_definitions(ScriptPathResolutionTest PRIVATE
    NORVES_ASSET_DIR="${CMAKE_SOURCE_DIR}/Assets"
)
set_target_properties(ScriptPathResolutionTest PROPERTIES FOLDER "Test/Core/Scripting")

include(CTest)
add_test(NAME AngelScriptVendorContractTest COMMAND AngelScriptVendorContractTest)
add_test(NAME AngelScriptBootstrapTest COMMAND AngelScriptBootstrapTest)
add_test(NAME ScriptComponentTickTest COMMAND ScriptComponentTickTest)
add_test(NAME ScriptComponentSceneRoundTripTest COMMAND ScriptComponentSceneRoundTripTest)
add_test(NAME ScriptPathResolutionTest COMMAND ScriptPathResolutionTest)

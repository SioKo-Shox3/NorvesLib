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

include(CTest)
add_test(NAME AngelScriptVendorContractTest COMMAND AngelScriptVendorContractTest)
add_test(NAME AngelScriptBootstrapTest COMMAND AngelScriptBootstrapTest)

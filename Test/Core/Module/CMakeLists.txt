add_executable(ModuleRegistryTest
    ModuleRegistryTest.cpp
)
target_link_libraries(ModuleRegistryTest PRIVATE Core)
target_include_directories(ModuleRegistryTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(ModuleRegistryTest PROPERTIES FOLDER "Test/Core/Module")

include(CTest)
add_test(NAME ModuleRegistryTest COMMAND ModuleRegistryTest)

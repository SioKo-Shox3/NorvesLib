add_executable(PhysicsModuleLinkTest
    PhysicsModuleLinkTest.cpp
)
target_link_libraries(PhysicsModuleLinkTest PRIVATE Core NorvesModule_Physics)
target_include_directories(PhysicsModuleLinkTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(PhysicsModuleLinkTest PROPERTIES FOLDER "Test/Modules/Physics")

include(CTest)
add_test(NAME PhysicsModuleLinkTest COMMAND PhysicsModuleLinkTest)

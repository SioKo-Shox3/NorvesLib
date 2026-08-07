function(norves_target_has_private_direct_link target dependency outHasPrivateDirectLink)
    get_target_property(targetDirectLinks ${target} LINK_LIBRARIES)
    if(NOT targetDirectLinks OR targetDirectLinks STREQUAL "targetDirectLinks-NOTFOUND")
        set(${outHasPrivateDirectLink} FALSE PARENT_SCOPE)
        return()
    endif()

    list(FIND targetDirectLinks ${dependency} directLinkIndex)
    if(directLinkIndex EQUAL -1)
        set(${outHasPrivateDirectLink} FALSE PARENT_SCOPE)
        return()
    endif()

    get_target_property(targetInterfaceLinks ${target} INTERFACE_LINK_LIBRARIES)
    if(targetInterfaceLinks AND NOT targetInterfaceLinks STREQUAL "targetInterfaceLinks-NOTFOUND")
        list(FIND targetInterfaceLinks ${dependency} interfaceLinkIndex)
        if(NOT interfaceLinkIndex EQUAL -1)
            set(${outHasPrivateDirectLink} FALSE PARENT_SCOPE)
            return()
        endif()
    endif()

    set(${outHasPrivateDirectLink} TRUE PARENT_SCOPE)
endfunction()

if(NOT TARGET Game)
    message(FATAL_ERROR "PhysicsModuleLinkTest requires the Game target")
endif()

norves_target_has_private_direct_link(Game NorvesModule_Physics gameHasPrivatePhysicsLink)
if(NOT gameHasPrivatePhysicsLink)
    message(FATAL_ERROR "Game must privately link NorvesModule_Physics")
endif()

# Mutation: INTERFACE visibility must not satisfy the PRIVATE direct-link contract.
add_library(PhysicsModuleLinkTest_PublicPhysicsMutation INTERFACE)
target_link_libraries(PhysicsModuleLinkTest_PublicPhysicsMutation INTERFACE NorvesModule_Physics)
norves_target_has_private_direct_link(
    PhysicsModuleLinkTest_PublicPhysicsMutation
    NorvesModule_Physics
    publicMutationHasPrivatePhysicsLink)
if(publicMutationHasPrivatePhysicsLink)
    message(FATAL_ERROR "PUBLIC Physics link incorrectly satisfied PRIVATE direct-link contract")
endif()

add_executable(PhysicsModuleLinkTest
    PhysicsModuleLinkTest.cpp
)
target_link_libraries(PhysicsModuleLinkTest PRIVATE Core NorvesModule_Physics)
target_include_directories(PhysicsModuleLinkTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Modules/Physics/Private
)
target_compile_definitions(PhysicsModuleLinkTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
set_target_properties(PhysicsModuleLinkTest PROPERTIES FOLDER "Test/Modules/Physics")

include(CTest)
add_test(NAME PhysicsModuleLinkTest COMMAND PhysicsModuleLinkTest)

add_executable(PhysicsBroadphaseQueryTest
    PhysicsBroadphaseQueryTest.cpp
)
target_link_libraries(PhysicsBroadphaseQueryTest PRIVATE Core NorvesModule_Physics)
target_include_directories(PhysicsBroadphaseQueryTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Modules/Physics/Private
)
set_target_properties(PhysicsBroadphaseQueryTest PROPERTIES FOLDER "Test/Modules/Physics")
add_test(NAME PhysicsBroadphaseQueryTest COMMAND PhysicsBroadphaseQueryTest)

add_executable(PhysicsDynamicsTest
    PhysicsDynamicsTest.cpp
)
target_link_libraries(PhysicsDynamicsTest PRIVATE Core NorvesModule_Physics)
target_include_directories(PhysicsDynamicsTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Modules/Physics/Private
)
set_target_properties(PhysicsDynamicsTest PROPERTIES FOLDER "Test/Modules/Physics")
add_test(NAME PhysicsDynamicsTest COMMAND PhysicsDynamicsTest)

add_executable(PhysicsLifecycleEventTest
    PhysicsLifecycleEventTest.cpp
)
target_link_libraries(PhysicsLifecycleEventTest PRIVATE Core NorvesModule_Physics)
target_include_directories(PhysicsLifecycleEventTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Modules/Physics/Private
)
set_target_properties(PhysicsLifecycleEventTest PROPERTIES FOLDER "Test/Modules/Physics")
add_test(NAME PhysicsLifecycleEventTest COMMAND PhysicsLifecycleEventTest)

add_executable(PhysicsFixedStepPipelineTest
    PhysicsFixedStepPipelineTest.cpp
)
target_link_libraries(PhysicsFixedStepPipelineTest PRIVATE Core NorvesModule_Physics)
target_include_directories(PhysicsFixedStepPipelineTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
    ${CMAKE_SOURCE_DIR}/Library/Modules/Physics/Private
)
set_target_properties(PhysicsFixedStepPipelineTest PROPERTIES FOLDER "Test/Modules/Physics")
add_test(NAME PhysicsFixedStepPipelineTest COMMAND PhysicsFixedStepPipelineTest)
set_tests_properties(PhysicsFixedStepPipelineTest PROPERTIES TIMEOUT 90)

add_executable(PhysicsArchitectureContractTest
    PhysicsArchitectureContractTest.cpp
)
target_link_libraries(PhysicsArchitectureContractTest PRIVATE Core NorvesModule_Physics)
target_include_directories(PhysicsArchitectureContractTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
    ${CMAKE_SOURCE_DIR}/Library/Modules/Physics/Private
)
target_compile_definitions(PhysicsArchitectureContractTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
set_target_properties(PhysicsArchitectureContractTest PROPERTIES FOLDER "Test/Modules/Physics")
add_test(NAME PhysicsArchitectureContractTest COMMAND PhysicsArchitectureContractTest)

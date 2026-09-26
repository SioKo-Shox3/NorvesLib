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

include(CTest)
add_test(NAME PhysicsModuleLinkTest COMMAND PhysicsDynamicsTest --test=PhysicsModuleLinkTest)

add_test(NAME PhysicsBroadphaseQueryTest COMMAND PhysicsDynamicsTest --test=PhysicsBroadphaseQueryTest)


norves_add_test_bundle(PhysicsDynamicsTest
    FOLDER "Test/Modules/Physics"
    MEMBER PhysicsDynamicsTest PhysicsDynamicsTest.cpp
    MEMBER PhysicsModuleLinkTest PhysicsModuleLinkTest.cpp
    MEMBER PhysicsBroadphaseQueryTest PhysicsBroadphaseQueryTest.cpp
    MEMBER PhysicsLifecycleEventTest PhysicsLifecycleEventTest.cpp
    MEMBER PhysicsFixedStepPipelineTest PhysicsFixedStepPipelineTest.cpp
)
target_link_libraries(PhysicsDynamicsTest PRIVATE Core NorvesModule_Physics)
target_include_directories(PhysicsDynamicsTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Modules/Physics/Private
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
target_compile_definitions(PhysicsDynamicsTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
add_test(NAME PhysicsDynamicsTest COMMAND PhysicsDynamicsTest)

add_test(NAME PhysicsLifecycleEventTest COMMAND PhysicsDynamicsTest --test=PhysicsLifecycleEventTest)

add_test(NAME PhysicsFixedStepPipelineTest COMMAND PhysicsDynamicsTest --test=PhysicsFixedStepPipelineTest)
set_tests_properties(PhysicsFixedStepPipelineTest PROPERTIES TIMEOUT 90)


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
add_test(NAME PhysicsProxyQueryTest COMMAND PhysicsDynamicsTest --test=PhysicsProxyQueryTest)


norves_add_test_bundle(PhysicsDynamicsTest
    FOLDER "Test/Modules/Physics"
    MEMBER PhysicsDynamicsTest PhysicsDynamicsTest.cpp
    MEMBER CharacterDriveOptionsTest CharacterDriveOptionsTest.cpp
    MEMBER DogMovementSmokeGeometryTest DogMovementSmokeGeometryTest.cpp
    MEMBER PhysicsCharacterMoveTest PhysicsCharacterMoveTest.cpp
    MEMBER ColliderLocalPoseTest ColliderLocalPoseTest.cpp
    MEMBER CharacterPlatformMotionTest CharacterPlatformMotionTest.cpp
    MEMBER PhysicsModuleLinkTest PhysicsModuleLinkTest.cpp
    MEMBER PhysicsBroadphaseQueryTest PhysicsBroadphaseQueryTest.cpp
    MEMBER PhysicsProxyQueryTest PhysicsProxyQueryTest.cpp
    MEMBER PhysicsProxyVisitorTest PhysicsProxyVisitorTest.cpp
    MEMBER PhysicsRayPrecisionTest PhysicsRayPrecisionTest.cpp
    MEMBER PhysicsQueryPruningTest PhysicsQueryPruningTest.cpp
    MEMBER PhysicsLegacyQueryTest PhysicsLegacyQueryTest.cpp
    MEMBER PhysicsLifecycleEventTest PhysicsLifecycleEventTest.cpp
    MEMBER PhysicsFixedStepPipelineTest PhysicsFixedStepPipelineTest.cpp
    SOURCES
        ${CMAKE_SOURCE_DIR}/Game/Gameplay/Locomotion/QuadrupedLocomotionComponent.cpp
        ${CMAKE_SOURCE_DIR}/Game/Gameplay/Camera/FollowCameraComponent.cpp
)
target_link_libraries(PhysicsDynamicsTest PRIVATE Core NorvesModule_Physics)
target_include_directories(PhysicsDynamicsTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Game
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


add_test(NAME PhysicsProxyVisitorTest COMMAND PhysicsDynamicsTest --test=PhysicsProxyVisitorTest)

add_test(NAME PhysicsRayPrecisionTest COMMAND PhysicsDynamicsTest --test=PhysicsRayPrecisionTest)

add_test(NAME PhysicsQueryPruningTest COMMAND PhysicsDynamicsTest --test=PhysicsQueryPruningTest)

add_test(NAME PhysicsLegacyQueryTest COMMAND PhysicsDynamicsTest --test=PhysicsLegacyQueryTest)

add_test(NAME PhysicsCharacterMoveTest COMMAND PhysicsDynamicsTest --test=PhysicsCharacterMoveTest)
add_test(NAME ColliderLocalPoseTest COMMAND PhysicsDynamicsTest --test=ColliderLocalPoseTest)
add_test(NAME CharacterPlatformMotionTest COMMAND PhysicsDynamicsTest --test=CharacterPlatformMotionTest)

add_test(NAME CharacterDriveOptionsTest COMMAND PhysicsDynamicsTest --test=CharacterDriveOptionsTest)
add_test(NAME DogMovementSmokeGeometryTest COMMAND PhysicsDynamicsTest --test=DogMovementSmokeGeometryTest)

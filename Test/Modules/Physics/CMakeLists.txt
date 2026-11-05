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
    MEMBER PhysicsHeightFieldColliderTest PhysicsHeightFieldColliderTest.cpp
    MEMBER PhysicsLifecycleEventTest PhysicsLifecycleEventTest.cpp
    MEMBER PhysicsFixedStepPipelineTest PhysicsFixedStepPipelineTest.cpp
    MEMBER PhysicsGroundQueryIntegrationTest PhysicsGroundQueryIntegrationTest.cpp
    MEMBER FollowCameraBehaviorTest FollowCameraBehaviorTest.cpp
    MEMBER CombatSweepTest ${CMAKE_SOURCE_DIR}/Test/Modules/Gameplay/CombatSweepTest.cpp
    MEMBER CombatLayerTest ${CMAKE_SOURCE_DIR}/Test/Modules/Gameplay/CombatLayerTest.cpp
    MEMBER CombatFrameRateInvarianceTest ${CMAKE_SOURCE_DIR}/Test/Modules/Gameplay/CombatFrameRateInvarianceTest.cpp
    MEMBER CombatMultiHitTest ${CMAKE_SOURCE_DIR}/Test/Modules/Gameplay/CombatMultiHitTest.cpp
    MEMBER CombatLifecycleTest ${CMAKE_SOURCE_DIR}/Test/Modules/Gameplay/CombatLifecycleTest.cpp
    MEMBER CombatJointFollowTest ${CMAKE_SOURCE_DIR}/Test/Modules/Gameplay/CombatJointFollowTest.cpp
    MEMBER CombatProfileLoadTest ${CMAKE_SOURCE_DIR}/Test/Modules/Gameplay/CombatProfileLoadTest.cpp
    MEMBER CombatResponseTest ${CMAKE_SOURCE_DIR}/Test/Modules/Gameplay/CombatResponseTest.cpp
    MEMBER HealthComponentTest ${CMAKE_SOURCE_DIR}/Test/Modules/Gameplay/HealthComponentTest.cpp
    MEMBER EnemyDefinitionLoadTest ${CMAKE_SOURCE_DIR}/Test/Modules/Gameplay/EnemyDefinitionLoadTest.cpp
    MEMBER EnemyPatternSelectionTest ${CMAKE_SOURCE_DIR}/Test/Modules/Gameplay/EnemyPatternSelectionTest.cpp
    MEMBER EnemyStateMachineTest ${CMAKE_SOURCE_DIR}/Test/Modules/Gameplay/EnemyStateMachineTest.cpp
    MEMBER ProgressionStateTest ${CMAKE_SOURCE_DIR}/Test/Modules/Gameplay/ProgressionStateTest.cpp
    MEMBER DefeatGateTest ${CMAKE_SOURCE_DIR}/Test/Modules/Gameplay/DefeatGateTest.cpp
    MEMBER EncounterZoneTest ${CMAKE_SOURCE_DIR}/Test/Modules/Gameplay/EncounterZoneTest.cpp
    MEMBER EncounterLifecycleTest ${CMAKE_SOURCE_DIR}/Test/Modules/Gameplay/EncounterLifecycleTest.cpp
    MEMBER EncounterSerializationTest ${CMAKE_SOURCE_DIR}/Test/Modules/Gameplay/EncounterSerializationTest.cpp
    MEMBER InteractionPromptTest ${CMAKE_SOURCE_DIR}/Test/Modules/Gameplay/InteractionPromptTest.cpp
    SOURCES
        ${CMAKE_SOURCE_DIR}/Game/Gameplay/Locomotion/QuadrupedLocomotionComponent.cpp
        ${CMAKE_SOURCE_DIR}/Game/Gameplay/Camera/FollowCameraComponent.cpp
        ${CMAKE_SOURCE_DIR}/Game/Gameplay/Camera/FollowCameraProfile.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameModes/DogMovementSmoke/DogMovementAutopilotScript.cpp
)
target_link_libraries(PhysicsDynamicsTest PRIVATE Core NorvesModule_Physics NorvesModule_Gameplay)
target_include_directories(PhysicsDynamicsTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Game
    ${CMAKE_SOURCE_DIR}/Library/Modules/Physics/Private
    ${CMAKE_SOURCE_DIR}/Library/Modules/Gameplay/Private
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
target_compile_definitions(PhysicsDynamicsTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
add_test(NAME PhysicsDynamicsTest COMMAND PhysicsDynamicsTest)

add_test(NAME PhysicsLifecycleEventTest COMMAND PhysicsDynamicsTest --test=PhysicsLifecycleEventTest)

add_test(NAME PhysicsFixedStepPipelineTest COMMAND PhysicsDynamicsTest --test=PhysicsFixedStepPipelineTest)
add_test(NAME PhysicsGroundQueryIntegrationTest COMMAND PhysicsDynamicsTest --test=PhysicsGroundQueryIntegrationTest)
set_tests_properties(PhysicsFixedStepPipelineTest PROPERTIES TIMEOUT 90)


add_test(NAME PhysicsProxyVisitorTest COMMAND PhysicsDynamicsTest --test=PhysicsProxyVisitorTest)

add_test(NAME PhysicsRayPrecisionTest COMMAND PhysicsDynamicsTest --test=PhysicsRayPrecisionTest)

add_test(NAME PhysicsQueryPruningTest COMMAND PhysicsDynamicsTest --test=PhysicsQueryPruningTest)

add_test(NAME PhysicsLegacyQueryTest COMMAND PhysicsDynamicsTest --test=PhysicsLegacyQueryTest)
add_test(NAME PhysicsHeightFieldColliderTest COMMAND PhysicsDynamicsTest --test=PhysicsHeightFieldColliderTest)

add_test(NAME PhysicsCharacterMoveTest COMMAND PhysicsDynamicsTest --test=PhysicsCharacterMoveTest)
add_test(NAME ColliderLocalPoseTest COMMAND PhysicsDynamicsTest --test=ColliderLocalPoseTest)
add_test(NAME CharacterPlatformMotionTest COMMAND PhysicsDynamicsTest --test=CharacterPlatformMotionTest)

add_test(NAME CharacterDriveOptionsTest COMMAND PhysicsDynamicsTest --test=CharacterDriveOptionsTest)
add_test(NAME DogMovementSmokeGeometryTest COMMAND PhysicsDynamicsTest --test=DogMovementSmokeGeometryTest)

add_test(NAME FollowCameraBehaviorTest COMMAND PhysicsDynamicsTest --test=FollowCameraBehaviorTest)
add_test(NAME CombatSweepTest COMMAND PhysicsDynamicsTest --test=CombatSweepTest)
add_test(NAME CombatLayerTest COMMAND PhysicsDynamicsTest --test=CombatLayerTest)
add_test(NAME CombatFrameRateInvarianceTest COMMAND PhysicsDynamicsTest --test=CombatFrameRateInvarianceTest)
add_test(NAME CombatMultiHitTest COMMAND PhysicsDynamicsTest --test=CombatMultiHitTest)
add_test(NAME CombatLifecycleTest COMMAND PhysicsDynamicsTest --test=CombatLifecycleTest)
add_test(NAME CombatJointFollowTest COMMAND PhysicsDynamicsTest --test=CombatJointFollowTest)
add_test(NAME CombatProfileLoadTest COMMAND PhysicsDynamicsTest --test=CombatProfileLoadTest)
add_test(NAME CombatResponseTest COMMAND PhysicsDynamicsTest --test=CombatResponseTest)
add_test(NAME HealthComponentTest COMMAND PhysicsDynamicsTest --test=HealthComponentTest)
add_test(NAME EnemyDefinitionLoadTest COMMAND PhysicsDynamicsTest --test=EnemyDefinitionLoadTest)
add_test(NAME EnemyPatternSelectionTest COMMAND PhysicsDynamicsTest --test=EnemyPatternSelectionTest)
add_test(NAME EnemyStateMachineTest COMMAND PhysicsDynamicsTest --test=EnemyStateMachineTest)
add_test(NAME ProgressionStateTest COMMAND PhysicsDynamicsTest --test=ProgressionStateTest)
add_test(NAME DefeatGateTest COMMAND PhysicsDynamicsTest --test=DefeatGateTest)
add_test(NAME EncounterZoneTest COMMAND PhysicsDynamicsTest --test=EncounterZoneTest)
add_test(NAME EncounterLifecycleTest COMMAND PhysicsDynamicsTest --test=EncounterLifecycleTest)
add_test(NAME EncounterSerializationTest COMMAND PhysicsDynamicsTest --test=EncounterSerializationTest)
add_test(NAME InteractionPromptTest COMMAND PhysicsDynamicsTest --test=InteractionPromptTest)

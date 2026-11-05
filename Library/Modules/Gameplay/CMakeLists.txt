set(MODULE_NAME NorvesModule_Gameplay)

set(GAMEPLAY_PUBLIC_HEADERS
    Public/Gameplay/Level/LevelMarkerComponent.h
    Public/Gameplay/Level/TerritoryMarkerComponent.h
    Public/Gameplay/Level/RestPointComponent.h
    Public/Gameplay/Level/PoiMarkerComponent.h
    Public/Gameplay/Level/AnimalSpawnPointComponent.h
    Public/Gameplay/Level/ScentTrailComponent.h
    Public/Gameplay/Level/GameplayComponentClasses.h
    Public/Gameplay/Level/LevelMarkerValidation.h
    Public/Gameplay/Grip/GripTuning.h
    Public/Gameplay/IGameplayModule.h
    Public/Gameplay/Combat/DamageTypes.h
    Public/Gameplay/Combat/HealthComponent.h
    Public/Gameplay/Combat/CombatShape.h
    Public/Gameplay/Combat/AttackProfile.h
    Public/Gameplay/Combat/CombatHitEvent.h
    Public/Gameplay/Combat/HitboxComponent.h
    Public/Gameplay/Combat/HurtboxSetComponent.h
    Public/Gameplay/Combat/CombatJoint.h
    Public/Gameplay/Combat/CombatData.h
    Public/Gameplay/Combat/CombatResponseSettings.h
    Public/Gameplay/Enemy/EnemyDefinition.h
    Public/Gameplay/Enemy/EnemyPatternSelector.h
    Public/Gameplay/Enemy/IEnemyAnimator.h
    Public/Gameplay/Enemy/AnimatorEnemyAnimator.h
    Public/Gameplay/Enemy/EnemyComponent.h
    Public/Gameplay/Progression/GameplayStableId.h
    Public/Gameplay/Progression/ProgressionState.h
    Public/Gameplay/Progression/DefeatGate.h
    Public/Gameplay/World/EncounterZoneTypes.h
    Public/Gameplay/World/ZoneProfileStack.h
    Public/Gameplay/World/ZoneSensorComponent.h
    Public/Gameplay/World/EncounterZoneComponent.h
    Public/Gameplay/World/EncounterService.h
    Public/Gameplay/World/InteractionTypes.h
    Public/Gameplay/World/InteractableComponent.h
    Public/Gameplay/World/InteractionService.h
)

set(GAMEPLAY_PRIVATE_SOURCES
    Private/Gameplay/Level/LevelMarkerComponents.cpp
    Private/Gameplay/Level/ScentTrailComponent.cpp
    Private/Gameplay/Level/GameplayComponentClasses.cpp
    Private/Gameplay/Level/LevelMarkerValidation.cpp
    Private/Gameplay/Grip/GripTuningLoader.cpp
    Private/Gameplay/Grip/GripSolver.h
    Private/Gameplay/Grip/GripSolver.cpp
    Private/Gameplay/Combat/HealthComponent.cpp
    Private/Gameplay/Combat/HitboxComponent.cpp
    Private/Gameplay/Combat/HurtboxSetComponent.cpp
    Private/Gameplay/Combat/CombatJointBinding.h
    Private/Gameplay/Combat/CombatJointBinding.cpp
    Private/Gameplay/Combat/CombatDataLoader.cpp
    Private/Gameplay/Combat/CombatResponse.h
    Private/Gameplay/Combat/CombatResponse.cpp
    Private/Gameplay/Combat/CombatDebugDraw.h
    Private/Gameplay/Combat/CombatDebugDraw.cpp
    Private/Gameplay/Enemy/EnemyDefinition.cpp
    Private/Gameplay/Enemy/EnemyPatternSelector.cpp
    Private/Gameplay/Enemy/AnimatorEnemyAnimator.cpp
    Private/Gameplay/Enemy/EnemyComponent.cpp
    Private/Gameplay/Combat/CombatRuntime.h
    Private/Gameplay/Combat/CombatRuntime.cpp
    Private/Gameplay/Progression/GameplayStableId.cpp
    Private/Gameplay/Progression/ProgressionState.cpp
    Private/Gameplay/Progression/DefeatGate.cpp
    Private/Gameplay/Combat/CombatGeometry.h
    Private/Gameplay/Combat/CombatGeometry.cpp
    Private/Gameplay/World/EncounterZoneMath.h
    Private/Gameplay/World/EncounterZoneMath.cpp
    Private/Gameplay/World/ZoneProfileStack.cpp
    Private/Gameplay/World/ZoneSensorComponent.cpp
    Private/Gameplay/World/EncounterZoneComponent.cpp
    Private/Gameplay/World/EncounterService.cpp
    Private/Gameplay/World/InteractableComponent.cpp
    Private/Gameplay/World/InteractionService.cpp
    Private/Gameplay/GameplayModule.h
    Private/Gameplay/GameplayModule.cpp
)

add_library(${MODULE_NAME} STATIC ${GAMEPLAY_PUBLIC_HEADERS} ${GAMEPLAY_PRIVATE_SOURCES})

target_link_libraries(${MODULE_NAME} PRIVATE Core)

target_include_directories(${MODULE_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Public
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Private
        ${CMAKE_SOURCE_DIR}
)

target_compile_features(${MODULE_NAME} PRIVATE cxx_std_23)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${GAMEPLAY_PUBLIC_HEADERS} ${GAMEPLAY_PRIVATE_SOURCES})

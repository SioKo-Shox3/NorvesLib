set(MODULE_NAME NorvesModule_Physics)

set(PHYSICS_PUBLIC_HEADERS
    Public/Physics/ColliderComponent.h
    Public/Physics/IPhysicsModule.h
    Public/Physics/PhysicsTypes.h
    Public/Physics/RigidBodyComponent.h
)

set(PHYSICS_PRIVATE_SOURCES
    Private/Physics/ColliderComponent.cpp
    Private/Physics/PhysicsBroadphase.cpp
    Private/Physics/PhysicsModule.cpp
    Private/Physics/RigidBodyComponent.cpp
)

add_library(${MODULE_NAME} STATIC ${PHYSICS_PUBLIC_HEADERS} ${PHYSICS_PRIVATE_SOURCES})

target_link_libraries(${MODULE_NAME} PRIVATE Core)

target_include_directories(${MODULE_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Public
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Private
        ${CMAKE_SOURCE_DIR}
)

target_compile_features(${MODULE_NAME} PRIVATE cxx_std_23)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${PHYSICS_PUBLIC_HEADERS} ${PHYSICS_PRIVATE_SOURCES})

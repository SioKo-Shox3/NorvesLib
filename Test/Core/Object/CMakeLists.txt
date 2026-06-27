add_executable(ObjectReflectionTest
    ObjectReflectionTest.cpp
    Animal.cpp
)
target_link_libraries(ObjectReflectionTest PRIVATE Core)
target_include_directories(ObjectReflectionTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)
set_target_properties(ObjectReflectionTest PROPERTIES FOLDER "Test/Core/Object")

add_executable(ResourceRegistryTest
    ResourceRegistryTest.cpp
)
target_link_libraries(ResourceRegistryTest PRIVATE Core)
target_include_directories(ResourceRegistryTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(ResourceRegistryTest PROPERTIES FOLDER "Test/Core/Object")

add_executable(RuntimeSchemaSnapshotTest
    RuntimeSchemaSnapshotTest.cpp
    Animal.cpp
)
target_link_libraries(RuntimeSchemaSnapshotTest PRIVATE Core)
target_include_directories(RuntimeSchemaSnapshotTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)
set_target_properties(RuntimeSchemaSnapshotTest PROPERTIES FOLDER "Test/Core/Object")

add_executable(ObjectHeapTest
    ObjectHeapTest.cpp
    Animal.cpp
)
target_link_libraries(ObjectHeapTest PRIVATE Core)
target_include_directories(ObjectHeapTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)
set_target_properties(ObjectHeapTest PROPERTIES FOLDER "Test/Core/Object")

add_executable(PropertyBagTest
    PropertyBagTest.cpp
    Animal.cpp
)
target_link_libraries(PropertyBagTest PRIVATE Core)
target_include_directories(PropertyBagTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)
set_target_properties(PropertyBagTest PROPERTIES FOLDER "Test/Core/Object")

add_executable(InvocationQueueTest
    InvocationQueueTest.cpp
    Animal.cpp
)
target_link_libraries(InvocationQueueTest PRIVATE Core)
target_include_directories(InvocationQueueTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)
set_target_properties(InvocationQueueTest PROPERTIES FOLDER "Test/Core/Object")

add_executable(ResourceMetadataTest
    ResourceMetadataTest.cpp
)
target_link_libraries(ResourceMetadataTest PRIVATE Core)
target_include_directories(ResourceMetadataTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(ResourceMetadataTest PROPERTIES FOLDER "Test/Core/Object")

add_executable(ObjectCastTest
    ObjectCastTest.cpp
    Animal.cpp
)
target_link_libraries(ObjectCastTest PRIVATE Core)
target_include_directories(ObjectCastTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)
set_target_properties(ObjectCastTest PROPERTIES FOLDER "Test/Core/Object")

add_executable(LightComponentBuildProxyTest
    LightComponentBuildProxyTest.cpp
)
target_link_libraries(LightComponentBuildProxyTest PRIVATE Core)
target_include_directories(LightComponentBuildProxyTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(LightComponentBuildProxyTest PROPERTIES FOLDER "Test/Core/Object")

add_executable(ObjectGCTest
    ObjectGCTest.cpp
)
target_link_libraries(ObjectGCTest PRIVATE Core)
target_include_directories(ObjectGCTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)
set_target_properties(ObjectGCTest PROPERTIES FOLDER "Test/Core/Object")

add_executable(IncrementalGCTest
    IncrementalGCTest.cpp
)
target_link_libraries(IncrementalGCTest PRIVATE Core)
target_include_directories(IncrementalGCTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)
set_target_properties(IncrementalGCTest PROPERTIES FOLDER "Test/Core/Object")

add_executable(SchemaProjectionTest
    SchemaProjectionTest.cpp
    Animal.cpp
)
target_link_libraries(SchemaProjectionTest PRIVATE Core)
target_include_directories(SchemaProjectionTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)
set_target_properties(SchemaProjectionTest PROPERTIES FOLDER "Test/Core/Object")

add_executable(EntitySubtreeSnapshotTest
    EntitySubtreeSnapshotTest.cpp
)
target_link_libraries(EntitySubtreeSnapshotTest PRIVATE Core)
target_include_directories(EntitySubtreeSnapshotTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(EntitySubtreeSnapshotTest PROPERTIES FOLDER "Test/Core/Object")

add_executable(PrefabRoundTripTest
    PrefabRoundTripTest.cpp
)
target_link_libraries(PrefabRoundTripTest PRIVATE Core)
target_include_directories(PrefabRoundTripTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(PrefabRoundTripTest PROPERTIES FOLDER "Test/Core/Object")

add_executable(LegacyCleanupTest
    LegacyCleanupTest.cpp
    Animal.cpp
)
target_link_libraries(LegacyCleanupTest PRIVATE Core)
target_include_directories(LegacyCleanupTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)
set_target_properties(LegacyCleanupTest PROPERTIES FOLDER "Test/Core/Object")

add_executable(TransformHierarchyTest
    TransformHierarchyTest.cpp
)
target_link_libraries(TransformHierarchyTest PRIVATE Core)
target_include_directories(TransformHierarchyTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(TransformHierarchyTest PROPERTIES FOLDER "Test/Core/Object")

add_executable(EntityTreeTest
    EntityTreeTest.cpp
)
target_link_libraries(EntityTreeTest PRIVATE Core)
target_include_directories(EntityTreeTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(EntityTreeTest PROPERTIES FOLDER "Test/Core/Object")

include(CTest)
add_test(NAME ObjectReflectionTest COMMAND ObjectReflectionTest)
add_test(NAME ResourceRegistryTest COMMAND ResourceRegistryTest)
add_test(NAME RuntimeSchemaSnapshotTest COMMAND RuntimeSchemaSnapshotTest)
add_test(NAME ObjectHeapTest COMMAND ObjectHeapTest)
add_test(NAME ObjectCastTest COMMAND ObjectCastTest)
add_test(NAME LightComponentBuildProxyTest COMMAND LightComponentBuildProxyTest)
add_test(NAME PropertyBagTest COMMAND PropertyBagTest)
add_test(NAME InvocationQueueTest COMMAND InvocationQueueTest)
add_test(NAME ResourceMetadataTest COMMAND ResourceMetadataTest)
add_test(NAME ObjectGCTest COMMAND ObjectGCTest)
add_test(NAME IncrementalGCTest COMMAND IncrementalGCTest)
add_test(NAME SchemaProjectionTest COMMAND SchemaProjectionTest)
add_test(NAME EntitySubtreeSnapshotTest COMMAND EntitySubtreeSnapshotTest)
add_test(NAME PrefabRoundTripTest COMMAND PrefabRoundTripTest)
add_test(NAME LegacyCleanupTest COMMAND LegacyCleanupTest)
add_test(NAME TransformHierarchyTest COMMAND TransformHierarchyTest)
add_test(NAME EntityTreeTest COMMAND EntityTreeTest)

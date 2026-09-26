norves_add_test_bundle(ObjectReflectionTest
    FOLDER "Test/Core/Object"
    MEMBER ObjectReflectionTest ObjectReflectionTest.cpp
    MEMBER RuntimeSchemaSnapshotTest RuntimeSchemaSnapshotTest.cpp
    MEMBER ObjectCastTest ObjectCastTest.cpp
    MEMBER PropertyBagTest PropertyBagTest.cpp
    MEMBER SchemaProjectionTest SchemaProjectionTest.cpp
    MEMBER StructPropertySerializeTest ${CMAKE_SOURCE_DIR}/Test/Reflection/StructPropertySerializeTest.cpp
    MEMBER EnumPropertySerializeTest ${CMAKE_SOURCE_DIR}/Test/Reflection/EnumPropertySerializeTest.cpp
    SOURCES
        Animal.cpp
)
target_link_libraries(ObjectReflectionTest PRIVATE Core)
target_include_directories(ObjectReflectionTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

norves_add_test_bundle(ResourceRegistryTest
    FOLDER "Test/Core/Object"
    MEMBER ResourceRegistryTest ResourceRegistryTest.cpp
    MEMBER ResourceMetadataTest ResourceMetadataTest.cpp
    MEMBER SkeletalResourceLifetimeTest SkeletalResourceLifetimeTest.cpp
)
target_link_libraries(ResourceRegistryTest PRIVATE Core)
target_include_directories(ResourceRegistryTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)

norves_add_test_bundle(ObjectHeapTest
    FOLDER "Test/Core/Object"
    MEMBER ObjectHeapTest ObjectHeapTest.cpp
    MEMBER InvocationQueueTest InvocationQueueTest.cpp
    MEMBER ObjectGCTest ObjectGCTest.cpp
    MEMBER IncrementalGCTest IncrementalGCTest.cpp
    SOURCES
        Animal.cpp
)
target_link_libraries(ObjectHeapTest PRIVATE Core)
target_include_directories(ObjectHeapTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

norves_add_test_bundle(SceneQueryRaycastTest
    FOLDER "Test/Core/Object"
    MEMBER SceneQueryRaycastTest SceneQueryRaycastTest.cpp
    MEMBER SceneQueryOverlapTest SceneQueryOverlapTest.cpp
    MEMBER SceneQueryPhysicsFacadeTest SceneQueryPhysicsFacadeTest.cpp
)
target_link_libraries(SceneQueryRaycastTest PRIVATE Core)
target_include_directories(SceneQueryRaycastTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)

norves_add_test_bundle(EntityTreeTest
    FOLDER "Test/Core/Object"
    MEMBER EntityTreeTest EntityTreeTest.cpp
    MEMBER LightComponentBuildProxyTest LightComponentBuildProxyTest.cpp
    MEMBER TransformHierarchyTest TransformHierarchyTest.cpp
    MEMBER EntityWorldAABBTest EntityWorldAABBTest.cpp
    MEMBER SpringArmComponentTest SpringArmComponentTest.cpp
)
target_link_libraries(EntityTreeTest PRIVATE Core)
target_include_directories(EntityTreeTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)

norves_add_test_bundle(SceneSerializerTest
    FOLDER "Test/Core/Object"
    MEMBER SceneSerializerTest SceneSerializerTest.cpp
    MEMBER EntitySubtreeSnapshotTest EntitySubtreeSnapshotTest.cpp
    MEMBER PrefabRoundTripTest PrefabRoundTripTest.cpp
    MEMBER SceneFileRoundTripTest SceneFileRoundTripTest.cpp
)
target_link_libraries(SceneSerializerTest PRIVATE Core)
target_include_directories(SceneSerializerTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
target_compile_definitions(SceneSerializerTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)

include(CTest)
add_test(NAME ObjectReflectionTest COMMAND ObjectReflectionTest)
add_test(NAME ResourceRegistryTest COMMAND ResourceRegistryTest)
add_test(NAME RuntimeSchemaSnapshotTest COMMAND ObjectReflectionTest --test=RuntimeSchemaSnapshotTest)
add_test(NAME ObjectHeapTest COMMAND ObjectHeapTest)
add_test(NAME ObjectCastTest COMMAND ObjectReflectionTest --test=ObjectCastTest)
add_test(NAME LightComponentBuildProxyTest COMMAND EntityTreeTest --test=LightComponentBuildProxyTest)
add_test(NAME PropertyBagTest COMMAND ObjectReflectionTest --test=PropertyBagTest)
add_test(NAME InvocationQueueTest COMMAND ObjectHeapTest --test=InvocationQueueTest)
add_test(NAME ResourceMetadataTest COMMAND ResourceRegistryTest --test=ResourceMetadataTest)
add_test(NAME ObjectGCTest COMMAND ObjectHeapTest --test=ObjectGCTest)
add_test(NAME IncrementalGCTest COMMAND ObjectHeapTest --test=IncrementalGCTest)
add_test(NAME SchemaProjectionTest COMMAND ObjectReflectionTest --test=SchemaProjectionTest)
add_test(NAME EntitySubtreeSnapshotTest COMMAND SceneSerializerTest --test=EntitySubtreeSnapshotTest)
add_test(NAME PrefabRoundTripTest COMMAND SceneSerializerTest --test=PrefabRoundTripTest)
add_test(NAME TransformHierarchyTest COMMAND EntityTreeTest --test=TransformHierarchyTest)
add_test(NAME EntityWorldAABBTest COMMAND EntityTreeTest --test=EntityWorldAABBTest)
add_test(NAME SceneQueryRaycastTest COMMAND SceneQueryRaycastTest)
add_test(NAME SceneQueryOverlapTest COMMAND SceneQueryRaycastTest --test=SceneQueryOverlapTest)
add_test(NAME SceneQueryPhysicsFacadeTest COMMAND SceneQueryRaycastTest --test=SceneQueryPhysicsFacadeTest)
add_test(NAME EntityTreeTest COMMAND EntityTreeTest)
add_test(NAME SceneSerializerTest COMMAND SceneSerializerTest)
add_test(NAME SceneFileRoundTripTest COMMAND SceneSerializerTest --test=SceneFileRoundTripTest)

add_test(NAME SkeletalResourceLifetimeTest COMMAND ResourceRegistryTest --test=SkeletalResourceLifetimeTest)

add_test(NAME SpringArmComponentTest COMMAND EntityTreeTest --test=SpringArmComponentTest)

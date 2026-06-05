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

include(CTest)
add_test(NAME ObjectReflectionTest COMMAND ObjectReflectionTest)
add_test(NAME ResourceRegistryTest COMMAND ResourceRegistryTest)
add_test(NAME RuntimeSchemaSnapshotTest COMMAND RuntimeSchemaSnapshotTest)
add_test(NAME ObjectHeapTest COMMAND ObjectHeapTest)
add_test(NAME PropertyBagTest COMMAND PropertyBagTest)
add_test(NAME InvocationQueueTest COMMAND InvocationQueueTest)
add_test(NAME ResourceMetadataTest COMMAND ResourceMetadataTest)

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

include(CTest)
add_test(NAME ObjectReflectionTest COMMAND ObjectReflectionTest)
add_test(NAME ResourceRegistryTest COMMAND ResourceRegistryTest)

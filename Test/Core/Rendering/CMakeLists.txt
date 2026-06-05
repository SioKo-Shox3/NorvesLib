add_executable(FramePacketManagerTest
    FramePacketManagerTest.cpp
)
target_link_libraries(FramePacketManagerTest PRIVATE Core)
target_include_directories(FramePacketManagerTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(FramePacketManagerTest PROPERTIES FOLDER "Test/Core/Rendering")

include(CTest)
add_test(NAME FramePacketManagerTest COMMAND FramePacketManagerTest)

add_executable(MeshBatcherTest
    MeshBatcherTest.cpp
)
target_link_libraries(MeshBatcherTest PRIVATE Core)
target_include_directories(MeshBatcherTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(MeshBatcherTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME MeshBatcherTest COMMAND MeshBatcherTest)

add_executable(ViewScreenTopologyTest
    ViewScreenTopologyTest.cpp
)
target_link_libraries(ViewScreenTopologyTest PRIVATE Core)
target_include_directories(ViewScreenTopologyTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(ViewScreenTopologyTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME ViewScreenTopologyTest COMMAND ViewScreenTopologyTest)

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

add_executable(SceneViewViewportCommandTest
    SceneViewViewportCommandTest.cpp
)
target_link_libraries(SceneViewViewportCommandTest PRIVATE Core)
target_include_directories(SceneViewViewportCommandTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(SceneViewViewportCommandTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME SceneViewViewportCommandTest COMMAND SceneViewViewportCommandTest)

add_executable(CameraViewConstantsTest
    CameraViewConstantsTest.cpp
)
target_link_libraries(CameraViewConstantsTest PRIVATE Core)
target_include_directories(CameraViewConstantsTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(CameraViewConstantsTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME CameraViewConstantsTest COMMAND CameraViewConstantsTest)

add_executable(ViewRenderContextActiveInputTest
    ViewRenderContextActiveInputTest.cpp
)
target_link_libraries(ViewRenderContextActiveInputTest PRIVATE Core)
target_include_directories(ViewRenderContextActiveInputTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(ViewRenderContextActiveInputTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME ViewRenderContextActiveInputTest COMMAND ViewRenderContextActiveInputTest)

add_executable(SceneViewProxyReconcileTest
    SceneViewProxyReconcileTest.cpp
)
target_link_libraries(SceneViewProxyReconcileTest PRIVATE Core)
target_include_directories(SceneViewProxyReconcileTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(SceneViewProxyReconcileTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME SceneViewProxyReconcileTest COMMAND SceneViewProxyReconcileTest)

add_executable(RenderFrameExecutorPlanTest
    RenderFrameExecutorPlanTest.cpp
)
target_link_libraries(RenderFrameExecutorPlanTest PRIVATE Core)
target_include_directories(RenderFrameExecutorPlanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(RenderFrameExecutorPlanTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RenderFrameExecutorPlanTest COMMAND RenderFrameExecutorPlanTest)

add_executable(CookedTextureUploadTest
    CookedTextureUploadTest.cpp
)
target_link_libraries(CookedTextureUploadTest PRIVATE Core)
target_include_directories(CookedTextureUploadTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
set_target_properties(CookedTextureUploadTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME CookedTextureUploadTest COMMAND CookedTextureUploadTest)

add_executable(RenderResourceManagerTextureAssetTest
    RenderResourceManagerTextureAssetTest.cpp
)
target_link_libraries(RenderResourceManagerTextureAssetTest PRIVATE Core)
target_include_directories(RenderResourceManagerTextureAssetTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(RenderResourceManagerTextureAssetTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RenderResourceManagerTextureAssetTest COMMAND RenderResourceManagerTextureAssetTest)

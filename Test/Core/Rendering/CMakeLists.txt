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

add_executable(InstanceDataFlattenTest
    InstanceDataFlattenTest.cpp
)
target_link_libraries(InstanceDataFlattenTest PRIVATE Core)
target_include_directories(InstanceDataFlattenTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(InstanceDataFlattenTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME InstanceDataFlattenTest COMMAND InstanceDataFlattenTest)

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

add_executable(SceneViewProxyLookupTest
    SceneViewProxyLookupTest.cpp
)
target_link_libraries(SceneViewProxyLookupTest PRIVATE Core)
target_include_directories(SceneViewProxyLookupTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(SceneViewProxyLookupTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME SceneViewProxyLookupTest COMMAND SceneViewProxyLookupTest)

add_executable(ComponentDirtyTrackingTest
    ComponentDirtyTrackingTest.cpp
)
target_link_libraries(ComponentDirtyTrackingTest PRIVATE Core)
target_include_directories(ComponentDirtyTrackingTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(ComponentDirtyTrackingTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME ComponentDirtyTrackingTest COMMAND ComponentDirtyTrackingTest)

add_executable(WorldSyncDifferentialTest
    WorldSyncDifferentialTest.cpp
)
target_link_libraries(WorldSyncDifferentialTest PRIVATE Core)
target_include_directories(WorldSyncDifferentialTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(WorldSyncDifferentialTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME WorldSyncDifferentialTest COMMAND WorldSyncDifferentialTest)

add_executable(RenderFrameExecutorPlanTest
    RenderFrameExecutorPlanTest.cpp
)
target_link_libraries(RenderFrameExecutorPlanTest PRIVATE Core)
target_include_directories(RenderFrameExecutorPlanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(RenderFrameExecutorPlanTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RenderFrameExecutorPlanTest COMMAND RenderFrameExecutorPlanTest)

add_executable(TransientResourcePoolTest
    TransientResourcePoolTest.cpp
)
target_link_libraries(TransientResourcePoolTest PRIVATE Core)
target_include_directories(TransientResourcePoolTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(TransientResourcePoolTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME TransientResourcePoolTest COMMAND TransientResourcePoolTest)
add_executable(InstanceBufferRingTest
    InstanceBufferRingTest.cpp
)
target_link_libraries(InstanceBufferRingTest PRIVATE Core)
target_include_directories(InstanceBufferRingTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(InstanceBufferRingTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME InstanceBufferRingTest COMMAND InstanceBufferRingTest)


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

add_executable(TextureHandleCacheTest
    TextureHandleCacheTest.cpp
)
target_link_libraries(TextureHandleCacheTest PRIVATE Core)
target_include_directories(TextureHandleCacheTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
set_target_properties(TextureHandleCacheTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME TextureHandleCacheTest COMMAND TextureHandleCacheTest)

add_executable(TextureAsyncTypesTest
    TextureAsyncTypesTest.cpp
)
target_link_libraries(TextureAsyncTypesTest PRIVATE Core)
target_include_directories(TextureAsyncTypesTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
set_target_properties(TextureAsyncTypesTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME TextureAsyncTypesTest COMMAND TextureAsyncTypesTest)

add_executable(TextureResourcesTextureAssetTest
    TextureResourcesTextureAssetTest.cpp
)
target_link_libraries(TextureResourcesTextureAssetTest PRIVATE Core)
target_include_directories(TextureResourcesTextureAssetTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(TextureResourcesTextureAssetTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME TextureResourcesTextureAssetTest COMMAND TextureResourcesTextureAssetTest)

add_executable(TextureResourcesPreparedTextureAssetTest
    TextureResourcesPreparedTextureAssetTest.cpp
)
target_link_libraries(TextureResourcesPreparedTextureAssetTest PRIVATE Core)
target_include_directories(TextureResourcesPreparedTextureAssetTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(TextureResourcesPreparedTextureAssetTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME TextureResourcesPreparedTextureAssetTest COMMAND TextureResourcesPreparedTextureAssetTest)

add_executable(TextureResourcesAssetLoadProfileContractTest
    TextureResourcesAssetLoadProfileContractTest.cpp
)
target_link_libraries(TextureResourcesAssetLoadProfileContractTest PRIVATE Core)
target_include_directories(TextureResourcesAssetLoadProfileContractTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(TextureResourcesAssetLoadProfileContractTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME TextureResourcesAssetLoadProfileContractTest COMMAND TextureResourcesAssetLoadProfileContractTest)

add_executable(RenderResourcesDomainContractTest
    RenderResourcesDomainContractTest.cpp
)
target_link_libraries(RenderResourcesDomainContractTest PRIVATE Core)
target_include_directories(RenderResourcesDomainContractTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(RenderResourcesDomainContractTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RenderResourcesDomainContractTest COMMAND RenderResourcesDomainContractTest)

add_executable(MeshResourcesProceduralGpuTest
    MeshResourcesProceduralGpuTest.cpp
)
target_link_libraries(MeshResourcesProceduralGpuTest PRIVATE Core)
target_include_directories(MeshResourcesProceduralGpuTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(MeshResourcesProceduralGpuTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME MeshResourcesProceduralGpuTest COMMAND MeshResourcesProceduralGpuTest)

add_executable(MegaGeometryResourcesTest
    MegaGeometryResourcesTest.cpp
)
target_link_libraries(MegaGeometryResourcesTest PRIVATE Core)
target_include_directories(MegaGeometryResourcesTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(MegaGeometryResourcesTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME MegaGeometryResourcesTest COMMAND MegaGeometryResourcesTest)

add_executable(MaterialResourcesTest
    MaterialResourcesTest.cpp
)
target_link_libraries(MaterialResourcesTest PRIVATE Core)
target_include_directories(MaterialResourcesTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(MaterialResourcesTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME MaterialResourcesTest COMMAND MaterialResourcesTest)

add_executable(TextureResourcesAssetCookSmokeTest
    TextureResourcesAssetCookSmokeTest.cpp
)
target_link_libraries(TextureResourcesAssetCookSmokeTest PRIVATE Core)
target_include_directories(TextureResourcesAssetCookSmokeTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
add_dependencies(TextureResourcesAssetCookSmokeTest AssetCook)
set_target_properties(TextureResourcesAssetCookSmokeTest PROPERTIES FOLDER "Test/Core/Rendering")

set(TEXTURE_RESOURCES_ASSET_COOK_SMOKE_DIR
    "${CMAKE_BINARY_DIR}/TextureResourcesAssetCookSmoke/$<CONFIG>")
add_test(NAME TextureResourcesAssetCookSmoke
    COMMAND ${CMAKE_COMMAND}
        "-DASSET_COOK_EXE=$<TARGET_FILE:AssetCook>"
        "-DRUNTIME_TEST_EXE=$<TARGET_FILE:TextureResourcesAssetCookSmokeTest>"
        "-DSMOKE_DIR=${TEXTURE_RESOURCES_ASSET_COOK_SMOKE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/TextureResourcesAssetCookSmoke.cmake.in"
)

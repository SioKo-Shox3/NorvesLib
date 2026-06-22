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

add_executable(MeshBatcherInstancingTest
    MeshBatcherInstancingTest.cpp
)
target_link_libraries(MeshBatcherInstancingTest PRIVATE Core)
target_include_directories(MeshBatcherInstancingTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(MeshBatcherInstancingTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME MeshBatcherInstancingTest COMMAND MeshBatcherInstancingTest)

add_executable(InstanceDataFlattenTest
    InstanceDataFlattenTest.cpp
)
target_link_libraries(InstanceDataFlattenTest PRIVATE Core)
target_include_directories(InstanceDataFlattenTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(InstanceDataFlattenTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME InstanceDataFlattenTest COMMAND InstanceDataFlattenTest)

add_executable(DrawCommandSorterTest
    DrawCommandSorterTest.cpp
)
target_link_libraries(DrawCommandSorterTest PRIVATE Core)
target_include_directories(DrawCommandSorterTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(DrawCommandSorterTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME DrawCommandSorterTest COMMAND DrawCommandSorterTest)

add_executable(ForwardPassPipelinePlacementTest
    ForwardPassPipelinePlacementTest.cpp
)
target_link_libraries(ForwardPassPipelinePlacementTest PRIVATE Core)
target_include_directories(ForwardPassPipelinePlacementTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(ForwardPassPipelinePlacementTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME ForwardPassPipelinePlacementTest COMMAND ForwardPassPipelinePlacementTest)

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

add_executable(CameraTableTest
    CameraTableTest.cpp
)
target_link_libraries(CameraTableTest PRIVATE Core)
target_include_directories(CameraTableTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(CameraTableTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME CameraTableTest COMMAND CameraTableTest)

add_executable(ViewportCameraIdRenderPlanTest
    ViewportCameraIdRenderPlanTest.cpp
)
target_link_libraries(ViewportCameraIdRenderPlanTest PRIVATE Core)
target_include_directories(ViewportCameraIdRenderPlanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(ViewportCameraIdRenderPlanTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME ViewportCameraIdRenderPlanTest COMMAND ViewportCameraIdRenderPlanTest)

add_executable(OrthographicViewportMatrixTest
    OrthographicViewportMatrixTest.cpp
)
target_link_libraries(OrthographicViewportMatrixTest PRIVATE Core)
target_include_directories(OrthographicViewportMatrixTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(OrthographicViewportMatrixTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME OrthographicViewportMatrixTest COMMAND OrthographicViewportMatrixTest)

add_executable(ViewRenderContextActiveInputTest
    ViewRenderContextActiveInputTest.cpp
)
target_link_libraries(ViewRenderContextActiveInputTest PRIVATE Core)
target_include_directories(ViewRenderContextActiveInputTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(ViewRenderContextActiveInputTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME ViewRenderContextActiveInputTest COMMAND ViewRenderContextActiveInputTest)

add_executable(LightingParamsLayoutTest
    LightingParamsLayoutTest.cpp
)
target_link_libraries(LightingParamsLayoutTest PRIVATE Core)
target_include_directories(LightingParamsLayoutTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
target_compile_definitions(LightingParamsLayoutTest PRIVATE
    NORVES_SHADER_DIR="${CMAKE_SOURCE_DIR}/Assets/Shaders"
)
set_target_properties(LightingParamsLayoutTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME LightingParamsLayoutTest COMMAND LightingParamsLayoutTest)

add_executable(ToneMappingParamsLayoutTest
    ToneMappingParamsLayoutTest.cpp
)
target_link_libraries(ToneMappingParamsLayoutTest PRIVATE Core)
target_include_directories(ToneMappingParamsLayoutTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
target_compile_definitions(ToneMappingParamsLayoutTest PRIVATE
    NORVES_SHADER_DIR="${CMAKE_SOURCE_DIR}/Assets/Shaders"
)
set_target_properties(ToneMappingParamsLayoutTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME ToneMappingParamsLayoutTest COMMAND ToneMappingParamsLayoutTest)

add_executable(DebugPostProcessPassThroughContractTest
    DebugPostProcessPassThroughContractTest.cpp
)
target_link_libraries(DebugPostProcessPassThroughContractTest PRIVATE Core)
target_include_directories(DebugPostProcessPassThroughContractTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
target_compile_definitions(DebugPostProcessPassThroughContractTest PRIVATE
    NORVES_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
set_target_properties(DebugPostProcessPassThroughContractTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME DebugPostProcessPassThroughContractTest COMMAND DebugPostProcessPassThroughContractTest)

add_executable(DebugViewModeStringTest
    DebugViewModeStringTest.cpp
)
target_link_libraries(DebugViewModeStringTest PRIVATE Core)
target_include_directories(DebugViewModeStringTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(DebugViewModeStringTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME DebugViewModeStringTest COMMAND DebugViewModeStringTest)

add_executable(ViewportDebugViewModeReleaseClampTest
    ViewportDebugViewModeReleaseClampTest.cpp
)
target_link_libraries(ViewportDebugViewModeReleaseClampTest PRIVATE Core)
target_include_directories(ViewportDebugViewModeReleaseClampTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(ViewportDebugViewModeReleaseClampTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME ViewportDebugViewModeReleaseClampTest COMMAND ViewportDebugViewModeReleaseClampTest)

add_executable(ViewportRenderPlanDebugModeTest
    ViewportRenderPlanDebugModeTest.cpp
)
target_link_libraries(ViewportRenderPlanDebugModeTest PRIVATE Core)
target_include_directories(ViewportRenderPlanDebugModeTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(ViewportRenderPlanDebugModeTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME ViewportRenderPlanDebugModeTest COMMAND ViewportRenderPlanDebugModeTest)

add_executable(ViewportSnapshotDebugWiringTest
    ViewportSnapshotDebugWiringTest.cpp
)
target_link_libraries(ViewportSnapshotDebugWiringTest PRIVATE Core)
target_include_directories(ViewportSnapshotDebugWiringTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(ViewportSnapshotDebugWiringTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME ViewportSnapshotDebugWiringTest COMMAND ViewportSnapshotDebugWiringTest)

add_executable(MegaGeometryFrameCommandDebugModeTest
    MegaGeometryFrameCommandDebugModeTest.cpp
)
target_link_libraries(MegaGeometryFrameCommandDebugModeTest PRIVATE Core)
target_include_directories(MegaGeometryFrameCommandDebugModeTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
target_compile_definitions(MegaGeometryFrameCommandDebugModeTest PRIVATE
    NORVES_SHADER_DIR="${CMAKE_SOURCE_DIR}/Assets/Shaders"
)
set_target_properties(MegaGeometryFrameCommandDebugModeTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME MegaGeometryFrameCommandDebugModeTest COMMAND MegaGeometryFrameCommandDebugModeTest)

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

add_executable(ComponentDataRegistryTest
    ComponentDataRegistryTest.cpp
)
target_link_libraries(ComponentDataRegistryTest PRIVATE Core)
target_include_directories(ComponentDataRegistryTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(ComponentDataRegistryTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME ComponentDataRegistryTest COMMAND ComponentDataRegistryTest)

add_executable(ComponentDataBenchmark
    ComponentDataBenchmark.cpp
)
target_link_libraries(ComponentDataBenchmark PRIVATE Core)
target_include_directories(ComponentDataBenchmark PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(ComponentDataBenchmark PROPERTIES FOLDER "Test/Core/Rendering")

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

add_executable(CanvasViewRenderTest
    CanvasViewRenderTest.cpp
)
target_link_libraries(CanvasViewRenderTest PRIVATE Core)
target_include_directories(CanvasViewRenderTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(CanvasViewRenderTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME CanvasViewRenderTest COMMAND CanvasViewRenderTest)

add_executable(BoardInstanceBatchingTest
    BoardInstanceBatchingTest.cpp
)
target_link_libraries(BoardInstanceBatchingTest PRIVATE Core)
target_include_directories(BoardInstanceBatchingTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(BoardInstanceBatchingTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME BoardInstanceBatchingTest COMMAND BoardInstanceBatchingTest)

add_executable(BoardComponentRoutingTest
    BoardComponentRoutingTest.cpp
)
target_link_libraries(BoardComponentRoutingTest PRIVATE Core)
target_include_directories(BoardComponentRoutingTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(BoardComponentRoutingTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME BoardComponentRoutingTest COMMAND BoardComponentRoutingTest)

add_executable(BillboardComponentTest
    BillboardComponentTest.cpp
)
target_link_libraries(BillboardComponentTest PRIVATE Core)
target_include_directories(BillboardComponentTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(BillboardComponentTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME BillboardComponentTest COMMAND BillboardComponentTest)

add_executable(SceneViewWorldBoardDrawCommandTest
    SceneViewWorldBoardDrawCommandTest.cpp
)
target_link_libraries(SceneViewWorldBoardDrawCommandTest PRIVATE Core)
target_include_directories(SceneViewWorldBoardDrawCommandTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(SceneViewWorldBoardDrawCommandTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME SceneViewWorldBoardDrawCommandTest COMMAND SceneViewWorldBoardDrawCommandTest)

add_executable(ForwardPassWorldBoardRoutingTest
    ForwardPassWorldBoardRoutingTest.cpp
)
target_link_libraries(ForwardPassWorldBoardRoutingTest PRIVATE Core)
target_include_directories(ForwardPassWorldBoardRoutingTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
target_compile_definitions(ForwardPassWorldBoardRoutingTest PRIVATE
    NORVES_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
set_target_properties(ForwardPassWorldBoardRoutingTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME ForwardPassWorldBoardRoutingTest COMMAND ForwardPassWorldBoardRoutingTest)

add_executable(BoardTransformTest
    BoardTransformTest.cpp
)
target_link_libraries(BoardTransformTest PRIVATE Core)
target_include_directories(BoardTransformTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(BoardTransformTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME BoardTransformTest COMMAND BoardTransformTest)

add_executable(SpriteSheetUVRectTest
    SpriteSheetUVRectTest.cpp
)
target_link_libraries(SpriteSheetUVRectTest PRIVATE Core)
target_include_directories(SpriteSheetUVRectTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(SpriteSheetUVRectTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME SpriteSheetUVRectTest COMMAND SpriteSheetUVRectTest)

add_executable(FlipbookAdvanceTest
    FlipbookAdvanceTest.cpp
)
target_link_libraries(FlipbookAdvanceTest PRIVATE Core)
target_include_directories(FlipbookAdvanceTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(FlipbookAdvanceTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME FlipbookAdvanceTest COMMAND FlipbookAdvanceTest)

add_executable(BoardBlendModeTest
    BoardBlendModeTest.cpp
)
target_link_libraries(BoardBlendModeTest PRIVATE Core)
target_include_directories(BoardBlendModeTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(BoardBlendModeTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME BoardBlendModeTest COMMAND BoardBlendModeTest)

add_executable(BoardSortKeyStableTest
    BoardSortKeyStableTest.cpp
)
target_link_libraries(BoardSortKeyStableTest PRIVATE Core)
target_include_directories(BoardSortKeyStableTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(BoardSortKeyStableTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME BoardSortKeyStableTest COMMAND BoardSortKeyStableTest)

add_executable(MultiLayerOrderTest
    MultiLayerOrderTest.cpp
)
target_link_libraries(MultiLayerOrderTest PRIVATE Core)
target_include_directories(MultiLayerOrderTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(MultiLayerOrderTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME MultiLayerOrderTest COMMAND MultiLayerOrderTest)

add_executable(CompositePassTest
    CompositePassTest.cpp
)
target_link_libraries(CompositePassTest PRIVATE Core)
target_include_directories(CompositePassTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(CompositePassTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME CompositePassTest COMMAND CompositePassTest)

add_executable(PresentationComposerTest
    PresentationComposerTest.cpp
)
target_link_libraries(PresentationComposerTest PRIVATE Core)
target_include_directories(PresentationComposerTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(PresentationComposerTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME PresentationComposerTest COMMAND PresentationComposerTest)

add_executable(RenderGraphPresentationPassTest
    RenderGraphPresentationPassTest.cpp
)
target_link_libraries(RenderGraphPresentationPassTest PRIVATE Core)
target_include_directories(RenderGraphPresentationPassTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(RenderGraphPresentationPassTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RenderGraphPresentationPassTest COMMAND RenderGraphPresentationPassTest)

add_executable(RenderGraphCompileTest
    RenderGraphCompileTest.cpp
)
target_link_libraries(RenderGraphCompileTest PRIVATE Core)
target_include_directories(RenderGraphCompileTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(RenderGraphCompileTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RenderGraphCompileTest COMMAND RenderGraphCompileTest)

add_executable(RenderGraphLifetimeTest
    RenderGraphLifetimeTest.cpp
)
target_link_libraries(RenderGraphLifetimeTest PRIVATE Core)
target_include_directories(RenderGraphLifetimeTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(RenderGraphLifetimeTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RenderGraphLifetimeTest COMMAND RenderGraphLifetimeTest)

add_executable(RenderGraphNamedResourceTest
    RenderGraphNamedResourceTest.cpp
)
target_link_libraries(RenderGraphNamedResourceTest PRIVATE Core)
target_include_directories(RenderGraphNamedResourceTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(RenderGraphNamedResourceTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RenderGraphNamedResourceTest COMMAND RenderGraphNamedResourceTest)

add_executable(RenderGraphAttachmentStateTest
    RenderGraphAttachmentStateTest.cpp
)
target_link_libraries(RenderGraphAttachmentStateTest PRIVATE Core)
target_include_directories(RenderGraphAttachmentStateTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(RenderGraphAttachmentStateTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RenderGraphAttachmentStateTest COMMAND RenderGraphAttachmentStateTest)

add_executable(RenderGraphDumpTest
    RenderGraphDumpTest.cpp
)
target_link_libraries(RenderGraphDumpTest PRIVATE Core)
target_include_directories(RenderGraphDumpTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(RenderGraphDumpTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RenderGraphDumpTest COMMAND RenderGraphDumpTest)

add_executable(LegacyViewPassAdapterTest
    LegacyViewPassAdapterTest.cpp
)
target_link_libraries(LegacyViewPassAdapterTest PRIVATE Core)
target_include_directories(LegacyViewPassAdapterTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(LegacyViewPassAdapterTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME LegacyViewPassAdapterTest COMMAND LegacyViewPassAdapterTest)

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

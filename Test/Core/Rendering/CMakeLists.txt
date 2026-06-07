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

add_executable(RenderResourceManagerPreparedTextureAssetTest
    RenderResourceManagerPreparedTextureAssetTest.cpp
)
target_link_libraries(RenderResourceManagerPreparedTextureAssetTest PRIVATE Core)
target_include_directories(RenderResourceManagerPreparedTextureAssetTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(RenderResourceManagerPreparedTextureAssetTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RenderResourceManagerPreparedTextureAssetTest COMMAND RenderResourceManagerPreparedTextureAssetTest)

add_executable(RenderResourceManagerAssetLoadProfileContractTest
    RenderResourceManagerAssetLoadProfileContractTest.cpp
)
target_link_libraries(RenderResourceManagerAssetLoadProfileContractTest PRIVATE Core)
target_include_directories(RenderResourceManagerAssetLoadProfileContractTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(RenderResourceManagerAssetLoadProfileContractTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RenderResourceManagerAssetLoadProfileContractTest COMMAND RenderResourceManagerAssetLoadProfileContractTest)

add_executable(RenderResourceManagerResourceContractTest
    RenderResourceManagerResourceContractTest.cpp
)
target_link_libraries(RenderResourceManagerResourceContractTest PRIVATE Core)
target_include_directories(RenderResourceManagerResourceContractTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(RenderResourceManagerResourceContractTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RenderResourceManagerResourceContractTest COMMAND RenderResourceManagerResourceContractTest)

add_executable(RenderResourceManagerProceduralMeshStoreTest
    RenderResourceManagerProceduralMeshStoreTest.cpp
)
target_link_libraries(RenderResourceManagerProceduralMeshStoreTest PRIVATE Core)
target_include_directories(RenderResourceManagerProceduralMeshStoreTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(RenderResourceManagerProceduralMeshStoreTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RenderResourceManagerProceduralMeshStoreTest COMMAND RenderResourceManagerProceduralMeshStoreTest)

add_executable(RenderResourceManagerMegaGeometryStoreTest
    RenderResourceManagerMegaGeometryStoreTest.cpp
)
target_link_libraries(RenderResourceManagerMegaGeometryStoreTest PRIVATE Core)
target_include_directories(RenderResourceManagerMegaGeometryStoreTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(RenderResourceManagerMegaGeometryStoreTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RenderResourceManagerMegaGeometryStoreTest COMMAND RenderResourceManagerMegaGeometryStoreTest)

add_executable(RenderResourceManagerMaterialStoreTest
    RenderResourceManagerMaterialStoreTest.cpp
)
target_link_libraries(RenderResourceManagerMaterialStoreTest PRIVATE Core)
target_include_directories(RenderResourceManagerMaterialStoreTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(RenderResourceManagerMaterialStoreTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RenderResourceManagerMaterialStoreTest COMMAND RenderResourceManagerMaterialStoreTest)

add_executable(RenderResourceManagerAssetCookSmokeTest
    RenderResourceManagerAssetCookSmokeTest.cpp
)
target_link_libraries(RenderResourceManagerAssetCookSmokeTest PRIVATE Core)
target_include_directories(RenderResourceManagerAssetCookSmokeTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
add_dependencies(RenderResourceManagerAssetCookSmokeTest AssetCook)
set_target_properties(RenderResourceManagerAssetCookSmokeTest PROPERTIES FOLDER "Test/Core/Rendering")

set(RENDER_RESOURCE_MANAGER_ASSET_COOK_SMOKE_DIR
    "${CMAKE_BINARY_DIR}/RenderResourceManagerAssetCookSmoke/$<CONFIG>")
add_test(NAME RenderResourceManagerAssetCookSmoke
    COMMAND ${CMAKE_COMMAND}
        "-DASSET_COOK_EXE=$<TARGET_FILE:AssetCook>"
        "-DRUNTIME_TEST_EXE=$<TARGET_FILE:RenderResourceManagerAssetCookSmokeTest>"
        "-DSMOKE_DIR=${RENDER_RESOURCE_MANAGER_ASSET_COOK_SMOKE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/RenderResourceManagerAssetCookSmoke.cmake.in"
)

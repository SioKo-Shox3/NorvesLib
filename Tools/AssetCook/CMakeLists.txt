# BC エンコーダ(bc7enc_rdo・DirectXTex)は AssetCook だけがリンクする。Core・Game には入れない。
add_subdirectory(${CMAKE_SOURCE_DIR}/Library/ThirdParty/bc7enc_rdo
    ${CMAKE_BINARY_DIR}/ThirdParty/bc7enc_rdo)
add_subdirectory(${CMAKE_SOURCE_DIR}/Library/ThirdParty/DirectXTex
    ${CMAKE_BINARY_DIR}/ThirdParty/DirectXTex)

add_library(AssetCookBlockCompressor STATIC
    BlockCompressor.cpp
    BlockCompressor.h
)
target_link_libraries(AssetCookBlockCompressor PUBLIC NorvesThirdParty_Bc7Enc NorvesThirdParty_DirectXTexBC Core)
target_include_directories(AssetCookBlockCompressor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(AssetCookBlockCompressor PROPERTIES FOLDER "Tools")

# メッシュ簡略化(meshoptimizer)も AssetCook だけがリンクする。Core・Game には入れない。
add_subdirectory(${CMAKE_SOURCE_DIR}/Library/ThirdParty/meshoptimizer
    ${CMAKE_BINARY_DIR}/ThirdParty/meshoptimizer)

add_library(AssetCookMeshOptimizer STATIC
    CookMeshDag.cpp
    CookMeshDag.h
    CookMeshOptimizer.cpp
    CookMeshOptimizer.h
)
target_link_libraries(AssetCookMeshOptimizer PUBLIC NorvesThirdParty_MeshOptimizer Core)
target_include_directories(AssetCookMeshOptimizer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(AssetCookMeshOptimizer PROPERTIES FOLDER "Tools")

# テクスチャのクック(用途ごとの形式・ミップ・NVTEX の書き出し)。スモークからも使えるよう独立の静的ライブラリにする。
add_library(AssetCookTextureCooker STATIC
    TextureCooker.cpp
    TextureCooker.h
)
target_link_libraries(AssetCookTextureCooker PUBLIC AssetCookBlockCompressor Core)
target_link_libraries(AssetCookTextureCooker PRIVATE NorvesThirdParty_TinyEXR)
target_include_directories(AssetCookTextureCooker
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/Library/ThirdParty/stb
        ${CMAKE_SOURCE_DIR}/Library/ThirdParty/tinyexr/include
)
set_target_properties(AssetCookTextureCooker PROPERTIES FOLDER "Tools")


# CLIと直接リンクする試験で同じcooker実装を共有する。
add_library(AssetCookLib STATIC
    CookStateFile.cpp
    CookStateFile.h
    CookStateFileTestAccess.h
    CookManagedBootstrap.cpp
    CookManagedUpdate.cpp
    CookManagedUpdate.h
    CookManagedUpdateTestAccess.h
    CookManagedTransactionController.cpp
    CookManagedTransactionController.h
    CookManagedBootstrap.h
    CookManagedOutputs.h
    CookBatchReport.cpp
    CookBatchWorkers.cpp
    CookBatchWorkers.h
    CookBatchReport.h
    CookManagedBootstrapTestAccess.h
    CookManagedStoreNative.h
    CookManagedStoreAccess.h
    CookManagedTransactionIntent.cpp
    CookManagedTransactionIntent.h
    CookInventoryValues.h
    CookReferenceValues.h
    CookManagedStoreIndex.cpp
    CookManagedStoreIndex.h
    ManagedStoreJson.h
    CookManagedStoreInitialization.h
    CookManagedStoreInitializationTestAccess.h
    CookManagedStoreObservation.cpp
    CookManagedStoreObservation.h
    CookManagedUpdateInventory.cpp
    CookManagedUpdateInventory.h
    CookDestinationLock.cpp
    CookDestinationLock.h
    CookDestinationLockTestAccess.h
    CookOwnerResolver.cpp
    CookOwnerResolver.h
    CookOwnerBinding.cpp
    CookOwnerBinding.h
    CookOwnedState.cpp
    CookOwnedState.h
    NativeCookPath.h
    CookCacheDecision.cpp
    CookCacheDecision.h
    CookOutputPlan.h
    CookOutputSetGuard.h
    CookOutputSetGuardTestAccess.h
    CookPathIdentity.cpp
    CookPathIdentity.h
    CookCacheDecisionTestAccess.h
    CookOutputPaths.h
    CookOutputPackage.cpp
    CookRigPayload.cpp
    CookRigPayload.h
    CookOutputPackage.h
    CookDependencySnapshot.cpp
    CookDependencySnapshot.h
    TextureAssetSetCook.cpp
    TextureAssetSetCook.h
    TextureAssetSetCli.cpp
    TextureAssetSetGenerators.cpp
    TextureAssetSetGenerators.h
    TextureAssetSetOutput.cpp
    TextureAssetSetOutput.h
    TextureAssetSetSpec.cpp
    TextureAssetSetSpec.h
    AssetCookOutput.cpp
    AssetCookOutput.h
    AssetCookLegacyOptions.h
    SingleAssetCook.cpp
    SingleAssetCook.h
    AudioCooker.cpp
    AudioCooker.h
    MeshCooker.cpp
    HeightfieldImport.cpp
    HeightfieldImport.h
    GrassFieldCooker.cpp
    GrassFieldCooker.h
    ScatterPlacement.cpp
    ScatterPlacement.h
    ScatterBaker.cpp
    ScatterBaker.h
    ScatterSpec.cpp
    ScatterSpec.h
    TerrainCooker.cpp
    TerrainCooker.h
    TerrainMasks.cpp
    TerrainMasks.h
    TerrainSpec.cpp
    TerrainSpec.h
    TerrainCliffDetail.cpp
    TerrainCliffDetail.h
    CookedTerrainQuery.cpp
    CookedTerrainQuery.h
    Sha256Digest.cpp
    Sha256Digest.h
    SkeletalBvhCook.cpp
    RigClipBankCook.cpp
    RigClipBankCook.h
    SkeletalRoleProfileCook.cpp
    SkeletalRoleFileInput.cpp
    SkeletalRoleFileCook.cpp
    SkeletalRoleFileCook.h
    SkeletalRoleFileCookTestAccess.h
    SkeletalRoleFileReport.cpp
    SkeletalRoleFileReport.h
    SkeletalRoleFileCli.cpp
    SkeletalRoleFileCli.h
    SkeletalRoleFileCliTestAccess.h
    SkeletalRoleFileInputTestAccess.h
    SkeletalRoleFileInput.h
    SkeletalRoleFileRequest.h
    SkeletalRoleProfileCook.h
    SkeletalBvhCook.h
    MeshMaterialV1Plan.cpp
    MeshMaterialV1Plan.h
    MeshCooker.h
    MaterialImport.cpp
    MaterialImport.h
    SkeletalImportReport.cpp
    SkeletalImportReport.h
    ModelInspection.h
    ModelCookCache.cpp
    ModelCookCache.h
    GeometryInspection.cpp
    GeometryInspection.h
    ImageInspection.cpp
    ImageInspection.h
    RigSplitImageDecode.cpp
    RigSplitImageInputs.cpp
    RigSplitImageInputs.h
    RigSplitFileCook.cpp
    RigSingleCook.cpp
    RigRetargetCook.cpp
    RigMotionCleanup.cpp
    RigMotionCleanup.h
    RigRetargetCli.cpp
    RigRetargetCook.h
    RigSingleCook.h
    RigSplitFileCook.h
    RigSplitFileCli.cpp
    NativeCookArguments.h
    RigSplitCook.cpp
    RigSplitCook.h
    VfxEffectCookCli.cpp
    VfxEffectCookCli.h
)
target_link_libraries(AssetCookLib PUBLIC Core AssetCookTextureCooker AssetCookMeshOptimizer)
target_link_libraries(AssetCookLib PRIVATE NorvesThirdParty_TinyEXR)
if(WIN32)
    target_link_libraries(AssetCookLib PRIVATE bcrypt shell32 psapi)
endif()
target_include_directories(AssetCookLib
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/Library/Core/Private
        ${CMAKE_SOURCE_DIR}/Library/ThirdParty/stb
        ${CMAKE_SOURCE_DIR}/Library/ThirdParty/tinyexr/include
)

add_executable(AssetCook Main.cpp)
target_link_libraries(AssetCook PRIVATE AssetCookLib)
target_include_directories(AssetCook PRIVATE
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
    ${CMAKE_SOURCE_DIR}/Library/ThirdParty/stb
)

set_target_properties(AssetCook AssetCookLib PROPERTIES FOLDER "Tools")

# BC エンコーダ(bc7enc_rdo)は AssetCook だけがリンクする。Core・Game には入れない。
add_subdirectory(${CMAKE_SOURCE_DIR}/Library/ThirdParty/bc7enc_rdo
    ${CMAKE_BINARY_DIR}/ThirdParty/bc7enc_rdo)

add_library(AssetCookBlockCompressor STATIC
    BlockCompressor.cpp
    BlockCompressor.h
)
target_link_libraries(AssetCookBlockCompressor PUBLIC NorvesThirdParty_Bc7Enc Core)
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
target_include_directories(AssetCookTextureCooker
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_SOURCE_DIR}/Library/ThirdParty/stb
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
    RigRetargetCli.cpp
    RigRetargetCook.h
    RigSingleCook.h
    RigSplitFileCook.h
    RigSplitFileCli.cpp
    NativeCookArguments.h
    RigSplitCook.cpp
    RigSplitCook.h
)
target_link_libraries(AssetCookLib PUBLIC Core AssetCookTextureCooker AssetCookMeshOptimizer)
if(WIN32)
    target_link_libraries(AssetCookLib PRIVATE bcrypt shell32)
endif()
target_include_directories(AssetCookLib
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/Library/Core/Private
        ${CMAKE_SOURCE_DIR}/Library/ThirdParty/stb
)

add_executable(AssetCook Main.cpp)
target_link_libraries(AssetCook PRIVATE AssetCookLib)
target_include_directories(AssetCook PRIVATE
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
    ${CMAKE_SOURCE_DIR}/Library/ThirdParty/stb
)

set_target_properties(AssetCook AssetCookLib PROPERTIES FOLDER "Tools")

# ブロック圧縮のスモーク。AssetCook をビルドするときに一緒にビルドされるよう依存を張る。
add_executable(AssetCookBlockCompressSmoke AssetCookBlockCompressSmoke.cpp)
target_link_libraries(AssetCookBlockCompressSmoke PRIVATE AssetCookBlockCompressor)
set_target_properties(AssetCookBlockCompressSmoke PROPERTIES FOLDER "Tools")
add_dependencies(AssetCook AssetCookBlockCompressSmoke)
add_test(NAME AssetCookBlockCompressSmoke COMMAND $<TARGET_FILE:AssetCookBlockCompressSmoke>)

# メッシュ簡略化の境界と、LOD の階層の焼き込み(閉じた球から階層を作り、切り口に穴が無いことなど)のスモーク。
add_executable(AssetCookMeshSimplifySmoke AssetCookMeshSimplifySmoke.cpp)
target_link_libraries(AssetCookMeshSimplifySmoke PRIVATE AssetCookMeshOptimizer)
set_target_properties(AssetCookMeshSimplifySmoke PROPERTIES FOLDER "Tools")
add_dependencies(AssetCook AssetCookMeshSimplifySmoke)
add_test(NAME AssetCookMeshSimplifySmoke COMMAND $<TARGET_FILE:AssetCookMeshSimplifySmoke>)

# 用途ごとのクックの中身(ORM の詰め方・法線のミップ・R16 の精度など)を確かめる。AssetCookTextureSmoke から呼ぶ。
add_executable(AssetCookTextureUsageSmoke AssetCookTextureUsageSmoke.cpp)
target_link_libraries(AssetCookTextureUsageSmoke PRIVATE AssetCookTextureCooker)
set_target_properties(AssetCookTextureUsageSmoke PROPERTIES FOLDER "Tools")
add_dependencies(AssetCook AssetCookTextureUsageSmoke)

set(ASSET_COOK_SMOKE_DIR "${CMAKE_BINARY_DIR}/AssetCookSmoke/$<CONFIG>")
add_test(NAME AssetCookRawSmoke
    COMMAND $<TARGET_FILE:AssetCook>
        --input "${CMAKE_SOURCE_DIR}/Test/Core/Asset/Fixtures/AssetCook/raw_sample.bin"
        --out "${ASSET_COOK_SMOKE_DIR}/Cooked/raw_sample.nvpkg"
        --manifest "${ASSET_COOK_SMOKE_DIR}/manifest.json"
        --logical "Raw/raw_sample.bin"
        --kind raw
        --entry "__raw__"
        --entry-type Raw
        --format raw.v0
        --variant default
)

set(ASSET_COOK_TEXTURE_SMOKE_DIR "${CMAKE_BINARY_DIR}/AssetCookSmoke/$<CONFIG>/Texture")
add_test(NAME AssetCookTextureSmoke
    COMMAND ${CMAKE_COMMAND}
        -DASSET_COOK_EXE=$<TARGET_FILE:AssetCook>
        -DUSAGE_SMOKE_EXE=$<TARGET_FILE:AssetCookTextureUsageSmoke>
        -DSMOKE_DIR=${ASSET_COOK_TEXTURE_SMOKE_DIR}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/AssetCookTextureSmoke.cmake.in"
)

set(ASSET_COOK_MESH_SMOKE_DIR "${CMAKE_BINARY_DIR}/AssetCookSmoke/$<CONFIG>/Mesh")
add_test(NAME AssetCookMeshSmoke
    COMMAND ${CMAKE_COMMAND}
        -DASSET_COOK_EXE=$<TARGET_FILE:AssetCook>
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -DSMOKE_DIR=${ASSET_COOK_MESH_SMOKE_DIR}
        -DDAG_SMOKE_EXE=$<TARGET_FILE:AssetCookMeshSimplifySmoke>
        -P "${CMAKE_CURRENT_SOURCE_DIR}/AssetCookMeshSmoke.cmake.in"
)

add_custom_target(AssetCookAudioSmoke DEPENDS AssetCook)
set(ASSET_COOK_AUDIO_SMOKE_DIR "${CMAKE_BINARY_DIR}/AssetCookSmoke/$<CONFIG>/Audio")
add_test(NAME AssetCookAudioSmoke
    COMMAND ${CMAKE_COMMAND}
        -DASSET_COOK_EXE=$<TARGET_FILE:AssetCook>
        -DSMOKE_DIR=${ASSET_COOK_AUDIO_SMOKE_DIR}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/AssetCookAudioSmoke.cmake.in"
)

set(ASSET_COOK_GLB_SMOKE_DIR "${CMAKE_BINARY_DIR}/AssetCookSmoke/$<CONFIG>/Glb")
add_test(NAME AssetCookGlbSmoke
    COMMAND ${CMAKE_COMMAND}
        -DASSET_COOK_EXE=$<TARGET_FILE:AssetCook>
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -DSMOKE_DIR=${ASSET_COOK_GLB_SMOKE_DIR}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/AssetCookGlbSmoke.cmake.in"
)

add_test(NAME AssetCookImportSmoke
    COMMAND ${CMAKE_COMMAND}
        -DASSET_COOK_EXE=$<TARGET_FILE:AssetCook>
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -DSMOKE_DIR=${CMAKE_BINARY_DIR}/AssetCookSmoke/$<CONFIG>/Import
        -P "${CMAKE_CURRENT_SOURCE_DIR}/AssetCookImportSmoke.cmake.in"
)

add_test(NAME AssetCookInspectSmoke
    COMMAND ${CMAKE_COMMAND}
        -DASSET_COOK_EXE=$<TARGET_FILE:AssetCook>
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -DSMOKE_DIR=${CMAKE_BINARY_DIR}/AssetCookSmoke/$<CONFIG>/Inspect
        -P "${CMAKE_CURRENT_SOURCE_DIR}/AssetCookInspectSmoke.cmake.in"
)

# CLIと直接リンクする試験で同じcooker実装を共有する。
add_library(AssetCookLib STATIC
    CookStateFile.cpp
    CookStateFile.h
    CookStateFileTestAccess.h
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
    TextureCooker.cpp
    TextureCooker.h
)

target_link_libraries(AssetCookLib PUBLIC Core)
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
        -DSMOKE_DIR=${ASSET_COOK_TEXTURE_SMOKE_DIR}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/AssetCookTextureSmoke.cmake.in"
)

set(ASSET_COOK_MESH_SMOKE_DIR "${CMAKE_BINARY_DIR}/AssetCookSmoke/$<CONFIG>/Mesh")
add_test(NAME AssetCookMeshSmoke
    COMMAND ${CMAKE_COMMAND}
        -DASSET_COOK_EXE=$<TARGET_FILE:AssetCook>
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -DSMOKE_DIR=${ASSET_COOK_MESH_SMOKE_DIR}
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

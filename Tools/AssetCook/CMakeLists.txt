add_executable(AssetCook
    AudioCooker.cpp
    AudioCooker.h
    Main.cpp
    MeshCooker.cpp
    MeshCooker.h
)

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

target_link_libraries(AssetCook PRIVATE Core AssetCookBlockCompressor AssetCookTextureCooker AssetCookMeshOptimizer)
target_include_directories(AssetCook PRIVATE
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
    ${CMAKE_SOURCE_DIR}/Library/ThirdParty/stb
)

set_target_properties(AssetCook PROPERTIES FOLDER "Tools")

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

# 起動画面の材質などを差分クックして build/CookedAssets/ へ書く(ALL に含めない)。
# 差分の判定(元画像・一覧の項目・AssetCook の実行ファイル)は Scripts/CookAssets.ps1 が項目ごとの印で行い、
# 変わらない項目は焼き直さない。AssetCook が変わったら全項目を焼き直す。
add_custom_target(CookAssets
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/Scripts/CookAssets.ps1"
        -AssetCookExe "$<TARGET_FILE:AssetCook>"
        -SpecDir "${CMAKE_SOURCE_DIR}/Assets/AssetSets"
        -RuntimeRoot "${CMAKE_BINARY_DIR}/CookedAssets"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "AssetSets の一覧を build/CookedAssets へ差分クックする"
    VERBATIM
)
add_dependencies(CookAssets AssetCook)
set_target_properties(CookAssets PROPERTIES FOLDER "Tools")

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

target_link_libraries(AssetCook PRIVATE Core AssetCookBlockCompressor AssetCookTextureCooker)
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

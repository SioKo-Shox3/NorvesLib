add_executable(ImpostorBaker Main.cpp ImpostorBakerMeshInput.cpp ImpostorBakerMeshInput.h
    ImpostorBakerOutput.cpp ImpostorBakerOutput.h)
target_link_libraries(ImpostorBaker PRIVATE Core AssetCookLib AssetCookTextureCooker)
set_target_properties(ImpostorBaker PROPERTIES FOLDER "Tools")

# AssetCook と同じく、C++23・MSVC の設定は上位の設定を引き継ぐ。
target_include_directories(ImpostorBaker PRIVATE
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
    ${CMAKE_SOURCE_DIR}/Library/ThirdParty/stb
)
target_compile_definitions(ImpostorBaker PRIVATE
    NORVES_SHADER_DIR="${CMAKE_SOURCE_DIR}/Assets/Shaders"
    NORVES_ASSET_DIR="${CMAKE_SOURCE_DIR}/Assets"
)

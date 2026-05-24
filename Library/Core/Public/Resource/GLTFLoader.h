#pragma once

#include "Container/Containers.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include <cstdint>

namespace NorvesLib::Core::Resource
{
    using namespace NorvesLib::Core::Container;
    using NorvesLib::Core::Rendering::Mesh3DVertex;

    /**
     * @brief glTFテクスチャ種別
     */
    enum class GLTFTextureType : uint8_t
    {
        Albedo,
        Normal,
        ARM, // AO(R), Roughness(G), Metallic(B)
    };

    /**
     * @brief glTFテクスチャ情報
     */
    struct GLTFTextureInfo
    {
        String FilePath;
        GLTFTextureType Type;
    };

    /**
     * @brief glTFロード結果
     */
    struct GLTFLoadResult
    {
        VariableArray<Mesh3DVertex> Vertices;
        VariableArray<uint32_t> Indices;
        VariableArray<GLTFTextureInfo> Textures;
        String MeshName;
        bool bDoubleSided = false;
        bool bSuccess = false;
    };

    /**
     * @brief 軽量glTFローダー
     *
     * glTF 2.0（JSON + 外部.bin）形式の単一メッシュロードに特化。
     * POSITION, NORMAL, TEXCOORD_0属性をMesh3DVertexにインターリーブします。
     */
    class GLTFLoader
    {
    public:
        /**
         * @brief glTFファイルをロード
         * @param gltfPath glTFファイルへのパス
         * @return ロード結果
         */
        static GLTFLoadResult Load(const String& gltfPath);
    };

} // namespace NorvesLib::Core::Resource

#include "Resource/GLTFAnalyzer.h"

#include "FileStream/FileStream.h"
#include "Logging/LogMacros.h"
#include "Rendering/MegaGeometry/MeshClusterizer.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Rendering/RenderResourceManager.h"
#include "Text/JsonDocument.h"

#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace NorvesLib::Core::Resource
{
    using namespace NorvesLib::Core::Container;

    namespace
    {
        constexpr uint32_t GLTF_FLOAT_COMPONENT = 5126;
        constexpr uint32_t GLTF_UINT16_COMPONENT = 5123;
        constexpr uint32_t GLTF_UINT32_COMPONENT = 5125;
        constexpr uint32_t INVALID_GLTF_INDEX = UINT32_MAX;

        struct AccessorInfo
        {
            uint32_t BufferView = 0;
            size_t ByteOffset = 0;
            uint32_t ComponentType = 0;
            uint32_t Count = 0;
            String Type;
        };

        struct BufferViewInfo
        {
            uint32_t Buffer = 0;
            size_t ByteLength = 0;
            size_t ByteOffset = 0;
            size_t ByteStride = 0;
        };

        struct BufferInfo
        {
            String Uri;
            size_t ByteLength = 0;
        };

        struct PrimitiveInfo
        {
            uint32_t PositionAccessor = INVALID_GLTF_INDEX;
            uint32_t NormalAccessor = INVALID_GLTF_INDEX;
            uint32_t TexCoordAccessor = INVALID_GLTF_INDEX;
            uint32_t IndexAccessor = INVALID_GLTF_INDEX;
            uint32_t MaterialIndex = INVALID_GLTF_INDEX;
            String MeshName;
        };

        struct MaterialTextureInfo
        {
            String AlbedoPath;
            String NormalPath;
            String ArmPath;
            bool bDoubleSided = false;
        };

        String ResolveAssetPath(const String& path)
        {
            String resolvedPath = path;
#ifdef NORVES_ASSET_DIR
            if (!path.empty() &&
                path[0] != '/' && path[0] != '\\' &&
                (path.size() < 2 || path[1] != ':'))
            {
                String relativePath = path;
                if (relativePath.size() > 7)
                {
                    String prefix = relativePath.substr(0, 7);
                    if (prefix == "Assets/" || prefix == "Assets\\")
                    {
                        relativePath = relativePath.substr(7);
                    }
                }
                resolvedPath = String(NORVES_ASSET_DIR) + "/" + relativePath;
            }
#endif
            return resolvedPath;
        }

        String NormalizePath(const std::filesystem::path& path)
        {
            return String(path.lexically_normal().string().c_str());
        }

        bool IsDataUri(const String& uri)
        {
            return uri.size() >= 5 && uri.substr(0, 5) == "data:";
        }

        bool ReadTextFile(const String& path, String& outContent)
        {
            auto fileStream = NorvesLib::FileStream::FileStream::Create(
                path,
                NorvesLib::FileStream::FileMode::Read,
                NorvesLib::FileStream::FileAccess::Read,
                NorvesLib::FileStream::FileShare::Read);
            if (!fileStream || !fileStream->IsOpen())
            {
                return false;
            }

            outContent = fileStream->ReadString();
            fileStream->Close();
            return true;
        }

        bool ReadBinaryFile(const String& path, VariableArray<uint8_t>& outData)
        {
            auto fileStream = NorvesLib::FileStream::FileStream::Create(
                path,
                NorvesLib::FileStream::FileMode::Read,
                NorvesLib::FileStream::FileAccess::Read,
                NorvesLib::FileStream::FileShare::Read);
            if (!fileStream || !fileStream->IsOpen())
            {
                return false;
            }

            int64_t fileSize = fileStream->GetSize();
            if (fileSize < 0)
            {
                fileStream->Close();
                return false;
            }

            outData.resize(static_cast<size_t>(fileSize));
            if (fileSize == 0)
            {
                fileStream->Close();
                return true;
            }

            size_t bytesRead = fileStream->Read(outData.data(), outData.size());
            fileStream->Close();
            return bytesRead == outData.size();
        }

        size_t GetComponentSize(uint32_t componentType)
        {
            switch (componentType)
            {
            case GLTF_FLOAT_COMPONENT:
            case GLTF_UINT32_COMPONENT:
                return 4;
            case GLTF_UINT16_COMPONENT:
                return 2;
            default:
                return 0;
            }
        }

        size_t GetComponentCount(const String& type)
        {
            if (type == "SCALAR")
            {
                return 1;
            }
            if (type == "VEC2")
            {
                return 2;
            }
            if (type == "VEC3")
            {
                return 3;
            }
            if (type == "VEC4")
            {
                return 4;
            }
            return 0;
        }

        size_t GetAccessorStride(const AccessorInfo& accessor, const BufferViewInfo& bufferView)
        {
            if (bufferView.ByteStride != 0)
            {
                return bufferView.ByteStride;
            }

            return GetComponentSize(accessor.ComponentType) * GetComponentCount(accessor.Type);
        }

        bool ValidateAccessorBounds(const AccessorInfo& accessor,
                                    const BufferViewInfo& bufferView,
                                    const VariableArray<uint8_t>& bufferData,
                                    const char* label)
        {
            size_t elementSize = GetComponentSize(accessor.ComponentType) * GetComponentCount(accessor.Type);
            size_t stride = GetAccessorStride(accessor, bufferView);
            size_t startOffset = bufferView.ByteOffset + accessor.ByteOffset;

            if (elementSize == 0 || stride < elementSize)
            {
                NORVES_LOG_ERROR("GLTFAnalyzer", "Invalid accessor layout: %s", label);
                return false;
            }

            if (accessor.Count == 0)
            {
                return true;
            }

            size_t requiredSize = startOffset + (static_cast<size_t>(accessor.Count) - 1) * stride + elementSize;
            if (requiredSize > bufferData.size())
            {
                NORVES_LOG_ERROR("GLTFAnalyzer", "Accessor range exceeds buffer: %s", label);
                return false;
            }

            return true;
        }

        float ReadFloatValue(const uint8_t* pData)
        {
            float value = 0.0f;
            std::memcpy(&value, pData, sizeof(float));
            return value;
        }

        uint16_t ReadUInt16Value(const uint8_t* pData)
        {
            uint16_t value = 0;
            std::memcpy(&value, pData, sizeof(uint16_t));
            return value;
        }

        uint32_t ReadUInt32Value(const uint8_t* pData)
        {
            uint32_t value = 0;
            std::memcpy(&value, pData, sizeof(uint32_t));
            return value;
        }

        bool ParseAccessors(const JsonValue& root, VariableArray<AccessorInfo>& outAccessors)
        {
            JsonValue accessorsValue = root.FindMember("accessors");
            if (!accessorsValue.IsArray())
            {
                NORVES_LOG_ERROR("GLTFAnalyzer", "Missing accessors array");
                return false;
            }

            outAccessors.clear();
            outAccessors.reserve(accessorsValue.GetArraySize());

            for (size_t index = 0; index < accessorsValue.GetArraySize(); ++index)
            {
                JsonValue accessorValue = accessorsValue.GetArrayElement(index);
                if (!accessorValue.IsObject())
                {
                    NORVES_LOG_ERROR("GLTFAnalyzer", "accessors[%zu] is not an object", index);
                    return false;
                }

                AccessorInfo accessor;
                accessor.BufferView = accessorValue.FindMember("bufferView").AsUInt32();
                accessor.ByteOffset = static_cast<size_t>(accessorValue.FindMember("byteOffset").AsUInt32(0));
                accessor.ComponentType = accessorValue.FindMember("componentType").AsUInt32();
                accessor.Count = accessorValue.FindMember("count").AsUInt32();
                accessor.Type = accessorValue.FindMember("type").AsString();
                outAccessors.push_back(std::move(accessor));
            }

            return true;
        }

        bool ParseBufferViews(const JsonValue& root, VariableArray<BufferViewInfo>& outBufferViews)
        {
            JsonValue bufferViewsValue = root.FindMember("bufferViews");
            if (!bufferViewsValue.IsArray())
            {
                NORVES_LOG_ERROR("GLTFAnalyzer", "Missing bufferViews array");
                return false;
            }

            outBufferViews.clear();
            outBufferViews.reserve(bufferViewsValue.GetArraySize());

            for (size_t index = 0; index < bufferViewsValue.GetArraySize(); ++index)
            {
                JsonValue bufferViewValue = bufferViewsValue.GetArrayElement(index);
                if (!bufferViewValue.IsObject())
                {
                    NORVES_LOG_ERROR("GLTFAnalyzer", "bufferViews[%zu] is not an object", index);
                    return false;
                }

                BufferViewInfo bufferView;
                bufferView.Buffer = bufferViewValue.FindMember("buffer").AsUInt32();
                bufferView.ByteLength = static_cast<size_t>(bufferViewValue.FindMember("byteLength").AsUInt32());
                bufferView.ByteOffset = static_cast<size_t>(bufferViewValue.FindMember("byteOffset").AsUInt32(0));
                bufferView.ByteStride = static_cast<size_t>(bufferViewValue.FindMember("byteStride").AsUInt32(0));
                outBufferViews.push_back(std::move(bufferView));
            }

            return true;
        }

        bool ParseBuffers(const JsonValue& root, VariableArray<BufferInfo>& outBuffers)
        {
            JsonValue buffersValue = root.FindMember("buffers");
            if (!buffersValue.IsArray())
            {
                NORVES_LOG_ERROR("GLTFAnalyzer", "Missing buffers array");
                return false;
            }

            outBuffers.clear();
            outBuffers.reserve(buffersValue.GetArraySize());

            for (size_t index = 0; index < buffersValue.GetArraySize(); ++index)
            {
                JsonValue bufferValue = buffersValue.GetArrayElement(index);
                if (!bufferValue.IsObject())
                {
                    NORVES_LOG_ERROR("GLTFAnalyzer", "buffers[%zu] is not an object", index);
                    return false;
                }

                BufferInfo buffer;
                buffer.Uri = bufferValue.FindMember("uri").AsString();
                buffer.ByteLength = static_cast<size_t>(bufferValue.FindMember("byteLength").AsUInt32());
                outBuffers.push_back(std::move(buffer));
            }

            return true;
        }

        bool LoadBuffers(const VariableArray<BufferInfo>& buffers,
                         const std::filesystem::path& gltfDirectory,
                         VariableArray<VariableArray<uint8_t>>& outBufferData)
        {
            outBufferData.clear();
            outBufferData.resize(buffers.size());

            for (size_t index = 0; index < buffers.size(); ++index)
            {
                const BufferInfo& buffer = buffers[index];
                if (buffer.Uri.empty())
                {
                    NORVES_LOG_ERROR("GLTFAnalyzer", "buffers[%zu].uri is empty", index);
                    return false;
                }

                if (IsDataUri(buffer.Uri))
                {
                    NORVES_LOG_ERROR("GLTFAnalyzer", "data URI buffers are not supported");
                    return false;
                }

                String bufferPath = NormalizePath(gltfDirectory / buffer.Uri.c_str());
                if (!ReadBinaryFile(bufferPath, outBufferData[index]))
                {
                    NORVES_LOG_ERROR("GLTFAnalyzer", "Failed to read buffer file: %s", bufferPath.c_str());
                    return false;
                }

                if (outBufferData[index].size() < buffer.ByteLength)
                {
                    NORVES_LOG_ERROR("GLTFAnalyzer", "Buffer file is smaller than expected: %s", bufferPath.c_str());
                    return false;
                }
            }

            return true;
        }

        bool ParsePrimitiveInfo(const JsonValue& root, PrimitiveInfo& outPrimitiveInfo)
        {
            JsonValue meshesValue = root.FindMember("meshes");
            if (!meshesValue.IsArray() || meshesValue.GetArraySize() == 0)
            {
                NORVES_LOG_ERROR("GLTFAnalyzer", "Missing meshes array");
                return false;
            }

            JsonValue meshValue = meshesValue.GetArrayElement(0);
            JsonValue primitivesValue = meshValue.FindMember("primitives");
            if (!primitivesValue.IsArray() || primitivesValue.GetArraySize() == 0)
            {
                NORVES_LOG_ERROR("GLTFAnalyzer", "Missing primitives array");
                return false;
            }

            JsonValue primitiveValue = primitivesValue.GetArrayElement(0);
            JsonValue attributesValue = primitiveValue.FindMember("attributes");
            if (!attributesValue.IsObject())
            {
                NORVES_LOG_ERROR("GLTFAnalyzer", "Missing primitive attributes");
                return false;
            }

            outPrimitiveInfo.PositionAccessor = attributesValue.FindMember("POSITION").AsUInt32(INVALID_GLTF_INDEX);
            outPrimitiveInfo.NormalAccessor = attributesValue.FindMember("NORMAL").AsUInt32(INVALID_GLTF_INDEX);
            outPrimitiveInfo.TexCoordAccessor = attributesValue.FindMember("TEXCOORD_0").AsUInt32(INVALID_GLTF_INDEX);
            outPrimitiveInfo.IndexAccessor = primitiveValue.FindMember("indices").AsUInt32(INVALID_GLTF_INDEX);
            outPrimitiveInfo.MaterialIndex = primitiveValue.FindMember("material").AsUInt32(INVALID_GLTF_INDEX);
            outPrimitiveInfo.MeshName = meshValue.FindMember("name").AsString();

            if (outPrimitiveInfo.PositionAccessor == INVALID_GLTF_INDEX ||
                outPrimitiveInfo.NormalAccessor == INVALID_GLTF_INDEX ||
                outPrimitiveInfo.TexCoordAccessor == INVALID_GLTF_INDEX ||
                outPrimitiveInfo.IndexAccessor == INVALID_GLTF_INDEX)
            {
                NORVES_LOG_ERROR("GLTFAnalyzer", "Missing required primitive accessors");
                return false;
            }

            return true;
        }

        bool ResolveTexturePath(const VariableArray<String>& imagePaths,
                                const VariableArray<uint32_t>& textureSources,
                                uint32_t textureIndex,
                                String& outPath)
        {
            if (textureIndex >= textureSources.size())
            {
                return false;
            }

            uint32_t imageIndex = textureSources[textureIndex];
            if (imageIndex >= imagePaths.size())
            {
                return false;
            }

            outPath = imagePaths[imageIndex];
            return !outPath.empty();
        }

        bool ParseMaterialTextures(const JsonValue& root,
                                   const std::filesystem::path& gltfDirectory,
                                   uint32_t materialIndex,
                                   MaterialTextureInfo& outMaterialInfo)
        {
            outMaterialInfo = {};

            VariableArray<String> imagePaths;
            JsonValue imagesValue = root.FindMember("images");
            if (imagesValue.IsArray())
            {
                imagePaths.reserve(imagesValue.GetArraySize());
                for (size_t index = 0; index < imagesValue.GetArraySize(); ++index)
                {
                    JsonValue imageValue = imagesValue.GetArrayElement(index);
                    String uri = imageValue.FindMember("uri").AsString();
                    if (uri.empty() || IsDataUri(uri))
                    {
                        imagePaths.push_back("");
                        continue;
                    }

                    imagePaths.push_back(NormalizePath(gltfDirectory / uri.c_str()));
                }
            }

            VariableArray<uint32_t> textureSources;
            JsonValue texturesValue = root.FindMember("textures");
            if (texturesValue.IsArray())
            {
                textureSources.reserve(texturesValue.GetArraySize());
                for (size_t index = 0; index < texturesValue.GetArraySize(); ++index)
                {
                    JsonValue textureValue = texturesValue.GetArrayElement(index);
                    textureSources.push_back(textureValue.FindMember("source").AsUInt32(INVALID_GLTF_INDEX));
                }
            }

            JsonValue materialsValue = root.FindMember("materials");
            if (!materialsValue.IsArray() || materialsValue.GetArraySize() == 0)
            {
                return true;
            }

            size_t resolvedMaterialIndex = materialIndex == INVALID_GLTF_INDEX ? 0 : static_cast<size_t>(materialIndex);
            if (resolvedMaterialIndex >= materialsValue.GetArraySize())
            {
                NORVES_LOG_WARNING("GLTFAnalyzer", "Material index is out of range: %u", materialIndex);
                return true;
            }

            JsonValue materialValue = materialsValue.GetArrayElement(resolvedMaterialIndex);
            outMaterialInfo.bDoubleSided = materialValue.FindMember("doubleSided").AsBool(false);

            JsonValue normalTextureValue = materialValue.FindMember("normalTexture");
            if (normalTextureValue.IsObject())
            {
                uint32_t normalTextureIndex = normalTextureValue.FindMember("index").AsUInt32(INVALID_GLTF_INDEX);
                ResolveTexturePath(imagePaths, textureSources, normalTextureIndex, outMaterialInfo.NormalPath);
            }

            JsonValue pbrValue = materialValue.FindMember("pbrMetallicRoughness");
            if (pbrValue.IsObject())
            {
                JsonValue baseColorTextureValue = pbrValue.FindMember("baseColorTexture");
                if (baseColorTextureValue.IsObject())
                {
                    uint32_t albedoTextureIndex = baseColorTextureValue.FindMember("index").AsUInt32(INVALID_GLTF_INDEX);
                    ResolveTexturePath(imagePaths, textureSources, albedoTextureIndex, outMaterialInfo.AlbedoPath);
                }

                JsonValue armTextureValue = pbrValue.FindMember("metallicRoughnessTexture");
                if (armTextureValue.IsObject())
                {
                    uint32_t armTextureIndex = armTextureValue.FindMember("index").AsUInt32(INVALID_GLTF_INDEX);
                    ResolveTexturePath(imagePaths, textureSources, armTextureIndex, outMaterialInfo.ArmPath);
                }
            }

            return true;
        }

        bool ExtractMeshData(const VariableArray<AccessorInfo>& accessors,
                             const VariableArray<BufferViewInfo>& bufferViews,
                             const VariableArray<VariableArray<uint8_t>>& bufferData,
                             const PrimitiveInfo& primitiveInfo,
                             VariableArray<Rendering::Mesh3DVertex>& outVertices,
                             VariableArray<uint32_t>& outIndices)
        {
            if (primitiveInfo.PositionAccessor >= accessors.size() ||
                primitiveInfo.NormalAccessor >= accessors.size() ||
                primitiveInfo.TexCoordAccessor >= accessors.size() ||
                primitiveInfo.IndexAccessor >= accessors.size())
            {
                NORVES_LOG_ERROR("GLTFAnalyzer", "Accessor index is invalid");
                return false;
            }

            const AccessorInfo& positionAccessor = accessors[primitiveInfo.PositionAccessor];
            const AccessorInfo& normalAccessor = accessors[primitiveInfo.NormalAccessor];
            const AccessorInfo& texCoordAccessor = accessors[primitiveInfo.TexCoordAccessor];
            const AccessorInfo& indexAccessor = accessors[primitiveInfo.IndexAccessor];

            if (positionAccessor.BufferView >= bufferViews.size() ||
                normalAccessor.BufferView >= bufferViews.size() ||
                texCoordAccessor.BufferView >= bufferViews.size() ||
                indexAccessor.BufferView >= bufferViews.size())
            {
                NORVES_LOG_ERROR("GLTFAnalyzer", "bufferView index is invalid");
                return false;
            }

            const BufferViewInfo& positionBufferView = bufferViews[positionAccessor.BufferView];
            const BufferViewInfo& normalBufferView = bufferViews[normalAccessor.BufferView];
            const BufferViewInfo& texCoordBufferView = bufferViews[texCoordAccessor.BufferView];
            const BufferViewInfo& indexBufferView = bufferViews[indexAccessor.BufferView];

            if (positionBufferView.Buffer >= bufferData.size() ||
                normalBufferView.Buffer >= bufferData.size() ||
                texCoordBufferView.Buffer >= bufferData.size() ||
                indexBufferView.Buffer >= bufferData.size())
            {
                NORVES_LOG_ERROR("GLTFAnalyzer", "buffer index is invalid");
                return false;
            }

            const auto& positionBuffer = bufferData[positionBufferView.Buffer];
            const auto& normalBuffer = bufferData[normalBufferView.Buffer];
            const auto& texCoordBuffer = bufferData[texCoordBufferView.Buffer];
            const auto& indexBuffer = bufferData[indexBufferView.Buffer];

            if (positionAccessor.ComponentType != GLTF_FLOAT_COMPONENT || positionAccessor.Type != "VEC3" ||
                normalAccessor.ComponentType != GLTF_FLOAT_COMPONENT || normalAccessor.Type != "VEC3" ||
                texCoordAccessor.ComponentType != GLTF_FLOAT_COMPONENT || texCoordAccessor.Type != "VEC2" ||
                indexAccessor.Type != "SCALAR")
            {
                NORVES_LOG_ERROR("GLTFAnalyzer", "Unsupported accessor type");
                return false;
            }

            if (positionAccessor.Count != normalAccessor.Count || positionAccessor.Count != texCoordAccessor.Count)
            {
                NORVES_LOG_ERROR("GLTFAnalyzer", "Vertex attribute counts do not match");
                return false;
            }

            if (!ValidateAccessorBounds(positionAccessor, positionBufferView, positionBuffer, "POSITION") ||
                !ValidateAccessorBounds(normalAccessor, normalBufferView, normalBuffer, "NORMAL") ||
                !ValidateAccessorBounds(texCoordAccessor, texCoordBufferView, texCoordBuffer, "TEXCOORD_0") ||
                !ValidateAccessorBounds(indexAccessor, indexBufferView, indexBuffer, "indices"))
            {
                return false;
            }

            size_t positionStride = GetAccessorStride(positionAccessor, positionBufferView);
            size_t normalStride = GetAccessorStride(normalAccessor, normalBufferView);
            size_t texCoordStride = GetAccessorStride(texCoordAccessor, texCoordBufferView);
            size_t indexStride = GetAccessorStride(indexAccessor, indexBufferView);

            const uint8_t* pPositionBase = positionBuffer.data() + positionBufferView.ByteOffset + positionAccessor.ByteOffset;
            const uint8_t* pNormalBase = normalBuffer.data() + normalBufferView.ByteOffset + normalAccessor.ByteOffset;
            const uint8_t* pTexCoordBase = texCoordBuffer.data() + texCoordBufferView.ByteOffset + texCoordAccessor.ByteOffset;
            const uint8_t* pIndexBase = indexBuffer.data() + indexBufferView.ByteOffset + indexAccessor.ByteOffset;

            outVertices.resize(positionAccessor.Count);
            for (uint32_t vertexIndex = 0; vertexIndex < positionAccessor.Count; ++vertexIndex)
            {
                const uint8_t* pPosition = pPositionBase + static_cast<size_t>(vertexIndex) * positionStride;
                const uint8_t* pNormal = pNormalBase + static_cast<size_t>(vertexIndex) * normalStride;
                const uint8_t* pTexCoord = pTexCoordBase + static_cast<size_t>(vertexIndex) * texCoordStride;

                Rendering::Mesh3DVertex& vertex = outVertices[vertexIndex];
                vertex.Position[0] = ReadFloatValue(pPosition + sizeof(float) * 0);
                vertex.Position[1] = ReadFloatValue(pPosition + sizeof(float) * 1);
                vertex.Position[2] = ReadFloatValue(pPosition + sizeof(float) * 2);
                vertex.Normal[0] = ReadFloatValue(pNormal + sizeof(float) * 0);
                vertex.Normal[1] = ReadFloatValue(pNormal + sizeof(float) * 1);
                vertex.Normal[2] = ReadFloatValue(pNormal + sizeof(float) * 2);
                vertex.TexCoord[0] = ReadFloatValue(pTexCoord + sizeof(float) * 0);
                vertex.TexCoord[1] = ReadFloatValue(pTexCoord + sizeof(float) * 1);
            }

            outIndices.resize(indexAccessor.Count);
            if (indexAccessor.ComponentType == GLTF_UINT32_COMPONENT)
            {
                for (uint32_t index = 0; index < indexAccessor.Count; ++index)
                {
                    outIndices[index] = ReadUInt32Value(pIndexBase + static_cast<size_t>(index) * indexStride);
                }
            }
            else if (indexAccessor.ComponentType == GLTF_UINT16_COMPONENT)
            {
                for (uint32_t index = 0; index < indexAccessor.Count; ++index)
                {
                    outIndices[index] = static_cast<uint32_t>(
                        ReadUInt16Value(pIndexBase + static_cast<size_t>(index) * indexStride));
                }
            }
            else
            {
                NORVES_LOG_ERROR("GLTFAnalyzer", "Unsupported index component type: %u", indexAccessor.ComponentType);
                return false;
            }

            // Match the engine's clockwise front-face convention.
            for (uint32_t index = 0; index + 2 < outIndices.size(); index += 3)
            {
                std::swap(outIndices[index + 1], outIndices[index + 2]);
            }

            return true;
        }

        Rendering::BoundingSphere CalculateBoundingSphere(const VariableArray<Rendering::Mesh3DVertex>& vertices)
        {
            Rendering::BoundingBox bounds = Rendering::BoundingBox::CreateInvalid();
            for (const auto& vertex : vertices)
            {
                bounds.Expand(vertex.Position[0], vertex.Position[1], vertex.Position[2]);
            }

            Rendering::BoundingSphere sphere;
            sphere.CenterX = (bounds.MinX + bounds.MaxX) * 0.5f;
            sphere.CenterY = (bounds.MinY + bounds.MaxY) * 0.5f;
            sphere.CenterZ = (bounds.MinZ + bounds.MaxZ) * 0.5f;

            float radiusSquared = 0.0f;
            for (const auto& vertex : vertices)
            {
                float deltaX = vertex.Position[0] - sphere.CenterX;
                float deltaY = vertex.Position[1] - sphere.CenterY;
                float deltaZ = vertex.Position[2] - sphere.CenterZ;
                radiusSquared = std::max(radiusSquared, deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
            }

            sphere.Radius = std::sqrt(radiusSquared);
            return sphere;
        }

        bool DecodeImageFile(const String& filePath,
                             VariableArray<uint8_t>& outPixels,
                             uint32_t& outWidth,
                             uint32_t& outHeight)
        {
            VariableArray<uint8_t> fileData;
            if (!ReadBinaryFile(filePath, fileData) || fileData.empty())
            {
                NORVES_LOG_ERROR("GLTFAnalyzer", "Failed to read image file: %s", filePath.c_str());
                return false;
            }

            int width = 0;
            int height = 0;
            int channels = 0;
            unsigned char* pPixels = stbi_load_from_memory(
                fileData.data(),
                static_cast<int>(fileData.size()),
                &width,
                &height,
                &channels,
                4);
            if (pPixels == nullptr || width <= 0 || height <= 0)
            {
                NORVES_LOG_ERROR("GLTFAnalyzer", "Failed to decode image file: %s", filePath.c_str());
                if (pPixels != nullptr)
                {
                    stbi_image_free(pPixels);
                }
                return false;
            }

            outWidth = static_cast<uint32_t>(width);
            outHeight = static_cast<uint32_t>(height);

            size_t pixelDataSize = static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight) * 4;
            outPixels.resize(pixelDataSize);
            std::memcpy(outPixels.data(), pPixels, pixelDataSize);
            stbi_image_free(pPixels);
            return true;
        }

        bool CreateTextureFromPixels(Rendering::RenderResourceManager& resourceManager,
                                     const String& debugName,
                                     uint32_t width,
                                     uint32_t height,
                                     Rendering::TextureCreateInfo::Format format,
                                     const void* pPixelData,
                                     size_t pixelDataSize,
                                     NorvesLib::RHI::TexturePtr& outTexture)
        {
            Rendering::TextureCreateInfo createInfo;
            createInfo.Width = width;
            createInfo.Height = height;
            createInfo.PixelFormat = format;
            createInfo.DebugName = debugName;

            Rendering::TextureHandle textureHandle = resourceManager.CreateTexture(createInfo, pPixelData, pixelDataSize);
            if (!textureHandle.IsValid())
            {
                return false;
            }

            outTexture = resourceManager.GetRHITexturePtr(textureHandle);
            return static_cast<bool>(outTexture);
        }

        void LoadStandardTexture(Rendering::RenderResourceManager& resourceManager,
                                 const String& texturePath,
                                 const String& debugName,
                                 NorvesLib::RHI::TexturePtr& outTexture)
        {
            if (texturePath.empty())
            {
                return;
            }

            VariableArray<uint8_t> pixels;
            uint32_t width = 0;
            uint32_t height = 0;
            if (!DecodeImageFile(texturePath, pixels, width, height))
            {
                return;
            }

            if (!CreateTextureFromPixels(resourceManager,
                                         debugName,
                                         width,
                                         height,
                                         Rendering::TextureCreateInfo::Format::RGBA8_UNORM,
                                         pixels.data(),
                                         pixels.size(),
                                         outTexture))
            {
                NORVES_LOG_ERROR("GLTFAnalyzer", "Failed to create texture: %s", texturePath.c_str());
            }
        }

        void LoadArmTextures(Rendering::RenderResourceManager& resourceManager,
                             const String& texturePath,
                             const String& debugNamePrefix,
                             NorvesLib::RHI::TexturePtr& outAOTexture,
                             NorvesLib::RHI::TexturePtr& outRoughnessTexture,
                             NorvesLib::RHI::TexturePtr& outMetallicTexture)
        {
            if (texturePath.empty())
            {
                return;
            }

            VariableArray<uint8_t> pixels;
            uint32_t width = 0;
            uint32_t height = 0;
            if (!DecodeImageFile(texturePath, pixels, width, height))
            {
                return;
            }

            size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
            VariableArray<uint8_t> aoPixels(pixelCount);
            VariableArray<uint8_t> roughnessPixels(pixelCount);
            VariableArray<uint8_t> metallicPixels(pixelCount);

            for (size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex)
            {
                aoPixels[pixelIndex] = pixels[pixelIndex * 4 + 0];
                roughnessPixels[pixelIndex] = pixels[pixelIndex * 4 + 1];
                metallicPixels[pixelIndex] = pixels[pixelIndex * 4 + 2];
            }

            if (!CreateTextureFromPixels(resourceManager,
                                         debugNamePrefix + "_AO",
                                         width,
                                         height,
                                         Rendering::TextureCreateInfo::Format::R8_UNORM,
                                         aoPixels.data(),
                                         aoPixels.size(),
                                         outAOTexture))
            {
                NORVES_LOG_ERROR("GLTFAnalyzer", "Failed to create AO texture: %s", texturePath.c_str());
            }

            if (!CreateTextureFromPixels(resourceManager,
                                         debugNamePrefix + "_Roughness",
                                         width,
                                         height,
                                         Rendering::TextureCreateInfo::Format::R8_UNORM,
                                         roughnessPixels.data(),
                                         roughnessPixels.size(),
                                         outRoughnessTexture))
            {
                NORVES_LOG_ERROR("GLTFAnalyzer", "Failed to create roughness texture: %s", texturePath.c_str());
            }

            if (!CreateTextureFromPixels(resourceManager,
                                         debugNamePrefix + "_Metallic",
                                         width,
                                         height,
                                         Rendering::TextureCreateInfo::Format::R8_UNORM,
                                         metallicPixels.data(),
                                         metallicPixels.size(),
                                         outMetallicTexture))
            {
                NORVES_LOG_ERROR("GLTFAnalyzer", "Failed to create metallic texture: %s", texturePath.c_str());
            }
        }
    } // anonymous namespace

    Rendering::ModelHandle GLTFAnalyzer::LoadModel(const String& gltfPath,
                                                   Rendering::RenderResourceManager& resourceManager)
    {
        String resolvedGltfPath = ResolveAssetPath(gltfPath);
        String jsonContent;
        if (!ReadTextFile(resolvedGltfPath, jsonContent))
        {
            NORVES_LOG_ERROR("GLTFAnalyzer", "Failed to open glTF file: %s", resolvedGltfPath.c_str());
            return Rendering::ModelHandle::Invalid();
        }

        JsonDocument document;
        String parseError;
        if (!JsonDocument::TryParse(jsonContent, document, &parseError))
        {
            NORVES_LOG_ERROR("GLTFAnalyzer", "Failed to parse glTF JSON: %s", parseError.c_str());
            return Rendering::ModelHandle::Invalid();
        }

        JsonValue root = document.GetRoot();
        if (!root.IsObject())
        {
            NORVES_LOG_ERROR("GLTFAnalyzer", "glTF root is not an object");
            return Rendering::ModelHandle::Invalid();
        }

        std::filesystem::path gltfFilePath(resolvedGltfPath.c_str());
        std::filesystem::path gltfDirectory = gltfFilePath.parent_path();

        VariableArray<AccessorInfo> accessors;
        VariableArray<BufferViewInfo> bufferViews;
        VariableArray<BufferInfo> buffers;
        if (!ParseAccessors(root, accessors) ||
            !ParseBufferViews(root, bufferViews) ||
            !ParseBuffers(root, buffers))
        {
            return Rendering::ModelHandle::Invalid();
        }

        VariableArray<VariableArray<uint8_t>> bufferData;
        if (!LoadBuffers(buffers, gltfDirectory, bufferData))
        {
            return Rendering::ModelHandle::Invalid();
        }

        PrimitiveInfo primitiveInfo;
        if (!ParsePrimitiveInfo(root, primitiveInfo))
        {
            return Rendering::ModelHandle::Invalid();
        }

        VariableArray<Rendering::Mesh3DVertex> vertices;
        VariableArray<uint32_t> indices;
        if (!ExtractMeshData(accessors, bufferViews, bufferData, primitiveInfo, vertices, indices))
        {
            return Rendering::ModelHandle::Invalid();
        }

        VariableArray<Rendering::MegaGeometry::MeshCluster> clusters;
        VariableArray<uint32_t> clusterizedIndices;
        Rendering::MegaGeometry::MeshClusterizer::Clusterize(
            vertices.data(),
            static_cast<uint32_t>(vertices.size()),
            sizeof(Rendering::Mesh3DVertex),
            indices.data(),
            static_cast<uint32_t>(indices.size()),
            clusters,
            clusterizedIndices);

        if (clusters.empty() || clusterizedIndices.empty())
        {
            NORVES_LOG_ERROR("GLTFAnalyzer", "MeshClusterizer returned an empty result");
            return Rendering::ModelHandle::Invalid();
        }

        String debugName = primitiveInfo.MeshName;
        if (debugName.empty())
        {
            debugName = String(gltfFilePath.stem().string().c_str());
        }

        MaterialTextureInfo materialInfo;
        ParseMaterialTextures(root, gltfDirectory, primitiveInfo.MaterialIndex, materialInfo);

        Rendering::MegaGeometry::MegaMeshMaterial material;
        material.BaseColor[0] = 1.0f;
        material.BaseColor[1] = 1.0f;
        material.BaseColor[2] = 1.0f;
        material.BaseColor[3] = 1.0f;

        LoadStandardTexture(resourceManager, materialInfo.AlbedoPath, debugName + "_Albedo", material.AlbedoTexture);
        LoadStandardTexture(resourceManager, materialInfo.NormalPath, debugName + "_Normal", material.NormalTexture);
        LoadArmTextures(resourceManager,
                        materialInfo.ArmPath,
                        debugName,
                        material.AOTexture,
                        material.RoughnessTexture,
                        material.MetallicTexture);

        Rendering::MegaGeometry::MegaMeshCreateInfo createInfo;
        createInfo.VertexData = vertices.data();
        createInfo.VertexDataSize = vertices.size() * sizeof(Rendering::Mesh3DVertex);
        createInfo.VertexCount = static_cast<uint32_t>(vertices.size());
        createInfo.VertexStride = sizeof(Rendering::Mesh3DVertex);
        createInfo.IndexData = clusterizedIndices.data();
        createInfo.IndexCount = static_cast<uint32_t>(clusterizedIndices.size());
        createInfo.Clusters = clusters;
        createInfo.TotalBounds = CalculateBoundingSphere(vertices);
        createInfo.bBuildLODHierarchy = false;
        createInfo.Material = material;
        createInfo.DebugName = debugName;

        Rendering::MegaGeometry::MegaMeshHandle megaMeshHandle = resourceManager.CreateMegaMesh(createInfo);
        if (!megaMeshHandle.IsValid())
        {
            NORVES_LOG_ERROR("GLTFAnalyzer", "Failed to create MegaMesh: %s", debugName.c_str());
            return Rendering::ModelHandle::Invalid();
        }

        Rendering::ModelHandle modelHandle = resourceManager.RegisterModel(
            megaMeshHandle,
            debugName,
            resolvedGltfPath);
        if (!modelHandle.IsValid())
        {
            resourceManager.ReleaseMegaMesh(megaMeshHandle);
            NORVES_LOG_ERROR("GLTFAnalyzer", "Failed to register model: %s", debugName.c_str());
            return Rendering::ModelHandle::Invalid();
        }

        NORVES_LOG_INFO("GLTFAnalyzer", "glTF model loaded: %s", debugName.c_str());
        return modelHandle;
    }

} // namespace NorvesLib::Core::Resource

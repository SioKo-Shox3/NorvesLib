#include "Resource/GLTFAnalyzer.h"
#include "Resource/ModelStaging.h"
#include "Resource/SkeletalGltfDecode.h"

#include "FileStream/FileStream.h"
#include "Logging/LogMacros.h"
#include "Rendering/MegaGeometry/MeshClusterizer.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Text/JsonDocument.h"
#include "Thread/Atomic.h"
#include "Thread/JobSystem.h"
#include "Thread/Mutex.h"
#include "Thread/Task.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <utility>

namespace NorvesLib::Core::Resource
{
    using namespace NorvesLib::Core::Container;
    using namespace NorvesLib::Core::Resource::ModelStaging;

    namespace
    {
        constexpr uint32_t GLTF_FLOAT_COMPONENT = 5126;
        constexpr uint32_t GLTF_UINT16_COMPONENT = 5123;
        constexpr uint32_t GLTF_UINT32_COMPONENT = 5125;
        constexpr uint32_t INVALID_GLTF_INDEX = UINT32_MAX;

        using LoadProfileClock = std::chrono::steady_clock;

        LoadProfileClock::time_point LoadProfileNow()
        {
            return LoadProfileClock::now();
        }

        double LoadProfileElapsedMs(LoadProfileClock::time_point startTime)
        {
            return std::chrono::duration<double, std::milli>(LoadProfileClock::now() - startTime).count();
        }

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

        struct AsyncModelLoadResult
        {
            ModelStagingData Staging;
            bool bSuccess = false;
        };

        struct AsyncModelLoadRequest
        {
            uint32_t RequestId = 0;
            String Path;
            String ResolvedPath;
            Thread::TaskPtr Task;
            AsyncModelLoadResult Result;
            VariableArray<NorvesLib::Core::Delegate<void, Rendering::ModelHandle>> Callbacks;
            // Set on the game thread by CancelModelLoad, read on the main/flush thread.
            // Atomic because flush reads it outside g_AsyncModelLoadMutex during finalization.
            Thread::Atomic<bool> Cancelled{false};
        };

        Thread::Mutex g_AsyncModelLoadMutex;
        VariableArray<TSharedPtr<AsyncModelLoadRequest>> g_PendingModelLoads;
        Map<String, TSharedPtr<AsyncModelLoadRequest>> g_PendingModelLoadsByPath;
        Thread::Atomic<uint32_t> g_NextAsyncModelLoadRequestId{1};
        bool g_bAsyncModelLoadAdmissionOpen = true;
        thread_local uint32_t GAsyncModelLoadCallbackDepth = 0;

        class CallbackContextGuard
        {
        public:
            CallbackContextGuard()
            {
                ++GAsyncModelLoadCallbackDepth;
            }

            ~CallbackContextGuard()
            {
                --GAsyncModelLoadCallbackDepth;
            }
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

        String NormalizeGenericPath(const std::filesystem::path& path)
        {
            return String(path.lexically_normal().generic_string().c_str());
        }

        bool IsAbsoluteLikePath(const String& path)
        {
            return !path.empty() &&
                   (path[0] == '/' ||
                    path[0] == '\\' ||
                    (path.size() >= 2 && path[1] == ':'));
        }

        bool IsDataUri(const String& uri)
        {
            return uri.size() >= 5 && uri.substr(0, 5) == "data:";
        }

        TextureReference BuildTextureReference(const String& gltfRequestPath,
                                               const std::filesystem::path& resolvedGltfDirectory,
                                               const String& uri)
        {
            TextureReference reference;
            if (uri.empty() || IsDataUri(uri))
            {
                return reference;
            }

            reference.ResolvedFallbackPath = NormalizePath(resolvedGltfDirectory / uri.c_str());
            if (IsAbsoluteLikePath(gltfRequestPath) || IsAbsoluteLikePath(uri))
            {
                return reference;
            }

            std::filesystem::path requestPath(gltfRequestPath.c_str());
            std::filesystem::path requestDirectory = requestPath.parent_path();
            if (requestDirectory.empty())
            {
                return reference;
            }

            reference.RequestPath = NormalizeGenericPath(requestDirectory / uri.c_str());
            return reference;
        }

        bool ReadTextFile(const String& path,
                          String& outContent,
                          const char* role,
                          uint32_t requestId,
                          const char* stage)
        {
            auto readStartTime = LoadProfileNow();
            auto fileStream = NorvesLib::FileStream::FileStream::Create(
                path,
                NorvesLib::FileStream::FileMode::Read,
                NorvesLib::FileStream::FileAccess::Read,
                NorvesLib::FileStream::FileShare::Read);
            if (!fileStream || !fileStream->IsOpen())
            {
                NORVES_LOG_INFO("AssetLoadProfile",
                                "stage=%s role=%s request_id=%u path=\"%s\" bytes=0 ms=%.3f success=0",
                                stage,
                                role,
                                static_cast<unsigned int>(requestId),
                                path.c_str(),
                                LoadProfileElapsedMs(readStartTime));
                return false;
            }

            outContent = fileStream->ReadString();
            fileStream->Close();
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=%s role=%s request_id=%u path=\"%s\" bytes=%zu ms=%.3f success=1",
                            stage,
                            role,
                            static_cast<unsigned int>(requestId),
                            path.c_str(),
                            outContent.size(),
                            LoadProfileElapsedMs(readStartTime));
            return true;
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
                         VariableArray<VariableArray<uint8_t>>& outBufferData,
                         const char* role,
                         uint32_t requestId)
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
                if (!ModelStaging::ReadBinaryFile(bufferPath, outBufferData[index], role, requestId, "gltf_buffer_read"))
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

        bool ResolveTextureReference(const VariableArray<TextureReference>& imageReferences,
                                     const VariableArray<uint32_t>& textureSources,
                                     uint32_t textureIndex,
                                     TextureReference& outReference)
        {
            if (textureIndex >= textureSources.size())
            {
                return false;
            }

            uint32_t imageIndex = textureSources[textureIndex];
            if (imageIndex >= imageReferences.size())
            {
                return false;
            }

            outReference = imageReferences[imageIndex];
            return outReference.HasReference();
        }

        bool ParseMaterialTextures(const JsonValue& root,
                                   const String& gltfRequestPath,
                                   const std::filesystem::path& gltfDirectory,
                                   uint32_t materialIndex,
                                   MaterialTextureInfo& outMaterialInfo)
        {
            outMaterialInfo = {};

            VariableArray<TextureReference> imageReferences;
            JsonValue imagesValue = root.FindMember("images");
            if (imagesValue.IsArray())
            {
                imageReferences.reserve(imagesValue.GetArraySize());
                for (size_t index = 0; index < imagesValue.GetArraySize(); ++index)
                {
                    JsonValue imageValue = imagesValue.GetArrayElement(index);
                    String uri = imageValue.FindMember("uri").AsString();
                    if (uri.empty() || IsDataUri(uri))
                    {
                        imageReferences.push_back({});
                        continue;
                    }

                    imageReferences.push_back(BuildTextureReference(gltfRequestPath, gltfDirectory, uri));
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
                ResolveTextureReference(imageReferences, textureSources, normalTextureIndex, outMaterialInfo.Normal);
            }

            JsonValue pbrValue = materialValue.FindMember("pbrMetallicRoughness");
            if (pbrValue.IsObject())
            {
                JsonValue baseColorTextureValue = pbrValue.FindMember("baseColorTexture");
                if (baseColorTextureValue.IsObject())
                {
                    uint32_t albedoTextureIndex = baseColorTextureValue.FindMember("index").AsUInt32(INVALID_GLTF_INDEX);
                    ResolveTextureReference(imageReferences, textureSources, albedoTextureIndex, outMaterialInfo.Albedo);
                }

                JsonValue armTextureValue = pbrValue.FindMember("metallicRoughnessTexture");
                if (armTextureValue.IsObject())
                {
                    uint32_t armTextureIndex = armTextureValue.FindMember("index").AsUInt32(INVALID_GLTF_INDEX);
                    ResolveTextureReference(imageReferences, textureSources, armTextureIndex, outMaterialInfo.Arm);
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

        bool BuildModelStaging(const String& gltfRequestPath,
                               const String& resolvedGltfPath,
                               ModelStagingData& outStaging,
                               const char* role,
                               uint32_t requestId)
        {
            auto totalStartTime = LoadProfileNow();
            String jsonContent;
            if (!ReadTextFile(resolvedGltfPath, jsonContent, role, requestId, "gltf_text_read"))
            {
                NORVES_LOG_ERROR("GLTFAnalyzer", "Failed to open glTF file: %s", resolvedGltfPath.c_str());
                return false;
            }

            JsonDocument document;
            String parseError;
            auto jsonParseStartTime = LoadProfileNow();
            if (!JsonDocument::TryParse(jsonContent, document, &parseError))
            {
                NORVES_LOG_INFO("AssetLoadProfile",
                                "stage=gltf_json_parse role=%s request_id=%u path=\"%s\" json_bytes=%zu ms=%.3f success=0",
                                role,
                                static_cast<unsigned int>(requestId),
                                resolvedGltfPath.c_str(),
                                jsonContent.size(),
                                LoadProfileElapsedMs(jsonParseStartTime));
                NORVES_LOG_ERROR("GLTFAnalyzer", "Failed to parse glTF JSON: %s", parseError.c_str());
                return false;
            }
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=gltf_json_parse role=%s request_id=%u path=\"%s\" json_bytes=%zu ms=%.3f success=1",
                            role,
                            static_cast<unsigned int>(requestId),
                            resolvedGltfPath.c_str(),
                            jsonContent.size(),
                            LoadProfileElapsedMs(jsonParseStartTime));

            JsonValue root = document.GetRoot();
            if (!root.IsObject())
            {
                NORVES_LOG_ERROR("GLTFAnalyzer", "glTF root is not an object");
                return false;
            }

            std::filesystem::path gltfFilePath(resolvedGltfPath.c_str());
            std::filesystem::path gltfDirectory = gltfFilePath.parent_path();

            VariableArray<AccessorInfo> accessors;
            VariableArray<BufferViewInfo> bufferViews;
            VariableArray<BufferInfo> buffers;
            auto metadataParseStartTime = LoadProfileNow();
            if (!ParseAccessors(root, accessors) ||
                !ParseBufferViews(root, bufferViews) ||
                !ParseBuffers(root, buffers))
            {
                NORVES_LOG_INFO("AssetLoadProfile",
                                "stage=gltf_buffer_metadata_parse role=%s request_id=%u path=\"%s\" accessors=%zu buffer_views=%zu buffers=%zu ms=%.3f success=0",
                                role,
                                static_cast<unsigned int>(requestId),
                                resolvedGltfPath.c_str(),
                                accessors.size(),
                                bufferViews.size(),
                                buffers.size(),
                                LoadProfileElapsedMs(metadataParseStartTime));
                return false;
            }
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=gltf_buffer_metadata_parse role=%s request_id=%u path=\"%s\" accessors=%zu buffer_views=%zu buffers=%zu ms=%.3f success=1",
                            role,
                            static_cast<unsigned int>(requestId),
                            resolvedGltfPath.c_str(),
                            accessors.size(),
                            bufferViews.size(),
                            buffers.size(),
                            LoadProfileElapsedMs(metadataParseStartTime));

            VariableArray<VariableArray<uint8_t>> bufferData;
            auto bufferReadTotalStartTime = LoadProfileNow();
            if (!LoadBuffers(buffers, gltfDirectory, bufferData, role, requestId))
            {
                size_t bufferBytes = 0;
                for (const auto& buffer : bufferData)
                {
                    bufferBytes += buffer.size();
                }
                NORVES_LOG_INFO("AssetLoadProfile",
                                "stage=gltf_buffer_read_total role=%s request_id=%u path=\"%s\" buffers=%zu bytes=%zu ms=%.3f success=0",
                                role,
                                static_cast<unsigned int>(requestId),
                                resolvedGltfPath.c_str(),
                                buffers.size(),
                                bufferBytes,
                                LoadProfileElapsedMs(bufferReadTotalStartTime));
                return false;
            }
            size_t bufferBytes = 0;
            for (const auto& buffer : bufferData)
            {
                bufferBytes += buffer.size();
            }
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=gltf_buffer_read_total role=%s request_id=%u path=\"%s\" buffers=%zu bytes=%zu ms=%.3f success=1",
                            role,
                            static_cast<unsigned int>(requestId),
                            resolvedGltfPath.c_str(),
                            buffers.size(),
                            bufferBytes,
                            LoadProfileElapsedMs(bufferReadTotalStartTime));

            PrimitiveInfo primitiveInfo;
            auto primitiveParseStartTime = LoadProfileNow();
            if (!ParsePrimitiveInfo(root, primitiveInfo))
            {
                NORVES_LOG_INFO("AssetLoadProfile",
                                "stage=gltf_primitive_parse role=%s request_id=%u path=\"%s\" ms=%.3f success=0",
                                role,
                                static_cast<unsigned int>(requestId),
                                resolvedGltfPath.c_str(),
                                LoadProfileElapsedMs(primitiveParseStartTime));
                return false;
            }
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=gltf_primitive_parse role=%s request_id=%u path=\"%s\" mesh=\"%s\" material_index=%u ms=%.3f success=1",
                            role,
                            static_cast<unsigned int>(requestId),
                            resolvedGltfPath.c_str(),
                            primitiveInfo.MeshName.c_str(),
                            static_cast<unsigned int>(primitiveInfo.MaterialIndex),
                            LoadProfileElapsedMs(primitiveParseStartTime));

            VariableArray<Rendering::Mesh3DVertex> vertices;
            VariableArray<uint32_t> indices;
            auto meshExtractStartTime = LoadProfileNow();
            if (!ExtractMeshData(accessors, bufferViews, bufferData, primitiveInfo, vertices, indices))
            {
                NORVES_LOG_INFO("AssetLoadProfile",
                                "stage=gltf_mesh_extract role=%s request_id=%u path=\"%s\" vertices=%zu indices=%zu ms=%.3f success=0",
                                role,
                                static_cast<unsigned int>(requestId),
                                resolvedGltfPath.c_str(),
                                vertices.size(),
                                indices.size(),
                                LoadProfileElapsedMs(meshExtractStartTime));
                return false;
            }
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=gltf_mesh_extract role=%s request_id=%u path=\"%s\" vertices=%zu indices=%zu vertex_bytes=%zu index_bytes=%zu ms=%.3f success=1",
                            role,
                            static_cast<unsigned int>(requestId),
                            resolvedGltfPath.c_str(),
                            vertices.size(),
                            indices.size(),
                            vertices.size() * sizeof(Rendering::Mesh3DVertex),
                            indices.size() * sizeof(uint32_t),
                            LoadProfileElapsedMs(meshExtractStartTime));

            VariableArray<Rendering::MegaGeometry::MeshCluster> clusters;
            VariableArray<uint32_t> clusterizedIndices;
            auto clusterizeStartTime = LoadProfileNow();
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
                NORVES_LOG_INFO("AssetLoadProfile",
                                "stage=gltf_clusterize role=%s request_id=%u path=\"%s\" clusters=%zu clusterized_indices=%zu ms=%.3f success=0",
                                role,
                                static_cast<unsigned int>(requestId),
                                resolvedGltfPath.c_str(),
                                clusters.size(),
                                clusterizedIndices.size(),
                                LoadProfileElapsedMs(clusterizeStartTime));
                NORVES_LOG_ERROR("GLTFAnalyzer", "MeshClusterizer returned an empty result");
                return false;
            }
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=gltf_clusterize role=%s request_id=%u path=\"%s\" clusters=%zu clusterized_indices=%zu cluster_bytes=%zu index_bytes=%zu ms=%.3f success=1",
                            role,
                            static_cast<unsigned int>(requestId),
                            resolvedGltfPath.c_str(),
                            clusters.size(),
                            clusterizedIndices.size(),
                            clusters.size() * sizeof(Rendering::MegaGeometry::MeshCluster),
                            clusterizedIndices.size() * sizeof(uint32_t),
                            LoadProfileElapsedMs(clusterizeStartTime));

            String debugName = primitiveInfo.MeshName;
            if (debugName.empty())
            {
                debugName = String(gltfFilePath.stem().string().c_str());
            }

            MaterialTextureInfo materialInfo;
            auto materialTextureParseStartTime = LoadProfileNow();
            bool bMaterialTextureParseSuccess = ParseMaterialTextures(
                root,
                gltfRequestPath,
                gltfDirectory,
                primitiveInfo.MaterialIndex,
                materialInfo);
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=gltf_material_texture_parse role=%s request_id=%u path=\"%s\" albedo=%d normal=%d arm=%d ms=%.3f success=%d",
                            role,
                            static_cast<unsigned int>(requestId),
                            resolvedGltfPath.c_str(),
                            materialInfo.Albedo.HasReference() ? 1 : 0,
                            materialInfo.Normal.HasReference() ? 1 : 0,
                            materialInfo.Arm.HasReference() ? 1 : 0,
                            LoadProfileElapsedMs(materialTextureParseStartTime),
                            bMaterialTextureParseSuccess ? 1 : 0);

            outStaging.Vertices = std::move(vertices);
            outStaging.ClusterizedIndices = std::move(clusterizedIndices);
            outStaging.Clusters = std::move(clusters);
            outStaging.TotalBounds = CalculateBoundingSphere(outStaging.Vertices);
            outStaging.DebugName = debugName;
            outStaging.ResolvedPath = resolvedGltfPath;
            outStaging.TextureReferences = materialInfo;

            auto textureStagingStartTime = LoadProfileNow();
            bool bAlbedoStagingSuccess = StageStandardTexture(
                materialInfo.Albedo,
                debugName + "_Albedo",
                outStaging.AlbedoTexture,
                role,
                requestId);
            bool bNormalStagingSuccess = StageStandardTexture(
                materialInfo.Normal,
                debugName + "_Normal",
                outStaging.NormalTexture,
                role,
                requestId);
            bool bArmStagingSuccess = StageArmTextures(
                materialInfo.Arm,
                debugName,
                outStaging.AOTexture,
                outStaging.RoughnessTexture,
                outStaging.MetallicTexture,
                role,
                requestId);
            bool bTextureStagingSuccess =
                bAlbedoStagingSuccess &&
                bNormalStagingSuccess &&
                bArmStagingSuccess;
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=gltf_texture_staging role=%s request_id=%u path=\"%s\" textures=%u prepared_textures=%u loose_texture_bytes=%zu ms=%.3f success=%d",
                            role,
                            static_cast<unsigned int>(requestId),
                            resolvedGltfPath.c_str(),
                            static_cast<unsigned int>(GetStagedTextureCount(outStaging)),
                            static_cast<unsigned int>(GetStagedPreparedTextureCount(outStaging)),
                            GetStagedLooseTextureBytes(outStaging),
                            LoadProfileElapsedMs(textureStagingStartTime),
                            bTextureStagingSuccess ? 1 : 0);
            if (!bTextureStagingSuccess)
            {
                return false;
            }

            size_t vertexBytes = outStaging.Vertices.size() * sizeof(Rendering::Mesh3DVertex);
            size_t indexBytes = outStaging.ClusterizedIndices.size() * sizeof(uint32_t);
            size_t clusterBytes = outStaging.Clusters.size() * sizeof(Rendering::MegaGeometry::MeshCluster);
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=gltf_staging_total role=%s request_id=%u path=\"%s\" debug_name=\"%s\" vertices=%zu indices=%zu clusters=%zu vertex_bytes=%zu index_bytes=%zu cluster_bytes=%zu loose_texture_bytes=%zu prepared_textures=%u cpu_staging_bytes=%zu ms=%.3f success=1",
                            role,
                            static_cast<unsigned int>(requestId),
                            resolvedGltfPath.c_str(),
                            outStaging.DebugName.c_str(),
                            outStaging.Vertices.size(),
                            outStaging.ClusterizedIndices.size(),
                            outStaging.Clusters.size(),
                            vertexBytes,
                            indexBytes,
                            clusterBytes,
                            GetStagedLooseTextureBytes(outStaging),
                            static_cast<unsigned int>(GetStagedPreparedTextureCount(outStaging)),
                            vertexBytes + indexBytes + clusterBytes + GetStagedLooseTextureBytes(outStaging),
                            LoadProfileElapsedMs(totalStartTime));
            return true;
        }
    } // anonymous namespace

    Skeletal::SkeletalGltfDecodeResult GLTFAnalyzer::AnalyzeSkeletal(const String& gltfPath)
    {
        const String resolvedPath = ResolveAssetPath(gltfPath);
        String jsonText;
        if (!ReadTextFile(resolvedPath, jsonText, "skeletal", 0, "gltf_skeletal_json_read"))
        {
            Skeletal::SkeletalGltfDecodeResult result;
            result.Status = Skeletal::SkeletalGltfDecodeStatus::FileReadFailed;
            return result;
        }
        return Skeletal::DecodeSkeletalGltf(jsonText, resolvedPath);
    }

    Rendering::ModelHandle GLTFAnalyzer::LoadModel(const String& gltfPath,
                                                   Rendering::ModelLoadResourceContext resources)
    {
        ModelStagingData staging;
        String resolvedGltfPath = ResolveAssetPath(gltfPath);
        if (!BuildModelStaging(gltfPath, resolvedGltfPath, staging, "caller", 0))
        {
            return Rendering::ModelHandle::Invalid();
        }

        return FinalizeModelStaging(staging, resources, "caller", 0);
    }

    uint32_t GLTFAnalyzer::LoadModelAsync(const String& gltfPath,
                                          Rendering::ModelLoadResourceContext resources,
                                          Delegate<void, Rendering::ModelHandle> callback)
    {
        (void)resources;

        String resolvedGltfPath = ResolveAssetPath(gltfPath);
        {
            Thread::ScopedLock lock(g_AsyncModelLoadMutex);
            if (!g_bAsyncModelLoadAdmissionOpen)
            {
                return 0;
            }
            auto pendingIt = g_PendingModelLoadsByPath.find(resolvedGltfPath);
            if (pendingIt != g_PendingModelLoadsByPath.end() && pendingIt->second)
            {
                if (callback.IsBound())
                {
                    pendingIt->second->Callbacks.push_back(std::move(callback));
                }
                return pendingIt->second->RequestId;
            }
        }

        auto request = MakeShared<AsyncModelLoadRequest>();
        request->RequestId = g_NextAsyncModelLoadRequestId.FetchAdd(1, std::memory_order_relaxed);
        request->Path = gltfPath;
        request->ResolvedPath = resolvedGltfPath;
        if (callback.IsBound())
        {
            request->Callbacks.push_back(std::move(callback));
        }

        request->Task = Thread::Task::Create([request]()
        {
            request->Result.bSuccess = BuildModelStaging(
                request->Path,
                request->ResolvedPath,
                request->Result.Staging,
                "worker",
                request->RequestId);
        }, Thread::TaskPriority::NORMAL);

        {
            Thread::ScopedLock lock(g_AsyncModelLoadMutex);
            if (!g_bAsyncModelLoadAdmissionOpen)
            {
                return 0;
            }
            auto pendingIt = g_PendingModelLoadsByPath.find(resolvedGltfPath);
            if (pendingIt != g_PendingModelLoadsByPath.end() && pendingIt->second)
            {
                for (auto& pendingCallback : request->Callbacks)
                {
                    if (pendingCallback.IsBound())
                    {
                        pendingIt->second->Callbacks.push_back(std::move(pendingCallback));
                    }
                }
                return pendingIt->second->RequestId;
            }
            g_PendingModelLoads.push_back(request);
            g_PendingModelLoadsByPath[resolvedGltfPath] = request;
            if (!Thread::JobSystem::Get().SubmitTask(request->Task))
            {
                request->Cancelled.Store(true, std::memory_order_release);
                request->Callbacks.clear();
                auto pendingRequest = std::find(
                    g_PendingModelLoads.begin(), g_PendingModelLoads.end(), request);
                if (pendingRequest != g_PendingModelLoads.end())
                {
                    g_PendingModelLoads.erase(pendingRequest);
                }
                auto byPathIt = g_PendingModelLoadsByPath.find(resolvedGltfPath);
                if (byPathIt != g_PendingModelLoadsByPath.end() && byPathIt->second == request)
                {
                    g_PendingModelLoadsByPath.erase(byPathIt);
                }
                return 0;
            }
        }
        NORVES_LOG_INFO("GLTFAnalyzer", "Async glTF model load started: %s (RequestId=%u)",
                        resolvedGltfPath.c_str(),
                        static_cast<unsigned int>(request->RequestId));
        return request->RequestId;
    }

    uint32_t GLTFAnalyzer::FlushCompletedModelLoads(Rendering::ModelLoadResourceContext resources,
                                                    uint32_t maxLoadsPerFrame)
    {
        auto flushStartTime = LoadProfileNow();
        VariableArray<TSharedPtr<AsyncModelLoadRequest>> completedRequests;
        double detachMs = 0.0;

        {
            auto detachStartTime = LoadProfileNow();
            Thread::ScopedLock lock(g_AsyncModelLoadMutex);
            for (auto it = g_PendingModelLoads.begin(); it != g_PendingModelLoads.end();)
            {
                auto& request = *it;
                if (!request || !request->Task ||
                    (!request->Task->IsCompleted() && !request->Task->IsCanceled()))
                {
                    ++it;
                    continue;
                }

                auto byPathIt = g_PendingModelLoadsByPath.find(request->ResolvedPath);
                if (byPathIt != g_PendingModelLoadsByPath.end() && byPathIt->second == request)
                {
                    g_PendingModelLoadsByPath.erase(byPathIt);
                }
                completedRequests.push_back(request);
                it = g_PendingModelLoads.erase(it);

                if (maxLoadsPerFrame > 0 &&
                    completedRequests.size() >= static_cast<size_t>(maxLoadsPerFrame))
                {
                    break;
                }
            }
            detachMs = LoadProfileElapsedMs(detachStartTime);
        }

        uint32_t processedCount = 0;
        uint32_t successCount = 0;
        uint32_t failedCount = 0;
        for (const auto& request : completedRequests)
        {
            Rendering::ModelHandle modelHandle = Rendering::ModelHandle::Invalid();
            if (request->Cancelled.Load(std::memory_order_acquire))
            {
                // The load was cancelled after submission. Skip GPU finalization
                // entirely (avoids allocate-then-free) and do not invoke callbacks.
                // The worker only produced CPU-side staging, which is freed when the
                // request shared pointer drops here, so nothing leaks on the GPU.
                ++processedCount;
                NORVES_LOG_INFO("GLTFAnalyzer",
                                "Skipped cancelled async glTF model load: %s (RequestId=%u)",
                                request->ResolvedPath.c_str(),
                                static_cast<unsigned int>(request->RequestId));
                continue;
            }
            if (request->Result.bSuccess)
            {
                modelHandle = FinalizeModelStaging(
                    request->Result.Staging,
                    resources,
                    "main_render",
                    request->RequestId);
                if (modelHandle.IsValid())
                {
                    ++successCount;
                }
                else
                {
                    ++failedCount;
                }
            }
            else
            {
                ++failedCount;
                NORVES_LOG_ERROR("GLTFAnalyzer", "Async glTF staging failed: %s", request->ResolvedPath.c_str());
            }

            for (const auto& callback : request->Callbacks)
            {
                if (callback.IsBound())
                {
                    CallbackContextGuard callbackContext;
                    callback(modelHandle);
                }
            }
            ++processedCount;
        }

        if (processedCount > 0)
        {
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=gltf_model_flush role=main_render processed=%u success=%u failed=%u max_loads_per_frame=%u detach_ms=%.3f flush_ms=%.3f",
                            static_cast<unsigned int>(processedCount),
                            static_cast<unsigned int>(successCount),
                            static_cast<unsigned int>(failedCount),
                            static_cast<unsigned int>(maxLoadsPerFrame),
                            detachMs,
                            LoadProfileElapsedMs(flushStartTime));
        }

        return processedCount;
    }

    void GLTFAnalyzer::CancelPendingModelLoadsAndWait()
    {
        VariableArray<TSharedPtr<AsyncModelLoadRequest>> pendingRequests;
        {
            Thread::ScopedLock lock(g_AsyncModelLoadMutex);
            pendingRequests = std::move(g_PendingModelLoads);
            g_PendingModelLoads.clear();
            g_PendingModelLoadsByPath.clear();
            for (const auto& request : pendingRequests)
            {
                if (request)
                {
                    request->Callbacks.clear();
                }
            }
        }

        for (const auto& request : pendingRequests)
        {
            if (request && request->Task)
            {
                request->Task->Cancel();
                request->Task->Wait();
            }
        }

        if (!pendingRequests.empty())
        {
            NORVES_LOG_INFO("GLTFAnalyzer",
                            "Cancelled pending async glTF model loads: %zu",
                            pendingRequests.size());
        }
    }

    void GLTFAnalyzer::CancelModelLoad(uint32_t requestId)
    {
        if (requestId == 0)
        {
            return;
        }

        Thread::ScopedLock lock(g_AsyncModelLoadMutex);
        for (const auto& request : g_PendingModelLoads)
        {
            if (request && request->RequestId == requestId)
            {
                // Mark cancelled and drop callbacks so completion never fires.
                // Keep the request in g_PendingModelLoads so FlushCompletedModelLoads still
                // reaches it and skips/releases any produced model. Removing it here would
                // orphan an in-flight worker task and leak whatever it eventually produces,
                // so we intentionally leave it in g_PendingModelLoads.
                request->Cancelled.Store(true, std::memory_order_release);
                request->Callbacks.clear();

                // Remove this request from the by-path coalescing map so a subsequent
                // same-path LoadModelAsync starts a FRESH request instead of coalescing onto
                // this cancelled one (which Flush would skip, silently dropping the reload).
                // Guard the erase: only evict the entry if it still maps to THIS request, so
                // we never evict a different, newer request that already replaced it.
                auto byPathIt = g_PendingModelLoadsByPath.find(request->ResolvedPath);
                if (byPathIt != g_PendingModelLoadsByPath.end() && byPathIt->second == request)
                {
                    g_PendingModelLoadsByPath.erase(byPathIt);
                }
                NORVES_LOG_INFO("GLTFAnalyzer",
                                "Cancelled async glTF model load: %s (RequestId=%u)",
                                request->ResolvedPath.c_str(),
                                static_cast<unsigned int>(requestId));
                return;
            }
        }
        // Not found: already flushed or unknown id. No-op.
    }

    uint32_t GLTFAnalyzer::GetPendingAsyncModelLoadCount()
    {
        Thread::ScopedLock lock(g_AsyncModelLoadMutex);
        return static_cast<uint32_t>(g_PendingModelLoads.size());
    }

    void GLTFAnalyzer::CloseAsyncAssetLoadAdmissionAndWait()
    {
        assert(GAsyncModelLoadCallbackDepth == 0);

        VariableArray<TSharedPtr<AsyncModelLoadRequest>> pendingRequests;
        {
            Thread::ScopedLock lock(g_AsyncModelLoadMutex);
            g_bAsyncModelLoadAdmissionOpen = false;
            pendingRequests = std::move(g_PendingModelLoads);
            g_PendingModelLoads.clear();
            g_PendingModelLoadsByPath.clear();
            for (const auto& request : pendingRequests)
            {
                if (request)
                {
                    request->Cancelled.Store(true, std::memory_order_release);
                    request->Callbacks.clear();
                }
            }
        }

        for (const auto& request : pendingRequests)
        {
            if (request && request->Task)
            {
                request->Task->Cancel();
                request->Task->Wait();
            }
        }
    }

    void GLTFAnalyzer::ReopenAsyncAssetLoadAdmission()
    {
        Thread::ScopedLock lock(g_AsyncModelLoadMutex);
        if (g_PendingModelLoads.empty())
        {
            g_bAsyncModelLoadAdmissionOpen = true;
        }
    }

    bool GLTFAnalyzer::IsAsyncAssetLoadAdmissionOpen()
    {
        Thread::ScopedLock lock(g_AsyncModelLoadMutex);
        return g_bAsyncModelLoadAdmissionOpen;
    }

} // namespace NorvesLib::Core::Resource

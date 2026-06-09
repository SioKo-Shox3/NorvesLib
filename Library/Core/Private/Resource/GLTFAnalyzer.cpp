#include "Resource/GLTFAnalyzer.h"

#include "FileStream/FileStream.h"
#include "Logging/LogMacros.h"
#include "Rendering/MegaGeometry/MeshClusterizer.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Rendering/RenderResources.h"
#include "Rendering/TextureUploadProfile.h"
#include "Text/JsonDocument.h"
#include "Thread/Atomic.h"
#include "Thread/JobSystem.h"
#include "Thread/Mutex.h"
#include "Thread/Task.h"

#include "stb_image.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <utility>

namespace NorvesLib::Core::Resource
{
    using namespace NorvesLib::Core::Container;

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

        struct TextureReference
        {
            String RequestPath;
            String ResolvedFallbackPath;

            bool HasReference() const
            {
                return !RequestPath.empty() || !ResolvedFallbackPath.empty();
            }
        };

        struct MaterialTextureInfo
        {
            TextureReference Albedo;
            TextureReference Normal;
            TextureReference Arm;
            bool bDoubleSided = false;
        };

        struct StagedTextureData
        {
            VariableArray<uint8_t> PixelData;
            Rendering::PreparedTextureAsset PreparedTexture;
            uint32_t Width = 0;
            uint32_t Height = 0;
            Rendering::TextureCreateInfo::Format Format = Rendering::TextureCreateInfo::Format::RGBA8_UNORM;
            String DebugName;
            bool bHasPreparedTexture = false;

            bool HasLoosePixelData() const
            {
                return !PixelData.empty() && Width > 0 && Height > 0;
            }

            bool HasPreparedTexture() const
            {
                return bHasPreparedTexture && PreparedTexture.HasCookedPayload();
            }

            bool HasData() const
            {
                return HasLoosePixelData() || HasPreparedTexture();
            }
        };

        struct ModelStagingData
        {
            VariableArray<Rendering::Mesh3DVertex> Vertices;
            VariableArray<uint32_t> ClusterizedIndices;
            VariableArray<Rendering::MegaGeometry::MeshCluster> Clusters;
            Rendering::BoundingSphere TotalBounds;
            String DebugName;
            String ResolvedPath;
            MaterialTextureInfo TextureReferences;

            StagedTextureData AlbedoTexture;
            StagedTextureData NormalTexture;
            StagedTextureData AOTexture;
            StagedTextureData RoughnessTexture;
            StagedTextureData MetallicTexture;
        };

        size_t GetStagedLooseTextureBytes(const ModelStagingData& staging)
        {
            return staging.AlbedoTexture.PixelData.size() +
                   staging.NormalTexture.PixelData.size() +
                   staging.AOTexture.PixelData.size() +
                   staging.RoughnessTexture.PixelData.size() +
                   staging.MetallicTexture.PixelData.size();
        }

        uint32_t GetStagedPreparedTextureCount(const ModelStagingData& staging)
        {
            uint32_t count = 0;
            count += staging.AlbedoTexture.HasPreparedTexture() ? 1u : 0u;
            count += staging.NormalTexture.HasPreparedTexture() ? 1u : 0u;
            count += staging.AOTexture.HasPreparedTexture() ? 1u : 0u;
            count += staging.RoughnessTexture.HasPreparedTexture() ? 1u : 0u;
            count += staging.MetallicTexture.HasPreparedTexture() ? 1u : 0u;
            return count;
        }

        uint32_t GetStagedTextureCount(const ModelStagingData& staging)
        {
            uint32_t count = 0;
            count += staging.AlbedoTexture.HasData() ? 1u : 0u;
            count += staging.NormalTexture.HasData() ? 1u : 0u;
            count += staging.AOTexture.HasData() ? 1u : 0u;
            count += staging.RoughnessTexture.HasData() ? 1u : 0u;
            count += staging.MetallicTexture.HasData() ? 1u : 0u;
            return count;
        }

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
        };

        Thread::Mutex g_AsyncModelLoadMutex;
        VariableArray<TSharedPtr<AsyncModelLoadRequest>> g_PendingModelLoads;
        Map<String, TSharedPtr<AsyncModelLoadRequest>> g_PendingModelLoadsByPath;
        Thread::Atomic<uint32_t> g_NextAsyncModelLoadRequestId{1};

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

        bool ReadBinaryFile(const String& path,
                            VariableArray<uint8_t>& outData,
                            const char* role,
                            uint32_t requestId,
                            const char* stage)
        {
            auto readStartTime = LoadProfileNow();
            size_t bytesRead = 0;
            auto fileStream = NorvesLib::FileStream::FileStream::Create(
                path,
                NorvesLib::FileStream::FileMode::Read,
                NorvesLib::FileStream::FileAccess::Read,
                NorvesLib::FileStream::FileShare::Read);
            if (!fileStream || !fileStream->IsOpen())
            {
                NORVES_LOG_INFO("AssetLoadProfile",
                                "stage=%s role=%s request_id=%u path=\"%s\" bytes=%zu ms=%.3f success=0",
                                stage,
                                role,
                                static_cast<unsigned int>(requestId),
                                path.c_str(),
                                bytesRead,
                                LoadProfileElapsedMs(readStartTime));
                return false;
            }

            int64_t fileSize = fileStream->GetSize();
            if (fileSize < 0)
            {
                fileStream->Close();
                NORVES_LOG_INFO("AssetLoadProfile",
                                "stage=%s role=%s request_id=%u path=\"%s\" bytes=%zu file_size=%lld ms=%.3f success=0",
                                stage,
                                role,
                                static_cast<unsigned int>(requestId),
                                path.c_str(),
                                bytesRead,
                                static_cast<long long>(fileSize),
                                LoadProfileElapsedMs(readStartTime));
                return false;
            }

            outData.resize(static_cast<size_t>(fileSize));
            if (fileSize == 0)
            {
                fileStream->Close();
                NORVES_LOG_INFO("AssetLoadProfile",
                                "stage=%s role=%s request_id=%u path=\"%s\" bytes=0 file_size=0 ms=%.3f success=1",
                                stage,
                                role,
                                static_cast<unsigned int>(requestId),
                                path.c_str(),
                                LoadProfileElapsedMs(readStartTime));
                return true;
            }

            bytesRead = fileStream->Read(outData.data(), outData.size());
            fileStream->Close();
            bool bSuccess = bytesRead == outData.size();
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=%s role=%s request_id=%u path=\"%s\" bytes=%zu file_size=%lld ms=%.3f success=%d",
                            stage,
                            role,
                            static_cast<unsigned int>(requestId),
                            path.c_str(),
                            bytesRead,
                            static_cast<long long>(fileSize),
                            LoadProfileElapsedMs(readStartTime),
                            bSuccess ? 1 : 0);
            return bSuccess;
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
                if (!ReadBinaryFile(bufferPath, outBufferData[index], role, requestId, "gltf_buffer_read"))
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

        bool DecodeImageFile(const String& filePath,
                             VariableArray<uint8_t>& outPixels,
                             uint32_t& outWidth,
                             uint32_t& outHeight,
                             const char* role,
                             uint32_t requestId)
        {
            VariableArray<uint8_t> fileData;
            if (!ReadBinaryFile(filePath, fileData, role, requestId, "gltf_image_read") || fileData.empty())
            {
                NORVES_LOG_ERROR("GLTFAnalyzer", "Failed to read image file: %s", filePath.c_str());
                return false;
            }

            int width = 0;
            int height = 0;
            int channels = 0;
            auto decodeStartTime = LoadProfileNow();
            unsigned char* pPixels = stbi_load_from_memory(
                fileData.data(),
                static_cast<int>(fileData.size()),
                &width,
                &height,
                &channels,
                4);
            double decodeMs = LoadProfileElapsedMs(decodeStartTime);
            if (pPixels == nullptr || width <= 0 || height <= 0)
            {
                NORVES_LOG_INFO("AssetLoadProfile",
                                "stage=gltf_image_decode role=%s request_id=%u path=\"%s\" file_bytes=%zu width=%d height=%d channels=%d ms=%.3f success=0",
                                role,
                                static_cast<unsigned int>(requestId),
                                filePath.c_str(),
                                fileData.size(),
                                width,
                                height,
                                channels,
                                decodeMs);
                NORVES_LOG_ERROR("GLTFAnalyzer", "Failed to decode image file: %s", filePath.c_str());
                if (pPixels != nullptr)
                {
                    stbi_image_free(pPixels);
                }
                return false;
            }

            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=gltf_image_decode role=%s request_id=%u path=\"%s\" file_bytes=%zu width=%d height=%d channels=%d ms=%.3f success=1",
                            role,
                            static_cast<unsigned int>(requestId),
                            filePath.c_str(),
                            fileData.size(),
                            width,
                            height,
                            channels,
                            decodeMs);

            outWidth = static_cast<uint32_t>(width);
            outHeight = static_cast<uint32_t>(height);

            size_t pixelDataSize = static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight) * 4;
            auto copyStartTime = LoadProfileNow();
            outPixels.resize(pixelDataSize);
            std::memcpy(outPixels.data(), pPixels, pixelDataSize);
            double copyMs = LoadProfileElapsedMs(copyStartTime);
            stbi_image_free(pPixels);
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=gltf_image_copy role=%s request_id=%u path=\"%s\" pixel_bytes=%zu width=%u height=%u ms=%.3f success=1",
                            role,
                            static_cast<unsigned int>(requestId),
                            filePath.c_str(),
                            pixelDataSize,
                            outWidth,
                            outHeight,
                            copyMs);
            return true;
        }

        bool CreateTextureFromPixels(Rendering::TextureResources& textures,
                                     const String& debugName,
                                     uint32_t width,
                                     uint32_t height,
                                     Rendering::TextureCreateInfo::Format format,
                                     const void* pPixelData,
                                     size_t pixelDataSize,
                                     NorvesLib::RHI::TexturePtr& outTexture)
        {
            auto calculateMipCount = [](uint32_t textureWidth, uint32_t textureHeight) -> uint32_t
            {
                uint32_t mipLevels = 1;
                while (textureWidth > 1 || textureHeight > 1)
                {
                    textureWidth = std::max(1u, textureWidth / 2);
                    textureHeight = std::max(1u, textureHeight / 2);
                    ++mipLevels;
                }
                return mipLevels;
            };

            Rendering::TextureCreateInfo createInfo;
            createInfo.Width = width;
            createInfo.Height = height;
            createInfo.MipLevels = calculateMipCount(width, height);
            createInfo.PixelFormat = format;
            createInfo.DebugName = debugName;

            Rendering::TextureHandle textureHandle = textures.CreateTexture(createInfo, pPixelData, pixelDataSize);
            if (!textureHandle.IsValid())
            {
                return false;
            }

            outTexture = textures.GetRHITexturePtr(textureHandle);
            return static_cast<bool>(outTexture);
        }

        void SetStagedTextureData(StagedTextureData& outTexture,
                                  VariableArray<uint8_t>&& pixels,
                                  uint32_t width,
                                  uint32_t height,
                                  Rendering::TextureCreateInfo::Format format,
                                  const String& debugName)
        {
            outTexture.PixelData = std::move(pixels);
            outTexture.PreparedTexture = {};
            outTexture.Width = width;
            outTexture.Height = height;
            outTexture.Format = format;
            outTexture.DebugName = debugName;
            outTexture.bHasPreparedTexture = false;
        }

        void SetPreparedStagedTextureData(StagedTextureData& outTexture,
                                          Rendering::PreparedTextureAsset&& preparedTexture,
                                          const String& debugName)
        {
            outTexture.PixelData.clear();
            outTexture.PreparedTexture = std::move(preparedTexture);
            outTexture.Width = 0;
            outTexture.Height = 0;
            outTexture.Format = Rendering::TextureCreateInfo::Format::RGBA8_UNORM;
            outTexture.DebugName = debugName;
            outTexture.bHasPreparedTexture = true;
        }

        bool ShouldUseLooseFallbackForPreparedStatus(Rendering::PreparedTextureAssetStatus status)
        {
            switch (status)
            {
            case Rendering::PreparedTextureAssetStatus::ManifestMissingLooseFallback:
            case Rendering::PreparedTextureAssetStatus::VariantMissingLooseFallback:
            case Rendering::PreparedTextureAssetStatus::DebugLooseFallback:
            case Rendering::PreparedTextureAssetStatus::InvalidRequest:
            case Rendering::PreparedTextureAssetStatus::InvalidPath:
            case Rendering::PreparedTextureAssetStatus::AbsolutePathUnsupported:
                return true;
            default:
                return false;
            }
        }

        bool DecodeStandardTextureFallback(const TextureReference& reference,
                                           const String& debugName,
                                           StagedTextureData& outTexture,
                                           const char* role,
                                           uint32_t requestId)
        {
            if (reference.ResolvedFallbackPath.empty())
            {
                return false;
            }

            VariableArray<uint8_t> pixels;
            uint32_t width = 0;
            uint32_t height = 0;
            if (!DecodeImageFile(reference.ResolvedFallbackPath, pixels, width, height, role, requestId))
            {
                return false;
            }

            SetStagedTextureData(
                outTexture,
                std::move(pixels),
                width,
                height,
                Rendering::TextureCreateInfo::Format::RGBA8_UNORM,
                debugName);
            return true;
        }

        bool DecodeArmTextureFallback(const TextureReference& reference,
                                      const String& debugNamePrefix,
                                      StagedTextureData& outAOTexture,
                                      StagedTextureData& outRoughnessTexture,
                                      StagedTextureData& outMetallicTexture,
                                      const char* role,
                                      uint32_t requestId)
        {
            if (reference.ResolvedFallbackPath.empty())
            {
                return false;
            }

            VariableArray<uint8_t> pixels;
            uint32_t width = 0;
            uint32_t height = 0;
            if (!DecodeImageFile(reference.ResolvedFallbackPath, pixels, width, height, role, requestId))
            {
                return false;
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

            SetStagedTextureData(
                outAOTexture,
                std::move(aoPixels),
                width,
                height,
                Rendering::TextureCreateInfo::Format::R8_UNORM,
                debugNamePrefix + "_AO");
            SetStagedTextureData(
                outRoughnessTexture,
                std::move(roughnessPixels),
                width,
                height,
                Rendering::TextureCreateInfo::Format::R8_UNORM,
                debugNamePrefix + "_Roughness");
            SetStagedTextureData(
                outMetallicTexture,
                std::move(metallicPixels),
                width,
                height,
                Rendering::TextureCreateInfo::Format::R8_UNORM,
                debugNamePrefix + "_Metallic");
            return true;
        }

        bool StageStandardTexture(const TextureReference& textureReference,
                                  const String& debugName,
                                  StagedTextureData& outTexture,
                                  const char* role,
                                  uint32_t requestId)
        {
            if (!textureReference.HasReference())
            {
                return true;
            }

            if (!textureReference.RequestPath.empty())
            {
                return true;
            }

            return DecodeStandardTextureFallback(textureReference, debugName, outTexture, role, requestId);
        }

        bool StageArmTextures(const TextureReference& textureReference,
                              const String& debugNamePrefix,
                              StagedTextureData& outAOTexture,
                              StagedTextureData& outRoughnessTexture,
                              StagedTextureData& outMetallicTexture,
                              const char* role,
                              uint32_t requestId)
        {
            if (!textureReference.HasReference())
            {
                return true;
            }

            if (!textureReference.RequestPath.empty())
            {
                return true;
            }

            return DecodeArmTextureFallback(
                textureReference,
                debugNamePrefix,
                outAOTexture,
                outRoughnessTexture,
                outMetallicTexture,
                role,
                requestId);
        }

        bool CreateTextureFromStagedData(Rendering::TextureResources& textures,
                                         const StagedTextureData& stagedTexture,
                                         NorvesLib::RHI::TexturePtr& outTexture,
                                         const char* role,
                                         uint32_t requestId)
        {
            if (!stagedTexture.HasData())
            {
                return true;
            }

            if (stagedTexture.HasPreparedTexture())
            {
                if (!textures.IsPreparedTextureAssetCurrent(stagedTexture.PreparedTexture))
                {
                    return false;
                }

                Rendering::TextureHandle textureHandle = textures.FinalizePreparedTextureAsset(
                    stagedTexture.PreparedTexture,
                    role,
                    requestId);
                if (!textureHandle.IsValid())
                {
                    return false;
                }

                outTexture = textures.GetRHITexturePtr(textureHandle);
                return static_cast<bool>(outTexture);
            }

            if (!stagedTexture.HasLoosePixelData())
            {
                return false;
            }

            return CreateTextureFromPixels(
                textures,
                stagedTexture.DebugName,
                stagedTexture.Width,
                stagedTexture.Height,
                stagedTexture.Format,
                stagedTexture.PixelData.data(),
                stagedTexture.PixelData.size(),
                outTexture);
        }

        bool CreateStandardTextureFromReference(Rendering::TextureResources& textures,
                                                const TextureReference& textureReference,
                                                const String& debugName,
                                                NorvesLib::RHI::TexturePtr& outTexture,
                                                const char* role,
                                                uint32_t requestId)
        {
            if (!textureReference.HasReference())
            {
                return true;
            }

            if (!textureReference.RequestPath.empty())
            {
                Rendering::PreparedTextureAsset prepared = textures.PrepareTextureAssetForWorker(
                    textureReference.RequestPath,
                    textureReference.ResolvedFallbackPath,
                    role,
                    requestId);

                if (prepared.Status == Rendering::PreparedTextureAssetStatus::CookedReady)
                {
                    if (!textures.IsPreparedTextureAssetCurrent(prepared))
                    {
                        return false;
                    }

                    Rendering::TextureHandle textureHandle = textures.FinalizePreparedTextureAsset(
                        prepared,
                        role,
                        requestId);
                    if (!textureHandle.IsValid())
                    {
                        return false;
                    }

                    outTexture = textures.GetRHITexturePtr(textureHandle);
                    return static_cast<bool>(outTexture);
                }

                if (!ShouldUseLooseFallbackForPreparedStatus(prepared.Status))
                {
                    return false;
                }
            }

            StagedTextureData looseFallback;
            if (!DecodeStandardTextureFallback(textureReference, debugName, looseFallback, role, requestId))
            {
                return false;
            }

            return CreateTextureFromStagedData(textures, looseFallback, outTexture, role, requestId);
        }

        bool CreateArmTexturesFromReference(Rendering::TextureResources& textures,
                                            const TextureReference& textureReference,
                                            const String& debugNamePrefix,
                                            NorvesLib::RHI::TexturePtr& outAOTexture,
                                            NorvesLib::RHI::TexturePtr& outRoughnessTexture,
                                            NorvesLib::RHI::TexturePtr& outMetallicTexture,
                                            const char* role,
                                            uint32_t requestId)
        {
            if (!textureReference.HasReference())
            {
                return true;
            }

            StagedTextureData aoStaging;
            StagedTextureData roughnessStaging;
            StagedTextureData metallicStaging;

            if (!textureReference.RequestPath.empty())
            {
                Rendering::PreparedTextureAsset prepared = textures.PrepareTextureAssetForWorker(
                    textureReference.RequestPath,
                    textureReference.ResolvedFallbackPath,
                    role,
                    requestId);

                if (prepared.Status == Rendering::PreparedTextureAssetStatus::CookedReady)
                {
                    if (!textures.IsPreparedTextureAssetCurrent(prepared))
                    {
                        return false;
                    }

                    Rendering::PreparedCookedTextureMip0RGBA8UNormLinearSplit split;
                    String splitReason;
                    if (!textures.TrySplitPreparedCookedTextureMip0RGBA8UNormLinear(
                            prepared,
                            split,
                            &splitReason,
                            role,
                            requestId))
                    {
                        if (prepared.FallbackMode == Rendering::TextureAssetFallbackMode::DebugAllowLooseFallback)
                        {
                            if (!DecodeArmTextureFallback(
                                    textureReference,
                                    debugNamePrefix,
                                    aoStaging,
                                    roughnessStaging,
                                    metallicStaging,
                                    role,
                                    requestId))
                            {
                                return false;
                            }
                        }
                        else
                        {
                            NORVES_LOG_ERROR("GLTFAnalyzer",
                                             "Failed to split cooked ARM texture: %s (%s)",
                                             textureReference.RequestPath.c_str(),
                                             splitReason.c_str());
                            return false;
                        }
                    }
                    else
                    {
                        SetStagedTextureData(
                            aoStaging,
                            std::move(split.R),
                            split.Width,
                            split.Height,
                            Rendering::TextureCreateInfo::Format::R8_UNORM,
                            debugNamePrefix + "_AO");
                        SetStagedTextureData(
                            roughnessStaging,
                            std::move(split.G),
                            split.Width,
                            split.Height,
                            Rendering::TextureCreateInfo::Format::R8_UNORM,
                            debugNamePrefix + "_Roughness");
                        SetStagedTextureData(
                            metallicStaging,
                            std::move(split.B),
                            split.Width,
                            split.Height,
                            Rendering::TextureCreateInfo::Format::R8_UNORM,
                            debugNamePrefix + "_Metallic");
                    }
                }
                else if (!ShouldUseLooseFallbackForPreparedStatus(prepared.Status))
                {
                    return false;
                }
            }

            if (!aoStaging.HasData() &&
                !DecodeArmTextureFallback(
                    textureReference,
                    debugNamePrefix,
                    aoStaging,
                    roughnessStaging,
                    metallicStaging,
                    role,
                    requestId))
            {
                return false;
            }

            return CreateTextureFromStagedData(textures, aoStaging, outAOTexture, role, requestId) &&
                   CreateTextureFromStagedData(textures, roughnessStaging, outRoughnessTexture, role, requestId) &&
                   CreateTextureFromStagedData(textures, metallicStaging, outMetallicTexture, role, requestId);
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
        Rendering::ModelHandle FinalizeModelStaging(const ModelStagingData& staging,
                                                    Rendering::ModelLoadResourceContext resources,
                                                    const char* role,
                                                    uint32_t requestId)
        {
            auto totalStartTime = LoadProfileNow();
            Rendering::MegaGeometry::MegaMeshMaterial material;
            material.BaseColor[0] = 1.0f;
            material.BaseColor[1] = 1.0f;
            material.BaseColor[2] = 1.0f;
            material.BaseColor[3] = 1.0f;

            auto textureFinalizeStartTime = LoadProfileNow();
            bool bAlbedoFinalizeSuccess = false;
            bool bNormalFinalizeSuccess = false;
            bool bAOFinalizeSuccess = false;
            bool bRoughnessFinalizeSuccess = false;
            bool bMetallicFinalizeSuccess = false;
            {
                Rendering::ScopedTextureCreateUploadProfileRole profileRole(role);
                bAlbedoFinalizeSuccess = staging.AlbedoTexture.HasData()
                                              ? CreateTextureFromStagedData(resources.Textures, staging.AlbedoTexture, material.AlbedoTexture, role, requestId)
                                              : CreateStandardTextureFromReference(resources.Textures, staging.TextureReferences.Albedo, staging.DebugName + "_Albedo", material.AlbedoTexture, role, requestId);
                bNormalFinalizeSuccess = staging.NormalTexture.HasData()
                                             ? CreateTextureFromStagedData(resources.Textures, staging.NormalTexture, material.NormalTexture, role, requestId)
                                             : CreateStandardTextureFromReference(resources.Textures, staging.TextureReferences.Normal, staging.DebugName + "_Normal", material.NormalTexture, role, requestId);
                if (staging.AOTexture.HasData() ||
                    staging.RoughnessTexture.HasData() ||
                    staging.MetallicTexture.HasData())
                {
                    bAOFinalizeSuccess = CreateTextureFromStagedData(resources.Textures, staging.AOTexture, material.AOTexture, role, requestId);
                    bRoughnessFinalizeSuccess = CreateTextureFromStagedData(resources.Textures, staging.RoughnessTexture, material.RoughnessTexture, role, requestId);
                    bMetallicFinalizeSuccess = CreateTextureFromStagedData(resources.Textures, staging.MetallicTexture, material.MetallicTexture, role, requestId);
                }
                else
                {
                    bAOFinalizeSuccess = bRoughnessFinalizeSuccess = bMetallicFinalizeSuccess =
                        CreateArmTexturesFromReference(
                            resources.Textures,
                            staging.TextureReferences.Arm,
                            staging.DebugName,
                            material.AOTexture,
                            material.RoughnessTexture,
                            material.MetallicTexture,
                            role,
                            requestId);
                }
            }
            bool bTextureFinalizeSuccess =
                bAlbedoFinalizeSuccess &&
                bNormalFinalizeSuccess &&
                bAOFinalizeSuccess &&
                bRoughnessFinalizeSuccess &&
                bMetallicFinalizeSuccess;
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=gltf_finalize_textures role=%s request_id=%u debug_name=\"%s\" textures=%u prepared_textures=%u loose_texture_bytes=%zu ms=%.3f success=%d",
                            role,
                            static_cast<unsigned int>(requestId),
                            staging.DebugName.c_str(),
                            static_cast<unsigned int>(GetStagedTextureCount(staging)),
                            static_cast<unsigned int>(GetStagedPreparedTextureCount(staging)),
                            GetStagedLooseTextureBytes(staging),
                            LoadProfileElapsedMs(textureFinalizeStartTime),
                            bTextureFinalizeSuccess ? 1 : 0);
            if (!bTextureFinalizeSuccess)
            {
                return Rendering::ModelHandle::Invalid();
            }

            Rendering::MegaGeometry::MegaMeshCreateInfo createInfo;
            createInfo.VertexData = staging.Vertices.data();
            createInfo.VertexDataSize = staging.Vertices.size() * sizeof(Rendering::Mesh3DVertex);
            createInfo.VertexCount = static_cast<uint32_t>(staging.Vertices.size());
            createInfo.VertexStride = sizeof(Rendering::Mesh3DVertex);
            createInfo.IndexData = staging.ClusterizedIndices.data();
            createInfo.IndexCount = static_cast<uint32_t>(staging.ClusterizedIndices.size());
            createInfo.Clusters = staging.Clusters;
            createInfo.TotalBounds = staging.TotalBounds;
            createInfo.bBuildLODHierarchy = false;
            createInfo.Material = material;
            createInfo.DebugName = staging.DebugName;

            auto megaMeshCreateStartTime = LoadProfileNow();
            Rendering::MegaGeometry::MegaMeshHandle megaMeshHandle = resources.MegaGeometry.CreateMegaMesh(createInfo);
            double megaMeshCreateMs = LoadProfileElapsedMs(megaMeshCreateStartTime);
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=gltf_finalize_megamesh role=%s request_id=%u debug_name=\"%s\" vertices=%zu indices=%zu clusters=%zu ms=%.3f success=%d",
                            role,
                            static_cast<unsigned int>(requestId),
                            staging.DebugName.c_str(),
                            staging.Vertices.size(),
                            staging.ClusterizedIndices.size(),
                            staging.Clusters.size(),
                            megaMeshCreateMs,
                            megaMeshHandle.IsValid() ? 1 : 0);
            if (!megaMeshHandle.IsValid())
            {
                NORVES_LOG_ERROR("GLTFAnalyzer", "Failed to create MegaMesh: %s", staging.DebugName.c_str());
                return Rendering::ModelHandle::Invalid();
            }

            auto modelRegisterStartTime = LoadProfileNow();
            Rendering::ModelHandle modelHandle = resources.MegaGeometry.RegisterModel(
                megaMeshHandle,
                staging.DebugName,
                staging.ResolvedPath);
            double modelRegisterMs = LoadProfileElapsedMs(modelRegisterStartTime);
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=gltf_finalize_register role=%s request_id=%u debug_name=\"%s\" path=\"%s\" ms=%.3f success=%d",
                            role,
                            static_cast<unsigned int>(requestId),
                            staging.DebugName.c_str(),
                            staging.ResolvedPath.c_str(),
                            modelRegisterMs,
                            modelHandle.IsValid() ? 1 : 0);
            if (!modelHandle.IsValid())
            {
                resources.MegaGeometry.ReleaseMegaMesh(megaMeshHandle);
                NORVES_LOG_ERROR("GLTFAnalyzer", "Failed to register model: %s", staging.DebugName.c_str());
                return Rendering::ModelHandle::Invalid();
            }

            NORVES_LOG_INFO("GLTFAnalyzer", "glTF model loaded: %s", staging.DebugName.c_str());
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=gltf_finalize_total role=%s request_id=%u debug_name=\"%s\" path=\"%s\" loose_texture_bytes=%zu prepared_textures=%u ms=%.3f success=1",
                            role,
                            static_cast<unsigned int>(requestId),
                            staging.DebugName.c_str(),
                            staging.ResolvedPath.c_str(),
                            GetStagedLooseTextureBytes(staging),
                            static_cast<unsigned int>(GetStagedPreparedTextureCount(staging)),
                            LoadProfileElapsedMs(totalStartTime));
            return modelHandle;
        }
    } // anonymous namespace

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
            g_PendingModelLoads.push_back(request);
            g_PendingModelLoadsByPath[resolvedGltfPath] = request;
        }

        Thread::JobSystem::Get().SubmitTask(request->Task);
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
                if (!request || !request->Task || !request->Task->IsCompleted())
                {
                    ++it;
                    continue;
                }

                g_PendingModelLoadsByPath.erase(request->ResolvedPath);
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
                callback.InvokeIfBound(modelHandle);
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

    uint32_t GLTFAnalyzer::GetPendingAsyncModelLoadCount()
    {
        Thread::ScopedLock lock(g_AsyncModelLoadMutex);
        return static_cast<uint32_t>(g_PendingModelLoads.size());
    }

} // namespace NorvesLib::Core::Resource

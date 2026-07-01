#pragma once

#include "Rendering/GpuResourceTypes.h"
#include "Rendering/ITextureHandleRegistrar.h"
#include "Rendering/MaterialTypes.h"
#include "Rendering/MegaGeometry/MegaGeometryTypes.h"
#include "Rendering/NeuralMaterialResource.h"
#include "Rendering/ProceduralMeshGPUData.h"
#include "Rendering/RenderResourcesFwd.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/TextureAssetTypes.h"
#include "Rendering/TextureAsyncTypes.h"
#include "Rendering/VertexLayout.h"
#include "Container/PointerTypes.h"
#include "Delegate/Delegate.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::RHI
{
    class IBuffer;
    class IDevice;
    class IShader;
    class ITexture;
}

namespace NorvesLib::Core::Rendering
{
    struct SubMesh;

    class GpuResources
    {
    public:
        BufferHandle CreateBuffer(const BufferCreateInfo &createInfo);
        BufferHandle CreateBuffer(const BufferCreateInfo &createInfo,
                                  const void *data,
                                  size_t dataSize);
        bool UpdateBuffer(BufferHandle handle,
                          const void *data,
                          size_t dataSize,
                          size_t offset = 0);
        void ReleaseBuffer(BufferHandle handle);

        SamplerHandle GetDefaultSampler();
        SamplerHandle GetPointSampler();
        void ReleaseSampler(SamplerHandle handle);

        ShaderHandle CreateShader(const ShaderCreateInfo &createInfo);
        ShaderHandle LoadShader(const Container::String &path, ShaderStage stage);
        void ReleaseShader(ShaderHandle handle);

        VertexLayoutHandle RegisterVertexLayout(const VertexLayout &layout);
        const VertexLayout *GetVertexLayout(VertexLayoutHandle handle) const;

        RHI::IBuffer *GetRHIBuffer(BufferHandle handle) const;
        RHI::IShader *GetRHIShader(ShaderHandle handle) const;
        ResourceStats GetResourceStats() const;

    private:
        friend class RenderResources;

        explicit GpuResources(RenderResources *pOwner);

        RenderResources *m_pOwner = nullptr;
    };

    class TextureResources : public ITextureHandleRegistrar
    {
    public:
        TextureHandle CreateTexture(const TextureCreateInfo &createInfo);
        TextureHandle CreateTexture(const TextureCreateInfo &createInfo,
                                    const void *data,
                                    size_t dataSize);
        TextureHandle LoadTexture(const Container::String &path);
        uint32_t LoadTextureAsync(const Container::String &path,
                                  NorvesLib::Core::Delegate<void, TextureHandle> callback = {});
        uint32_t FlushCompletedTextureLoads();
        uint32_t GetPendingAsyncLoadCount() const;

        bool SetTextureAssetRoot(const Container::String &assetRoot);
        bool LoadTextureAssetManifestFromJsonText(
            const Container::String &jsonText,
            const Container::String &sourceName = Container::String());
        bool ResetTextureAssetManifest();
        bool SetTextureAssetFallbackMode(TextureAssetFallbackMode mode);

        [[nodiscard]] PreparedTextureAsset PrepareTextureAssetForWorker(
            const Container::String &requestPath,
            const Container::String &resolvedFallbackPath = {},
            const char *role = "worker",
            uint32_t requestId = 0);
        [[nodiscard]] bool IsPreparedTextureAssetCurrent(const PreparedTextureAsset &prepared) const;
        [[nodiscard]] TextureHandle FinalizePreparedTextureAsset(
            const PreparedTextureAsset &prepared,
            const char *role = "main_render",
            uint32_t requestId = 0);
        [[nodiscard]] bool TrySplitPreparedCookedTextureMip0RGBA8UNormLinear(
            const PreparedTextureAsset &prepared,
            PreparedCookedTextureMip0RGBA8UNormLinearSplit &outSplit,
            Container::String *pOutReason = nullptr,
            const char *role = "worker",
            uint32_t requestId = 0) const;

        TextureHandle RegisterExternalTexture(
            Container::TSharedPtr<RHI::ITexture> rhiTexture,
            const Container::String &debugName = Container::String()) override;
        void ReleaseTexture(TextureHandle handle) override;

        RHI::ITexture *GetRHITexture(TextureHandle handle) const;
        Container::TSharedPtr<RHI::ITexture> GetRHITexturePtr(TextureHandle handle) const;

    private:
        friend class RenderResources;

        explicit TextureResources(RenderResources *pOwner);

        RenderResources *m_pOwner = nullptr;
    };

    class MaterialResources
    {
    public:
        MaterialHandle Create(const MaterialCreateData &createInfo);
        const MaterialResourceData *GetData(MaterialHandle handle) const;
        bool Update(MaterialHandle handle, const MaterialCreateData &createInfo);
        void Release(MaterialHandle handle);

        MaterialHandle CreateNeural(const NeuralMaterialDesc &desc);
        Container::VariableArray<NeuralMaterialResource *> GetNeuralResources() const;

    private:
        friend class RenderResources;

        explicit MaterialResources(RenderResources *pOwner);

        RenderResources *m_pOwner = nullptr;
    };

    class MeshResources
    {
    public:
        using MeshGPUData = ProceduralMeshGPUData;

        bool Register(MeshDataHandle handle,
                      const void *vertices,
                      size_t vertexSize,
                      const uint32_t *indices,
                      uint32_t indexCount);
        bool Register(MeshDataHandle handle,
                      const void *vertices,
                      size_t vertexSize,
                      const uint32_t *indices,
                      uint32_t indexCount,
                      const SubMesh* subMeshes,
                      uint32_t subMeshCount);
        const MeshGPUData *GetGPUData(MeshDataHandle handle) const;
        bool TryGetSubMeshRanges(MeshDataHandle handle,
                                 Container::FixedArray<SubMeshRange, MAX_MATERIAL_SLOTS>& out,
                                 uint32_t& outCount) const;
        void Unregister(MeshDataHandle handle);

    private:
        friend class RenderResources;

        explicit MeshResources(RenderResources *pOwner);

        RenderResources *m_pOwner = nullptr;
    };

    class MegaGeometryResources
    {
    public:
        MegaGeometry::MegaMeshHandle CreateMegaMesh(const MegaGeometry::MegaMeshCreateInfo &createInfo);
        const MegaGeometry::MegaMeshGPUData *GetMegaMeshGPUData(MegaGeometry::MegaMeshHandle handle) const;
        void ReleaseMegaMesh(MegaGeometry::MegaMeshHandle handle);

        ModelHandle RegisterModel(MegaGeometry::MegaMeshHandle megaMeshHandle,
                                  const Container::String &debugName = "",
                                  const Container::String &sourcePath = "");
        MegaGeometry::MegaMeshHandle GetModelMegaMeshHandle(ModelHandle handle) const;
        void ReleaseModel(ModelHandle handle);

    private:
        friend class RenderResources;

        explicit MegaGeometryResources(RenderResources *pOwner);

        RenderResources *m_pOwner = nullptr;
    };

    class RenderResources
    {
    public:
        RenderResources();
        ~RenderResources();

        RenderResources(const RenderResources &) = delete;
        RenderResources &operator=(const RenderResources &) = delete;

        bool Initialize(Container::TSharedPtr<RHI::IDevice> device);
        void Shutdown();
        bool IsInitialized() const;

        void ClearAllResources();
        void CleanupUnusedResources();
        ResourceStats GetResourceStats() const;

        GpuResources &Gpu() { return m_Gpu; }
        const GpuResources &Gpu() const { return m_Gpu; }

        TextureResources &Textures() { return m_Textures; }
        const TextureResources &Textures() const { return m_Textures; }

        MaterialResources &Materials() { return m_Materials; }
        const MaterialResources &Materials() const { return m_Materials; }

        MeshResources &Meshes() { return m_Meshes; }
        const MeshResources &Meshes() const { return m_Meshes; }

        MegaGeometryResources &MegaGeometry() { return m_MegaGeometry; }
        const MegaGeometryResources &MegaGeometry() const { return m_MegaGeometry; }

    private:
        friend class GpuResources;
        friend class TextureResources;
        friend class MaterialResources;
        friend class MeshResources;
        friend class MegaGeometryResources;

        struct Impl;

        Container::TUniquePtr<Impl> m_Impl;
        GpuResources m_Gpu;
        TextureResources m_Textures;
        MaterialResources m_Materials;
        MeshResources m_Meshes;
        MegaGeometryResources m_MegaGeometry;
    };

} // namespace NorvesLib::Core::Rendering

#pragma once

#include "Container/Containers.h"
#include "Rendering/RenderGraph/RenderGraphTypes.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphDump.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "Text/IdentityPool.h"
#include <cstdint>

namespace NorvesLib::RHI
{
    class TransientResourcePool;
}

namespace NorvesLib::Core::Rendering
{
    struct ViewRenderContext;

    struct RGTextureOutput
    {
        Identity Name;
        RGTextureHandle Handle;
        RHI::TexturePtr Texture;
    };

    struct RenderGraphExecutionResult
    {
        bool bSuccess = false;
        uint32_t ExecutedPassCount = 0;
        Container::UnorderedMap<Identity, RGTextureOutput, Identity::Hasher> TextureOutputs;

        bool TryGetTexture(Identity name, RHI::TexturePtr& outTexture) const
        {
            outTexture = nullptr;
            if (!name.IsValid())
            {
                return false;
            }

            auto it = TextureOutputs.find(name);
            if (it == TextureOutputs.end() || !it->second.Texture)
            {
                return false;
            }

            outTexture = it->second.Texture;
            return true;
        }
    };

    class RenderGraph
    {
    public:
        RenderGraph() = default;
        ~RenderGraph();

        RenderGraph(const RenderGraph&) = delete;
        RenderGraph& operator=(const RenderGraph&) = delete;

        bool Initialize(RHI::TransientResourcePool* transientPool);
        void Shutdown();
        bool IsInitialized() const
        {
            return m_bInitialized;
        }

        void BeginFrame(uint64_t frameIndex);
        void Reset();

        uint32_t AddPass(IRenderGraphPass* pass);
        bool AddDependency(uint32_t beforePassIndex, uint32_t afterPassIndex);

        bool Compile();
        bool Compile(const ViewRenderContext& context);
        bool Execute(ViewRenderContext& context);
        RenderGraphExecutionResult ExecuteWithResult(ViewRenderContext& context);

        void SetDebugDumpOptions(const RGDumpOptions& options);
        const RGDumpOptions& GetDebugDumpOptions() const
        {
            return m_DebugDumpOptions;
        }
        RGDumpStrings BuildDebugDump() const;
        bool WriteDebugDumpFiles();

        const RenderGraphExecutionResult& GetLastExecutionResult() const
        {
            return m_LastExecutionResult;
        }

        bool TryGetLastOutputTexture(Identity name, RHI::TexturePtr& outTexture) const
        {
            return m_LastExecutionResult.TryGetTexture(name, outTexture);
        }

        const Container::VariableArray<uint32_t>& GetCompiledPassOrder() const
        {
            return m_CompiledPassOrder;
        }

        const Container::VariableArray<RGCompiledBarrier>& GetCompiledBarriers() const
        {
            return m_CompiledBarriers;
        }

        const Container::VariableArray<RGCompiledResourceLifetime>& GetCompiledResourceLifetimes() const
        {
            return m_CompiledResourceLifetimes;
        }

        const Container::VariableArray<RGTransientAllocationStep>& GetTransientAllocationPlan() const
        {
            return m_TransientAllocationPlan;
        }

        bool TryGetCompiledResourceLifetime(RGResourceHandle handle, RGCompiledResourceLifetime& outLifetime) const;
        bool TryGetDeclaredPassVersionDiagnostic(uint32_t passIndex,
                                                 uint32_t accessIndex,
                                                 RGNamedResourceVersionDiagnostic& outDiagnostic) const;
        RenderGraphDebugStats GetDebugStats() const
        {
            RenderGraphDebugStats stats;
            stats.DeclaredPassCount = static_cast<uint32_t>(m_Passes.size());
            stats.CompiledPassCount = static_cast<uint32_t>(m_CompiledPassOrder.size());
            stats.ExecutedPassCount = m_LastExecutedPassCount;
            stats.TransientAcquireCount = m_LastTransientAcquireCount;
            stats.BarrierCount = m_LastCompiledBarrierCount;
            return stats;
        }

        uint32_t GetDeclaredPassAccessCount(uint32_t passIndex) const;
        bool TryGetNamedResourceVersion(Identity name, uint32_t& outVersion) const;
        bool TryGetDeclaredPassAccess(uint32_t passIndex,
                                      uint32_t accessIndex,
                                      RGResourceHandle& outResource,
                                      RGAccessMode& outMode,
                                      RHI::ResourceState& outState,
                                      RHI::ResourceState& outFinalState,
                                      bool* outColorAttachmentLoadStore = nullptr,
                                      RHI::AttachmentLoadOp* outLoadOp = nullptr,
                                      RHI::AttachmentStoreOp* outStoreOp = nullptr,
                                      RGAttachmentKind* outAttachmentKind = nullptr,
                                      RGAttachmentMutability* outAttachmentMutability = nullptr) const;

        uint32_t GetPassCount() const
        {
            return static_cast<uint32_t>(m_Passes.size());
        }

        uint32_t GetLastExecutedPassCount() const
        {
            return m_LastExecutedPassCount;
        }

        uint32_t GetLastCompiledBarrierCount() const
        {
            return m_LastCompiledBarrierCount;
        }

        uint32_t GetLastTransientAcquireCount() const
        {
            return m_LastTransientAcquireCount;
        }

    private:
        friend class RenderGraphBuilder;
        friend class RenderGraphResources;

        struct RGResourceRecord
        {
            RGResourceHandle Handle;
            RGResourceKind Kind = RGResourceKind::Invalid;
            RGResourceLifetime Lifetime = RGResourceLifetime::Invalid;
            RGTextureDesc TextureDesc;
            RGBufferDesc BufferDesc;
            RHI::TexturePtr ImportedTexture;
            RHI::BufferPtr ImportedBuffer;
            RHI::TexturePtr ExecutionTexture;
            RHI::BufferPtr ExecutionBuffer;
            RHI::ResourceState InitialState = RHI::ResourceState::Undefined;
            const char* DebugName = nullptr;
        };

        struct RGPassAccess
        {
            RGResourceHandle Resource;
            RGAccessMode Mode = RGAccessMode::Read;
            RHI::ResourceState State = RHI::ResourceState::ShaderResource;
            RHI::ResourceState FinalState = RHI::ResourceState::ShaderResource;
            bool bAttachment = false;
            RGAttachmentKind AttachmentKind = RGAttachmentKind::Color;
            RGAttachmentMutability AttachmentMutability = RGAttachmentMutability::Write;
            RHI::AttachmentLoadOp LoadOp = RHI::AttachmentLoadOp::DontCare;
            RHI::AttachmentStoreOp StoreOp = RHI::AttachmentStoreOp::Store;
            uint64_t BufferOffset = 0;
            uint64_t BufferSize = 0;
            Identity NamedResourceIdentity;
            bool bNamedResourceVersionBeforeValid = false;
            uint32_t NamedResourceVersionBefore = 0;
            bool bNamedResourceVersionAfterValid = false;
            uint32_t NamedResourceVersionAfter = 0;
            bool bCreatesNewHead = false;
            bool bMutatesCurrentHead = false;
        };

        struct RGPassDeclaration
        {
            Container::VariableArray<RGPassAccess> Accesses;
            bool bPreserveInsertionOrder = false;
        };

        struct RGPassDependency
        {
            uint32_t BeforePassIndex = RGInvalidPassIndex;
            uint32_t AfterPassIndex = RGInvalidPassIndex;
        };

        struct RGNamedResource
        {
            RGResourceKind Kind = RGResourceKind::Invalid;
            RGResourceHandle CurrentHead;
            uint32_t Version = 0;
        };

        RGResourceHandle CreateTextureResource(const RGTextureDesc& desc);
        RGResourceHandle CreateBufferResource(const RGBufferDesc& desc);
        RGResourceHandle CreateLogicalResource(const char* debugName);
        RGResourceHandle ImportTextureResource(RHI::TexturePtr texture,
                                               RHI::ResourceState initialState,
                                               const char* debugName);
        RGResourceHandle ImportBufferResource(RHI::BufferPtr buffer,
                                              RHI::ResourceState initialState,
                                              const char* debugName);

        RGTextureHandle CreateTextureResourceHandle(const RGTextureDesc& desc);
        RGBufferHandle CreateBufferResourceHandle(const RGBufferDesc& desc);
        bool PublishTextureResource(Identity name, RGTextureHandle handle);
        bool PublishTextureResource(Identity name, RGResourceHandle handle);
        bool PublishBufferResource(Identity name, RGBufferHandle handle);
        RGTextureHandle ReadTextureResource(uint32_t passIndex,
                                            Identity name,
                                            RHI::ResourceState state);
        bool TryReadTextureResource(uint32_t passIndex,
                                    Identity name,
                                    RGTextureHandle& outHandle,
                                    RHI::ResourceState state);
        bool TryLoadStoreColorAttachmentResource(uint32_t passIndex,
                                                 Identity name,
                                                 RGTextureHandle& outHandle,
                                                 RHI::AttachmentLoadOp loadOp,
                                                 RHI::AttachmentStoreOp storeOp,
                                                 RHI::ResourceState state,
                                                 RHI::ResourceState finalState);
        bool TryUseAttachmentResource(uint32_t passIndex,
                                      Identity name,
                                      RGTextureHandle& outHandle,
                                      RGAttachmentKind kind,
                                      RGAttachmentMutability mutability,
                                      RHI::AttachmentLoadOp loadOp,
                                      RHI::AttachmentStoreOp storeOp,
                                      RHI::ResourceState state,
                                      RHI::ResourceState finalState);
        RGBufferHandle ReadBufferResource(uint32_t passIndex,
                                          Identity name,
                                          RHI::ResourceState state,
                                          uint64_t offset = 0,
                                          uint64_t size = 0);
        RGTextureHandle WriteTextureResource(uint32_t passIndex,
                                             Identity name,
                                             const RGTextureDesc& desc,
                                             RHI::ResourceState state,
                                             RHI::ResourceState finalState);
        RGTextureHandle WriteTextureAttachmentResource(uint32_t passIndex,
                                                       Identity name,
                                                       const RGTextureDesc& desc,
                                                       RGAttachmentKind kind,
                                                       RHI::AttachmentLoadOp loadOp,
                                                       RHI::AttachmentStoreOp storeOp,
                                                       RHI::ResourceState state,
                                                       RHI::ResourceState finalState);
        RGBufferHandle WriteBufferResource(uint32_t passIndex,
                                           Identity name,
                                           const RGBufferDesc& desc,
                                           RHI::ResourceState state,
                                           RHI::ResourceState finalState,
                                           uint64_t offset = 0,
                                           uint64_t size = 0);
        bool TryGetTextureResource(Identity name, RGTextureHandle& outHandle);
        bool TryGetBufferResource(Identity name, RGBufferHandle& outHandle);
        bool ExportTextureResource(Identity name, RGTextureHandle handle);
        bool ExportTextureResource(Identity name, RGResourceHandle handle);

        void AddAccess(uint32_t passIndex,
                       RGResourceHandle handle,
                       RGAccessMode mode,
                       RHI::ResourceState state,
                       RHI::ResourceState finalState,
                       Identity namedResourceIdentity = Identity{},
                       uint64_t bufferOffset = 0,
                       uint64_t bufferSize = 0);
        void AddAttachmentAccess(uint32_t passIndex,
                                 RGResourceHandle handle,
                                 RGAttachmentKind kind,
                                 RGAttachmentMutability mutability,
                                 RHI::AttachmentLoadOp loadOp,
                                 RHI::AttachmentStoreOp storeOp,
                                 RHI::ResourceState state,
                                 RHI::ResourceState finalState,
                                 Identity namedResourceIdentity = Identity{});
        void AddPreserveInsertionOrder(uint32_t passIndex);
        bool NormalizeBufferRange(RGResourceHandle handle,
                                  uint64_t offset,
                                  uint64_t size,
                                  uint64_t& outOffset,
                                  uint64_t& outSize) const;

        bool CompileInternal(const ViewRenderContext* context);
        bool ValidatePassAccesses() const;
        bool ValidatePassIndex(uint32_t passIndex) const;
        bool ValidateHandle(RGResourceHandle handle) const;
        bool ValidateTextureHandle(RGTextureHandle handle) const;
        bool ValidateBufferHandle(RGBufferHandle handle) const;
        bool ValidateNamedResource(Identity name, RGResourceKind expectedKind, const RGNamedResource*& outResource) const;
        void MarkGraphError();
        bool AddEdge(uint32_t beforePassIndex,
                     uint32_t afterPassIndex,
                     Container::VariableArray<Container::VariableArray<uint32_t>>& adjacency,
                     Container::VariableArray<uint32_t>& indegree) const;
        bool BuildDependencyGraph(Container::VariableArray<Container::VariableArray<uint32_t>>& adjacency,
                                  Container::VariableArray<uint32_t>& indegree) const;
        bool TopologicalSort(const Container::VariableArray<Container::VariableArray<uint32_t>>& adjacency,
                             Container::VariableArray<uint32_t> indegree);
        void BuildResourceLifetimes();
        bool ValidateResourceLifetimes() const;
        void BuildTransientAllocationPlan();
        void BuildBarriers();
        bool ShouldBuildDebugDump() const;
        bool ShouldWriteDebugDump() const;
        bool ShouldEmitDebugMarkers() const;

        bool AcquireTransientResourcesForOrderIndex(uint32_t orderIndex);
        bool AcquireTransientResource(RGResourceRecord& resource);
        bool ExecuteBarrier(const RGCompiledBarrier& barrier, ViewRenderContext& context);
        void FlushPendingFrameCommands(ViewRenderContext& context) const;

        RHI::TexturePtr ResolveTexture(RGResourceHandle handle);
        RHI::BufferPtr ResolveBuffer(RGResourceHandle handle);
        RHI::ITexture* ResolveTextureRaw(RGResourceHandle handle);
        RHI::IBuffer* ResolveBufferRaw(RGResourceHandle handle);
        RHI::TexturePtr ResolveTexture(RGTextureHandle handle);
        RHI::BufferPtr ResolveBuffer(RGBufferHandle handle);
        RHI::ITexture* ResolveTextureRaw(RGTextureHandle handle);
        RHI::IBuffer* ResolveBufferRaw(RGBufferHandle handle);

        void ClearCompileData();
        void ClearCompiledProducts();
        bool HasResourceUsage(RHI::ResourceUsage usage, RHI::ResourceUsage flag) const;

        RHI::TransientResourcePool* m_TransientPool = nullptr;
        Container::VariableArray<IRenderGraphPass*> m_Passes;
        Container::VariableArray<RGResourceRecord> m_Resources;
        Container::VariableArray<RGPassDeclaration> m_PassDeclarations;
        Container::VariableArray<RGPassDependency> m_ExplicitDependencies;
        Container::VariableArray<uint32_t> m_CompiledPassOrder;
        Container::VariableArray<RGCompiledBarrier> m_CompiledBarriers;
        Container::VariableArray<RGCompiledResourceLifetime> m_CompiledResourceLifetimes;
        Container::VariableArray<RGTransientAllocationStep> m_TransientAllocationPlan;
        Container::UnorderedMap<Identity, RGNamedResource, Identity::Hasher> m_NamedResources;
        Container::UnorderedMap<Identity, RGTextureHandle, Identity::Hasher> m_TextureExports;
        RenderGraphExecutionResult m_LastExecutionResult;
        uint32_t m_HandleGeneration = 1;
        uint32_t m_LastExecutedPassCount = 0;
        uint64_t m_FrameIndex = 0;
        RGDumpOptions m_DebugDumpOptions;
        uint32_t m_DebugDumpFrameCount = 0;
        uint32_t m_LastCompiledBarrierCount = 0;
        uint32_t m_LastTransientAcquireCount = 0;
        bool m_bInitialized = false;
        bool m_bCompiled = false;
        bool m_bHasGraphError = false;
    };

} // namespace NorvesLib::Core::Rendering

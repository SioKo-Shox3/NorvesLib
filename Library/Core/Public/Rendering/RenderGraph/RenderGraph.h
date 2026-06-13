#pragma once

#include "Container/Containers.h"
#include "Rendering/RenderGraph/RenderGraphTypes.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include <cstdint>

namespace NorvesLib::RHI
{
    class TransientResourcePool;
}

namespace NorvesLib::Core::Rendering
{
    struct ViewRenderContext;

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
        bool Execute(ViewRenderContext& context);

        const Container::VariableArray<uint32_t>& GetCompiledPassOrder() const
        {
            return m_CompiledPassOrder;
        }

        const Container::VariableArray<RGCompiledBarrier>& GetCompiledBarriers() const
        {
            return m_CompiledBarriers;
        }

        uint32_t GetPassCount() const
        {
            return static_cast<uint32_t>(m_Passes.size());
        }

        uint32_t GetLastExecutedPassCount() const
        {
            return m_LastExecutedPassCount;
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

        RGResourceHandle CreateTextureResource(const RGTextureDesc& desc);
        RGResourceHandle CreateBufferResource(const RGBufferDesc& desc);
        RGResourceHandle CreateLogicalResource(const char* debugName);
        RGResourceHandle ImportTextureResource(RHI::TexturePtr texture,
                                               RHI::ResourceState initialState,
                                               const char* debugName);
        RGResourceHandle ImportBufferResource(RHI::BufferPtr buffer,
                                              RHI::ResourceState initialState,
                                              const char* debugName);

        void AddAccess(uint32_t passIndex,
                       RGResourceHandle handle,
                       RGAccessMode mode,
                       RHI::ResourceState state);
        void AddPreserveInsertionOrder(uint32_t passIndex);

        bool ValidatePassIndex(uint32_t passIndex) const;
        bool ValidateHandle(RGResourceHandle handle) const;
        bool AddEdge(uint32_t beforePassIndex,
                     uint32_t afterPassIndex,
                     Container::VariableArray<Container::VariableArray<uint32_t>>& adjacency,
                     Container::VariableArray<uint32_t>& indegree) const;
        bool BuildDependencyGraph(Container::VariableArray<Container::VariableArray<uint32_t>>& adjacency,
                                  Container::VariableArray<uint32_t>& indegree) const;
        bool TopologicalSort(const Container::VariableArray<Container::VariableArray<uint32_t>>& adjacency,
                             Container::VariableArray<uint32_t> indegree);
        void BuildBarriers();

        bool ExecuteBarrier(const RGCompiledBarrier& barrier, ViewRenderContext& context);
        void FlushPendingFrameCommands(ViewRenderContext& context) const;

        RHI::TexturePtr ResolveTexture(RGResourceHandle handle);
        RHI::BufferPtr ResolveBuffer(RGResourceHandle handle);
        RHI::ITexture* ResolveTextureRaw(RGResourceHandle handle);
        RHI::IBuffer* ResolveBufferRaw(RGResourceHandle handle);

        void ClearCompileData();
        bool HasResourceUsage(RHI::ResourceUsage usage, RHI::ResourceUsage flag) const;

        RHI::TransientResourcePool* m_TransientPool = nullptr;
        Container::VariableArray<IRenderGraphPass*> m_Passes;
        Container::VariableArray<RGResourceRecord> m_Resources;
        Container::VariableArray<RGPassDeclaration> m_PassDeclarations;
        Container::VariableArray<RGPassDependency> m_ExplicitDependencies;
        Container::VariableArray<uint32_t> m_CompiledPassOrder;
        Container::VariableArray<RGCompiledBarrier> m_CompiledBarriers;
        uint32_t m_HandleGeneration = 1;
        uint32_t m_LastExecutedPassCount = 0;
        uint64_t m_FrameIndex = 0;
        bool m_bInitialized = false;
        bool m_bCompiled = false;
    };

} // namespace NorvesLib::Core::Rendering

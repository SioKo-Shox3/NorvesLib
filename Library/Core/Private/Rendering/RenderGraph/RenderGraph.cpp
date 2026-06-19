#include "Rendering/RenderGraph/RenderGraph.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/ITexture.h"
#include "RHI/TransientResourcePool.h"
#include "Debug/Stats.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        bool IsInvalidPassIndex(uint32_t passIndex)
        {
            return passIndex == RGInvalidPassIndex;
        }
    } // namespace

    RGTextureDesc RGTextureDesc::RenderTarget(uint32_t width,
                                              uint32_t height,
                                              RHI::Format format,
                                              const char* debugName)
    {
        RGTextureDesc desc;
        desc.Width = width;
        desc.Height = height;
        desc.Format = format;
        desc.Usage = RHI::ResourceUsage::RenderTarget | RHI::ResourceUsage::ShaderRead;
        desc.DebugName = debugName;
        return desc;
    }

    RGTextureDesc RGTextureDesc::DepthStencil(uint32_t width,
                                              uint32_t height,
                                              RHI::Format format,
                                              const char* debugName)
    {
        RGTextureDesc desc;
        desc.Width = width;
        desc.Height = height;
        desc.Format = format;
        desc.Usage = RHI::ResourceUsage::DepthStencil | RHI::ResourceUsage::ShaderRead;
        desc.DebugName = debugName;
        return desc;
    }

    RHI::TextureDesc RGTextureDesc::ToRHI() const
    {
        RHI::TextureDesc desc;
        desc.Width = Width;
        desc.Height = Height;
        desc.Depth = Depth;
        desc.MipLevels = MipLevels;
        desc.ArraySize = ArraySize;
        desc.TextureFormat = Format;
        desc.Usage = Usage;
        desc.Dimension = Dimension;
        desc.IsCubemap = bIsCubemap;
        desc.DebugName = DebugName;
        return desc;
    }

    RHI::BufferDesc RGBufferDesc::ToRHI() const
    {
        return RHI::BufferDesc(Size, Usage, bCPUAccessible, DebugName);
    }

    RenderGraphBuilder::RenderGraphBuilder(RenderGraph* graph,
                                           uint32_t passIndex,
                                           const ViewRenderContext* context)
        : m_Graph(graph), m_PassIndex(passIndex), m_Context(context)
    {
    }

    RGResourceHandle RenderGraphBuilder::CreateTexture(const RGTextureDesc& desc)
    {
        return m_Graph ? m_Graph->CreateTextureResource(desc) : RGResourceHandle{};
    }

    RGResourceHandle RenderGraphBuilder::CreateBuffer(const RGBufferDesc& desc)
    {
        return m_Graph ? m_Graph->CreateBufferResource(desc) : RGResourceHandle{};
    }

    RGTextureHandle RenderGraphBuilder::CreateTextureHandle(const RGTextureDesc& desc)
    {
        return m_Graph ? m_Graph->CreateTextureResourceHandle(desc) : RGTextureHandle{};
    }

    RGBufferHandle RenderGraphBuilder::CreateBufferHandle(const RGBufferDesc& desc)
    {
        return m_Graph ? m_Graph->CreateBufferResourceHandle(desc) : RGBufferHandle{};
    }

    RGResourceHandle RenderGraphBuilder::CreateLogical(const char* debugName)
    {
        return m_Graph ? m_Graph->CreateLogicalResource(debugName) : RGResourceHandle{};
    }

    RGResourceHandle RenderGraphBuilder::ImportTexture(RHI::TexturePtr texture,
                                                       RHI::ResourceState initialState,
                                                       const char* debugName)
    {
        return m_Graph ? m_Graph->ImportTextureResource(texture, initialState, debugName) : RGResourceHandle{};
    }

    RGResourceHandle RenderGraphBuilder::ImportBuffer(RHI::BufferPtr buffer,
                                                      RHI::ResourceState initialState,
                                                      const char* debugName)
    {
        return m_Graph ? m_Graph->ImportBufferResource(buffer, initialState, debugName) : RGResourceHandle{};
    }

    void RenderGraphBuilder::Read(RGResourceHandle handle, RHI::ResourceState state)
    {
        Read(handle, state, 0, 0);
    }

    void RenderGraphBuilder::Read(RGResourceHandle handle,
                                  RHI::ResourceState state,
                                  uint64_t offset,
                                  uint64_t size)
    {
        if (m_Graph)
        {
            m_Graph->AddAccess(m_PassIndex, handle, RGAccessMode::Read, state, state, Identity{}, offset, size);
        }
    }

    void RenderGraphBuilder::Write(RGResourceHandle handle, RHI::ResourceState state)
    {
        Write(handle, state, state, 0, 0);
    }

    void RenderGraphBuilder::Write(RGResourceHandle handle,
                                   RHI::ResourceState state,
                                   uint64_t offset,
                                   uint64_t size)
    {
        Write(handle, state, state, offset, size);
    }

    void RenderGraphBuilder::Write(RGResourceHandle handle,
                                   RHI::ResourceState state,
                                   RHI::ResourceState finalState)
    {
        Write(handle, state, finalState, 0, 0);
    }

    void RenderGraphBuilder::Write(RGResourceHandle handle,
                                   RHI::ResourceState state,
                                   RHI::ResourceState finalState,
                                   uint64_t offset,
                                   uint64_t size)
    {
        if (m_Graph)
        {
            m_Graph->AddAccess(m_PassIndex, handle, RGAccessMode::Write, state, finalState, Identity{}, offset, size);
        }
    }

    bool RenderGraphBuilder::PublishTexture(Identity name, RGTextureHandle handle)
    {
        return m_Graph ? m_Graph->PublishTextureResource(name, handle) : false;
    }

    bool RenderGraphBuilder::PublishBuffer(Identity name, RGBufferHandle handle)
    {
        return m_Graph ? m_Graph->PublishBufferResource(name, handle) : false;
    }

    RGTextureHandle RenderGraphBuilder::ReadTexture(Identity name, RHI::ResourceState state)
    {
        return m_Graph ? m_Graph->ReadTextureResource(m_PassIndex, name, state) : RGTextureHandle{};
    }

    bool RenderGraphBuilder::TryReadTexture(Identity name, RGTextureHandle& outHandle, RHI::ResourceState state)
    {
        if (!m_Graph)
        {
            outHandle = RGTextureHandle{};
            return false;
        }

        return m_Graph->TryReadTextureResource(m_PassIndex, name, outHandle, state);
    }

    bool RenderGraphBuilder::TryLoadStoreColorAttachment(Identity name,
                                                         RGTextureHandle& outHandle,
                                                         RHI::AttachmentLoadOp loadOp,
                                                         RHI::AttachmentStoreOp storeOp,
                                                         RHI::ResourceState state,
                                                         RHI::ResourceState finalState)
    {
        if (!m_Graph)
        {
            outHandle = RGTextureHandle{};
            return false;
        }

        return m_Graph->TryLoadStoreColorAttachmentResource(m_PassIndex,
                                                            name,
                                                            outHandle,
                                                            loadOp,
                                                            storeOp,
                                                            state,
                                                            finalState);
    }

    bool RenderGraphBuilder::TryUseAttachment(Identity name,
                                              RGTextureHandle& outHandle,
                                              RGAttachmentKind kind,
                                              RGAttachmentMutability mutability,
                                              RHI::AttachmentLoadOp loadOp,
                                              RHI::AttachmentStoreOp storeOp,
                                              RHI::ResourceState state,
                                              RHI::ResourceState finalState)
    {
        if (!m_Graph)
        {
            outHandle = RGTextureHandle{};
            return false;
        }

        return m_Graph->TryUseAttachmentResource(m_PassIndex,
                                                 name,
                                                 outHandle,
                                                 kind,
                                                 mutability,
                                                 loadOp,
                                                 storeOp,
                                                 state,
                                                 finalState);
    }

    bool RenderGraphBuilder::LoadStoreColorAttachment(RGResourceHandle handle,
                                                      RHI::AttachmentLoadOp loadOp,
                                                      RHI::AttachmentStoreOp storeOp,
                                                      RHI::ResourceState state,
                                                      RHI::ResourceState finalState)
    {
        if (!m_Graph)
        {
            return false;
        }

        if (!m_Graph->ValidateHandle(handle) ||
            m_Graph->m_Resources[handle.Index].Kind != RGResourceKind::Texture)
        {
            m_Graph->MarkGraphError();
            return false;
        }

        m_Graph->AddAttachmentAccess(m_PassIndex,
                                     handle,
                                     RGAttachmentKind::Color,
                                     RGAttachmentMutability::Write,
                                     loadOp,
                                     storeOp,
                                     state,
                                     finalState);
        return true;
    }

    RGBufferHandle RenderGraphBuilder::ReadBuffer(Identity name, RHI::ResourceState state)
    {
        return ReadBuffer(name, state, 0, 0);
    }

    bool RenderGraphBuilder::UseAttachment(RGResourceHandle handle,
                                           RGAttachmentKind kind,
                                           RGAttachmentMutability mutability,
                                           RHI::AttachmentLoadOp loadOp,
                                           RHI::AttachmentStoreOp storeOp,
                                           RHI::ResourceState state,
                                           RHI::ResourceState finalState)
    {
        if (!m_Graph)
        {
            return false;
        }

        if (!m_Graph->ValidateHandle(handle) ||
            m_Graph->m_Resources[handle.Index].Kind != RGResourceKind::Texture)
        {
            m_Graph->MarkGraphError();
            return false;
        }

        m_Graph->AddAttachmentAccess(m_PassIndex,
                                     handle,
                                     kind,
                                     mutability,
                                     loadOp,
                                     storeOp,
                                     state,
                                     finalState);
        return true;
    }

    RGBufferHandle RenderGraphBuilder::ReadBuffer(Identity name,
                                                  RHI::ResourceState state,
                                                  uint64_t offset,
                                                  uint64_t size)
    {
        return m_Graph ? m_Graph->ReadBufferResource(m_PassIndex, name, state, offset, size) : RGBufferHandle{};
    }

    RGTextureHandle RenderGraphBuilder::WriteTexture(Identity name,
                                                     const RGTextureDesc& desc,
                                                     RHI::ResourceState state,
                                                     RHI::ResourceState finalState)
    {
        return m_Graph ? m_Graph->WriteTextureResource(m_PassIndex, name, desc, state, finalState) : RGTextureHandle{};
    }

    RGBufferHandle RenderGraphBuilder::WriteBuffer(Identity name,
                                                   const RGBufferDesc& desc,
                                                   RHI::ResourceState state,
                                                   RHI::ResourceState finalState)
    {
        return WriteBuffer(name, desc, state, finalState, 0, 0);
    }

    RGTextureHandle RenderGraphBuilder::WriteTextureAttachment(Identity name,
                                                               const RGTextureDesc& desc,
                                                               RGAttachmentKind kind,
                                                               RHI::AttachmentLoadOp loadOp,
                                                               RHI::AttachmentStoreOp storeOp,
                                                               RHI::ResourceState state,
                                                               RHI::ResourceState finalState)
    {
        return m_Graph ? m_Graph->WriteTextureAttachmentResource(m_PassIndex,
                                                                 name,
                                                                 desc,
                                                                 kind,
                                                                 loadOp,
                                                                 storeOp,
                                                                 state,
                                                                 finalState)
                       : RGTextureHandle{};
    }

    RGBufferHandle RenderGraphBuilder::WriteBuffer(Identity name,
                                                   const RGBufferDesc& desc,
                                                   RHI::ResourceState state,
                                                   RHI::ResourceState finalState,
                                                   uint64_t offset,
                                                   uint64_t size)
    {
        return m_Graph ? m_Graph->WriteBufferResource(m_PassIndex, name, desc, state, finalState, offset, size)
                       : RGBufferHandle{};
    }

    bool RenderGraphBuilder::TryGetTexture(Identity name, RGTextureHandle& outHandle) const
    {
        if (!m_Graph)
        {
            outHandle = RGTextureHandle{};
            return false;
        }

        return m_Graph->TryGetTextureResource(name, outHandle);
    }

    bool RenderGraphBuilder::TryGetBuffer(Identity name, RGBufferHandle& outHandle) const
    {
        if (!m_Graph)
        {
            outHandle = RGBufferHandle{};
            return false;
        }

        return m_Graph->TryGetBufferResource(name, outHandle);
    }

    bool RenderGraphBuilder::ExportTexture(Identity name, RGTextureHandle handle)
    {
        return m_Graph ? m_Graph->ExportTextureResource(name, handle) : false;
    }

    void RenderGraphBuilder::PreserveInsertionOrder()
    {
        if (m_Graph)
        {
            m_Graph->AddPreserveInsertionOrder(m_PassIndex);
        }
    }

    bool RenderGraphBuilder::AddDependency(uint32_t beforePassIndex, uint32_t afterPassIndex)
    {
        return m_Graph ? m_Graph->AddDependency(beforePassIndex, afterPassIndex) : false;
    }

    RenderGraphResources::RenderGraphResources(RenderGraph* graph)
        : m_Graph(graph)
    {
    }

    RHI::TexturePtr RenderGraphResources::GetTexture(RGResourceHandle handle)
    {
        return m_Graph ? m_Graph->ResolveTexture(handle) : nullptr;
    }

    RHI::BufferPtr RenderGraphResources::GetBuffer(RGResourceHandle handle)
    {
        return m_Graph ? m_Graph->ResolveBuffer(handle) : nullptr;
    }

    RHI::ITexture* RenderGraphResources::GetTextureRaw(RGResourceHandle handle)
    {
        return m_Graph ? m_Graph->ResolveTextureRaw(handle) : nullptr;
    }

    RHI::IBuffer* RenderGraphResources::GetBufferRaw(RGResourceHandle handle)
    {
        return m_Graph ? m_Graph->ResolveBufferRaw(handle) : nullptr;
    }

    RHI::TexturePtr RenderGraphResources::GetTexture(RGTextureHandle handle)
    {
        return m_Graph ? m_Graph->ResolveTexture(handle) : nullptr;
    }

    RHI::BufferPtr RenderGraphResources::GetBuffer(RGBufferHandle handle)
    {
        return m_Graph ? m_Graph->ResolveBuffer(handle) : nullptr;
    }

    RHI::ITexture* RenderGraphResources::GetTextureRaw(RGTextureHandle handle)
    {
        return m_Graph ? m_Graph->ResolveTextureRaw(handle) : nullptr;
    }

    RHI::IBuffer* RenderGraphResources::GetBufferRaw(RGBufferHandle handle)
    {
        return m_Graph ? m_Graph->ResolveBufferRaw(handle) : nullptr;
    }

    RenderGraph::~RenderGraph()
    {
        Shutdown();
    }

    bool RenderGraph::Initialize(RHI::TransientResourcePool* transientPool)
    {
        if (m_bInitialized)
        {
            m_TransientPool = transientPool;
            return true;
        }

        m_TransientPool = transientPool;
        m_bInitialized = true;
        return true;
    }

    void RenderGraph::Shutdown()
    {
        if (!m_bInitialized)
        {
            return;
        }

        Reset();
        m_TransientPool = nullptr;
        m_bInitialized = false;
    }

    void RenderGraph::BeginFrame(uint64_t frameIndex)
    {
        m_FrameIndex = frameIndex;
        Reset();
    }

    void RenderGraph::Reset()
    {
        m_Passes.clear();
        m_ExplicitDependencies.clear();
        ClearCompileData();
    }

    uint32_t RenderGraph::AddPass(IRenderGraphPass* pass)
    {
        if (!pass)
        {
            NORVES_LOG_ERROR("RenderGraph", "Cannot add null pass");
            return RGInvalidPassIndex;
        }

        const uint32_t passIndex = static_cast<uint32_t>(m_Passes.size());
        m_Passes.push_back(pass);
        return passIndex;
    }

    bool RenderGraph::AddDependency(uint32_t beforePassIndex, uint32_t afterPassIndex)
    {
        if (!ValidatePassIndex(beforePassIndex) || !ValidatePassIndex(afterPassIndex))
        {
            NORVES_LOG_ERROR("RenderGraph",
                             "Invalid pass dependency: before=%u after=%u passCount=%zu",
                             beforePassIndex,
                             afterPassIndex,
                             m_Passes.size());
            return false;
        }

        RGPassDependency dependency;
        dependency.BeforePassIndex = beforePassIndex;
        dependency.AfterPassIndex = afterPassIndex;
        m_ExplicitDependencies.push_back(dependency);
        return true;
    }

    bool RenderGraph::Compile()
    {
        return CompileInternal(nullptr);
    }

    bool RenderGraph::Compile(const ViewRenderContext& context)
    {
        return CompileInternal(&context);
    }

    bool RenderGraph::CompileInternal(const ViewRenderContext* context)
    {
        NORVES_STAT_SCOPE_CATEGORY("RenderGraph.Compile", "RenderGraph");
        ClearCompileData();
        m_PassDeclarations.resize(m_Passes.size());

        for (uint32_t passIndex = 0; passIndex < m_Passes.size(); ++passIndex)
        {
            IRenderGraphPass* pass = m_Passes[passIndex];
            if (!pass)
            {
                NORVES_LOG_ERROR("RenderGraph", "Null pass at index %u", passIndex);
                ClearCompiledProducts();
                return false;
            }

            RenderGraphBuilder builder(this, passIndex, context);
            pass->Declare(builder);
        }

        if (m_bHasGraphError)
        {
            ClearCompiledProducts();
            return false;
        }

        if (!ValidatePassAccesses())
        {
            ClearCompiledProducts();
            return false;
        }

        Container::VariableArray<Container::VariableArray<uint32_t>> adjacency;
        Container::VariableArray<uint32_t> indegree;
        if (!BuildDependencyGraph(adjacency, indegree))
        {
            ClearCompiledProducts();
            return false;
        }

        if (!TopologicalSort(adjacency, indegree))
        {
            ClearCompiledProducts();
            return false;
        }

        BuildResourceLifetimes();
        if (!ValidateResourceLifetimes())
        {
            ClearCompiledProducts();
            return false;
        }

        BuildTransientAllocationPlan();
        BuildBarriers();
        m_LastCompiledBarrierCount = static_cast<uint32_t>(m_CompiledBarriers.size());
        NORVES_STAT_ADD(NorvesLib::Debug::StatsManager::Get().GetRenderingStats().RenderGraphBarrierCount,
                        m_LastCompiledBarrierCount);
        m_bCompiled = true;
        return true;
    }

    bool RenderGraph::Execute(ViewRenderContext& context)
    {
        return ExecuteWithResult(context).bSuccess;
    }

    RenderGraphExecutionResult RenderGraph::ExecuteWithResult(ViewRenderContext& context)
    {
        NORVES_STAT_SCOPE_CATEGORY("RenderGraph.Execute", "RenderGraph");
        m_LastExecutedPassCount = 0;
        m_LastTransientAcquireCount = 0;
        RenderGraphExecutionResult result;
        if (!m_bCompiled)
        {
            NORVES_LOG_ERROR("RenderGraph", "Execute called before successful Compile");
            m_LastExecutionResult = result;
            return result;
        }

        RenderGraphResources resources(this);
        uint32_t nextBarrierIndex = 0;
        for (uint32_t orderIndex = 0; orderIndex < m_CompiledPassOrder.size(); ++orderIndex)
        {
            const uint32_t passIndex = m_CompiledPassOrder[orderIndex];
            if (!ValidatePassIndex(passIndex))
            {
                result.ExecutedPassCount = m_LastExecutedPassCount;
                m_LastExecutionResult = result;
                return result;
            }

            IRenderGraphPass* pass = m_Passes[passIndex];
            if (!pass)
            {
                result.ExecutedPassCount = m_LastExecutedPassCount;
                m_LastExecutionResult = result;
                return result;
            }

            FlushPendingFrameCommands(context);
            if (!AcquireTransientResourcesForOrderIndex(orderIndex))
            {
                result.ExecutedPassCount = m_LastExecutedPassCount;
                m_LastExecutionResult = result;
                return result;
            }

            while (nextBarrierIndex < m_CompiledBarriers.size() &&
                   m_CompiledBarriers[nextBarrierIndex].CompiledOrderIndex == orderIndex)
            {
                if (!ExecuteBarrier(m_CompiledBarriers[nextBarrierIndex], context))
                {
                    result.ExecutedPassCount = m_LastExecutedPassCount;
                    m_LastExecutionResult = result;
                    return result;
                }
                ++nextBarrierIndex;
            }

            FlushPendingFrameCommands(context);
#if NORVES_ENABLE_RENDERGRAPH_DUMP
            const bool bDebugMarkers = ShouldEmitDebugMarkers() && context.CommandList != nullptr;
            if (bDebugMarkers)
            {
                context.CommandList->BeginDebugMarker(pass->GetName());
            }
#endif
            pass->Execute(resources, context);
#if NORVES_ENABLE_RENDERGRAPH_DUMP
            if (bDebugMarkers)
            {
                context.CommandList->EndDebugMarker();
            }
#endif
            ++m_LastExecutedPassCount;
            FlushPendingFrameCommands(context);
        }

        for (const auto& exportEntry : m_TextureExports)
        {
            RHI::TexturePtr texture = ResolveTexture(exportEntry.second);
            if (!texture)
            {
                NORVES_LOG_ERROR("RenderGraph", "Failed to resolve exported texture");
                result.ExecutedPassCount = m_LastExecutedPassCount;
                result.TextureOutputs.clear();
                m_LastExecutionResult = result;
                return result;
            }

            RGTextureOutput output;
            output.Name = exportEntry.first;
            output.Handle = exportEntry.second;
            output.Texture = texture;
            result.TextureOutputs[exportEntry.first] = output;
        }

        result.bSuccess = true;
        result.ExecutedPassCount = m_LastExecutedPassCount;
        m_LastExecutionResult = result;
        if (ShouldWriteDebugDump())
        {
            WriteDebugDumpFiles();
        }
        return result;
    }

    void RenderGraph::SetDebugDumpOptions(const RGDumpOptions& options)
    {
        m_DebugDumpOptions = options;
        m_DebugDumpFrameCount = 0;
    }

    RGResourceHandle RenderGraph::CreateTextureResource(const RGTextureDesc& desc)
    {
        RGResourceRecord record;
        record.Kind = RGResourceKind::Texture;
        record.Lifetime = RGResourceLifetime::Transient;
        record.TextureDesc = desc;
        record.InitialState = RHI::ResourceState::Undefined;
        record.DebugName = desc.DebugName;
        record.Handle.Index = static_cast<uint32_t>(m_Resources.size());
        record.Handle.Generation = m_HandleGeneration;
        m_Resources.push_back(record);
        return record.Handle;
    }

    RGResourceHandle RenderGraph::CreateBufferResource(const RGBufferDesc& desc)
    {
        RGResourceRecord record;
        record.Kind = RGResourceKind::Buffer;
        record.Lifetime = RGResourceLifetime::Transient;
        record.BufferDesc = desc;
        record.InitialState = RHI::ResourceState::Undefined;
        record.DebugName = desc.DebugName;
        record.Handle.Index = static_cast<uint32_t>(m_Resources.size());
        record.Handle.Generation = m_HandleGeneration;
        m_Resources.push_back(record);
        return record.Handle;
    }

    RGResourceHandle RenderGraph::CreateLogicalResource(const char* debugName)
    {
        RGResourceRecord record;
        record.Kind = RGResourceKind::Logical;
        record.Lifetime = RGResourceLifetime::Logical;
        record.DebugName = debugName;
        record.Handle.Index = static_cast<uint32_t>(m_Resources.size());
        record.Handle.Generation = m_HandleGeneration;
        m_Resources.push_back(record);
        return record.Handle;
    }

    RGResourceHandle RenderGraph::ImportTextureResource(RHI::TexturePtr texture,
                                                        RHI::ResourceState initialState,
                                                        const char* debugName)
    {
        if (!texture)
        {
            NORVES_LOG_ERROR("RenderGraph", "Cannot import null texture");
            return RGResourceHandle{};
        }

        RGResourceRecord record;
        record.Kind = RGResourceKind::Texture;
        record.Lifetime = RGResourceLifetime::Imported;
        record.ImportedTexture = texture;
        record.ExecutionTexture = texture;
        record.InitialState = initialState;
        record.DebugName = debugName;
        record.TextureDesc.Width = texture->GetWidth();
        record.TextureDesc.Height = texture->GetHeight();
        record.TextureDesc.Depth = texture->GetDepth();
        record.TextureDesc.MipLevels = texture->GetMipLevels();
        record.TextureDesc.ArraySize = texture->GetArraySize();
        record.TextureDesc.Format = texture->GetFormat();
        record.TextureDesc.Usage = texture->GetUsage();
        record.TextureDesc.bIsCubemap = texture->IsCubemap();
        record.TextureDesc.DebugName = debugName;
        record.Handle.Index = static_cast<uint32_t>(m_Resources.size());
        record.Handle.Generation = m_HandleGeneration;
        m_Resources.push_back(record);
        return record.Handle;
    }

    RGResourceHandle RenderGraph::ImportBufferResource(RHI::BufferPtr buffer,
                                                       RHI::ResourceState initialState,
                                                       const char* debugName)
    {
        if (!buffer)
        {
            NORVES_LOG_ERROR("RenderGraph", "Cannot import null buffer");
            return RGResourceHandle{};
        }

        RGResourceRecord record;
        record.Kind = RGResourceKind::Buffer;
        record.Lifetime = RGResourceLifetime::Imported;
        record.ImportedBuffer = buffer;
        record.ExecutionBuffer = buffer;
        record.InitialState = initialState;
        record.DebugName = debugName;
        record.BufferDesc.Size = buffer->GetSize();
        record.BufferDesc.Usage = buffer->GetUsage();
        record.BufferDesc.DebugName = debugName;
        record.Handle.Index = static_cast<uint32_t>(m_Resources.size());
        record.Handle.Generation = m_HandleGeneration;
        m_Resources.push_back(record);
        return record.Handle;
    }

    RGTextureHandle RenderGraph::CreateTextureResourceHandle(const RGTextureDesc& desc)
    {
        return RGTextureHandle(CreateTextureResource(desc));
    }

    RGBufferHandle RenderGraph::CreateBufferResourceHandle(const RGBufferDesc& desc)
    {
        return RGBufferHandle(CreateBufferResource(desc));
    }

    bool RenderGraph::PublishTextureResource(Identity name, RGTextureHandle handle)
    {
        if (!name.IsValid())
        {
            NORVES_LOG_ERROR("RenderGraph", "Cannot publish texture with invalid name");
            MarkGraphError();
            return false;
        }

        if (!ValidateTextureHandle(handle))
        {
            NORVES_LOG_ERROR("RenderGraph", "Cannot publish invalid texture handle");
            MarkGraphError();
            return false;
        }

        auto existing = m_NamedResources.find(name);
        if (existing != m_NamedResources.end())
        {
            NORVES_LOG_ERROR("RenderGraph", "Duplicate named texture publish");
            MarkGraphError();
            return false;
        }

        RGNamedResource resource;
        resource.Kind = RGResourceKind::Texture;
        resource.CurrentHead = handle.ToResourceHandle();
        resource.Version = 0;
        m_NamedResources[name] = resource;
        return true;
    }

    bool RenderGraph::PublishBufferResource(Identity name, RGBufferHandle handle)
    {
        if (!name.IsValid())
        {
            NORVES_LOG_ERROR("RenderGraph", "Cannot publish buffer with invalid name");
            MarkGraphError();
            return false;
        }

        if (!ValidateBufferHandle(handle))
        {
            NORVES_LOG_ERROR("RenderGraph", "Cannot publish invalid buffer handle");
            MarkGraphError();
            return false;
        }

        auto existing = m_NamedResources.find(name);
        if (existing != m_NamedResources.end())
        {
            NORVES_LOG_ERROR("RenderGraph", "Duplicate named buffer publish");
            MarkGraphError();
            return false;
        }

        RGNamedResource resource;
        resource.Kind = RGResourceKind::Buffer;
        resource.CurrentHead = handle.ToResourceHandle();
        resource.Version = 0;
        m_NamedResources[name] = resource;
        return true;
    }

    RGTextureHandle RenderGraph::ReadTextureResource(uint32_t passIndex,
                                                     Identity name,
                                                     RHI::ResourceState state)
    {
        const RGNamedResource* resource = nullptr;
        if (!ValidateNamedResource(name, RGResourceKind::Texture, resource))
        {
            MarkGraphError();
            return RGTextureHandle{};
        }

        RGTextureHandle handle(resource->CurrentHead);
        AddAccess(passIndex, handle.ToResourceHandle(), RGAccessMode::Read, state, state, name);
        RGPassAccess& access = m_PassDeclarations[passIndex].Accesses.back();
        access.bNamedResourceVersionBeforeValid = true;
        access.NamedResourceVersionBefore = resource->Version;
        return handle;
    }

    bool RenderGraph::TryReadTextureResource(uint32_t passIndex,
                                             Identity name,
                                             RGTextureHandle& outHandle,
                                             RHI::ResourceState state)
    {
        outHandle = RGTextureHandle{};
        if (!name.IsValid())
        {
            NORVES_LOG_ERROR("RenderGraph", "Invalid named resource identity");
            MarkGraphError();
            return false;
        }

        auto existing = m_NamedResources.find(name);
        if (existing == m_NamedResources.end())
        {
            return false;
        }

        if (existing->second.Kind != RGResourceKind::Texture)
        {
            NORVES_LOG_ERROR("RenderGraph", "Named resource type mismatch");
            MarkGraphError();
            return false;
        }

        RGTextureHandle handle(existing->second.CurrentHead);
        if (!ValidateTextureHandle(handle))
        {
            NORVES_LOG_ERROR("RenderGraph", "Named resource current head is invalid");
            MarkGraphError();
            return false;
        }

        outHandle = handle;
        AddAccess(passIndex, handle.ToResourceHandle(), RGAccessMode::Read, state, state, name);
        RGPassAccess& access = m_PassDeclarations[passIndex].Accesses.back();
        access.bNamedResourceVersionBeforeValid = true;
        access.NamedResourceVersionBefore = existing->second.Version;
        return true;
    }

    bool RenderGraph::TryLoadStoreColorAttachmentResource(uint32_t passIndex,
                                                          Identity name,
                                                          RGTextureHandle& outHandle,
                                                          RHI::AttachmentLoadOp loadOp,
                                                          RHI::AttachmentStoreOp storeOp,
                                                          RHI::ResourceState state,
                                                          RHI::ResourceState finalState)
    {
        return TryUseAttachmentResource(passIndex,
                                        name,
                                        outHandle,
                                        RGAttachmentKind::Color,
                                        RGAttachmentMutability::Write,
                                        loadOp,
                                        storeOp,
                                        state,
                                        finalState);
    }

    bool RenderGraph::TryUseAttachmentResource(uint32_t passIndex,
                                               Identity name,
                                               RGTextureHandle& outHandle,
                                               RGAttachmentKind kind,
                                               RGAttachmentMutability mutability,
                                               RHI::AttachmentLoadOp loadOp,
                                               RHI::AttachmentStoreOp storeOp,
                                               RHI::ResourceState state,
                                               RHI::ResourceState finalState)
    {
        outHandle = RGTextureHandle{};
        if (!name.IsValid())
        {
            NORVES_LOG_ERROR("RenderGraph", "Invalid named resource identity");
            MarkGraphError();
            return false;
        }

        auto existing = m_NamedResources.find(name);
        if (existing == m_NamedResources.end())
        {
            return false;
        }

        if (existing->second.Kind != RGResourceKind::Texture)
        {
            NORVES_LOG_ERROR("RenderGraph", "Named attachment type mismatch");
            MarkGraphError();
            return false;
        }

        RGTextureHandle handle(existing->second.CurrentHead);
        if (!ValidateTextureHandle(handle))
        {
            NORVES_LOG_ERROR("RenderGraph", "Named color attachment current head is invalid");
            MarkGraphError();
            return false;
        }

        const uint32_t versionBefore = existing->second.Version;
        uint32_t versionAfter = versionBefore;
        if (mutability == RGAttachmentMutability::Write)
        {
            ++existing->second.Version;
            versionAfter = existing->second.Version;
        }
        outHandle = handle;
        AddAttachmentAccess(passIndex,
                            handle.ToResourceHandle(),
                            kind,
                            mutability,
                            loadOp,
                            storeOp,
                            state,
                            finalState,
                            name);
        RGPassAccess& access = m_PassDeclarations[passIndex].Accesses.back();
        access.bNamedResourceVersionBeforeValid = true;
        access.NamedResourceVersionBefore = versionBefore;
        if (mutability == RGAttachmentMutability::Write)
        {
            access.bNamedResourceVersionAfterValid = true;
            access.NamedResourceVersionAfter = versionAfter;
            access.bMutatesCurrentHead = true;
        }
        return true;
    }

    RGBufferHandle RenderGraph::ReadBufferResource(uint32_t passIndex,
                                                   Identity name,
                                                   RHI::ResourceState state,
                                                   uint64_t offset,
                                                   uint64_t size)
    {
        const RGNamedResource* resource = nullptr;
        if (!ValidateNamedResource(name, RGResourceKind::Buffer, resource))
        {
            MarkGraphError();
            return RGBufferHandle{};
        }

        RGBufferHandle handle(resource->CurrentHead);
        const uint32_t accessCountBefore = passIndex < m_PassDeclarations.size()
                                               ? static_cast<uint32_t>(m_PassDeclarations[passIndex].Accesses.size())
                                               : 0u;
        AddAccess(passIndex, handle.ToResourceHandle(), RGAccessMode::Read, state, state, name, offset, size);
        if (passIndex < m_PassDeclarations.size() &&
            m_PassDeclarations[passIndex].Accesses.size() > accessCountBefore)
        {
            RGPassAccess& access = m_PassDeclarations[passIndex].Accesses.back();
            access.bNamedResourceVersionBeforeValid = true;
            access.NamedResourceVersionBefore = resource->Version;
        }
        return handle;
    }

    RGTextureHandle RenderGraph::WriteTextureResource(uint32_t passIndex,
                                                      Identity name,
                                                      const RGTextureDesc& desc,
                                                      RHI::ResourceState state,
                                                      RHI::ResourceState finalState)
    {
        if (!name.IsValid())
        {
            NORVES_LOG_ERROR("RenderGraph", "Cannot write texture with invalid name");
            MarkGraphError();
            return RGTextureHandle{};
        }

        auto existing = m_NamedResources.find(name);
        uint32_t version = 0;
        bool bVersionBeforeValid = false;
        uint32_t versionBefore = 0;
        if (existing != m_NamedResources.end())
        {
            if (existing->second.Kind != RGResourceKind::Texture)
            {
                NORVES_LOG_ERROR("RenderGraph", "Named resource texture write type mismatch");
                MarkGraphError();
                return RGTextureHandle{};
            }
            bVersionBeforeValid = true;
            versionBefore = existing->second.Version;
            version = versionBefore + 1;
        }

        RGTextureHandle handle = CreateTextureResourceHandle(desc);
        RGNamedResource resource;
        resource.Kind = RGResourceKind::Texture;
        resource.CurrentHead = handle.ToResourceHandle();
        resource.Version = version;
        m_NamedResources[name] = resource;

        AddAccess(passIndex, handle.ToResourceHandle(), RGAccessMode::Write, state, finalState, name);
        RGPassAccess& access = m_PassDeclarations[passIndex].Accesses.back();
        access.bNamedResourceVersionBeforeValid = bVersionBeforeValid;
        access.NamedResourceVersionBefore = versionBefore;
        access.bNamedResourceVersionAfterValid = true;
        access.NamedResourceVersionAfter = version;
        access.bCreatesNewHead = true;
        return handle;
    }

    RGTextureHandle RenderGraph::WriteTextureAttachmentResource(uint32_t passIndex,
                                                                Identity name,
                                                                const RGTextureDesc& desc,
                                                                RGAttachmentKind kind,
                                                                RHI::AttachmentLoadOp loadOp,
                                                                RHI::AttachmentStoreOp storeOp,
                                                                RHI::ResourceState state,
                                                                RHI::ResourceState finalState)
    {
        if (!name.IsValid())
        {
            NORVES_LOG_ERROR("RenderGraph", "Cannot write texture attachment with invalid name");
            MarkGraphError();
            return RGTextureHandle{};
        }

        auto existing = m_NamedResources.find(name);
        uint32_t version = 0;
        bool bVersionBeforeValid = false;
        uint32_t versionBefore = 0;
        if (existing != m_NamedResources.end())
        {
            if (existing->second.Kind != RGResourceKind::Texture)
            {
                NORVES_LOG_ERROR("RenderGraph", "Named resource texture attachment write type mismatch");
                MarkGraphError();
                return RGTextureHandle{};
            }
            bVersionBeforeValid = true;
            versionBefore = existing->second.Version;
            version = versionBefore + 1;
        }

        RGTextureHandle handle = CreateTextureResourceHandle(desc);
        RGNamedResource resource;
        resource.Kind = RGResourceKind::Texture;
        resource.CurrentHead = handle.ToResourceHandle();
        resource.Version = version;
        m_NamedResources[name] = resource;

        AddAttachmentAccess(passIndex,
                            handle.ToResourceHandle(),
                            kind,
                            RGAttachmentMutability::Write,
                            loadOp,
                            storeOp,
                            state,
                            finalState,
                            name);
        RGPassAccess& access = m_PassDeclarations[passIndex].Accesses.back();
        access.bNamedResourceVersionBeforeValid = bVersionBeforeValid;
        access.NamedResourceVersionBefore = versionBefore;
        access.bNamedResourceVersionAfterValid = true;
        access.NamedResourceVersionAfter = version;
        access.bCreatesNewHead = true;
        return handle;
    }

    RGBufferHandle RenderGraph::WriteBufferResource(uint32_t passIndex,
                                                    Identity name,
                                                    const RGBufferDesc& desc,
                                                    RHI::ResourceState state,
                                                    RHI::ResourceState finalState,
                                                    uint64_t offset,
                                                    uint64_t size)
    {
        if (!name.IsValid())
        {
            NORVES_LOG_ERROR("RenderGraph", "Cannot write buffer with invalid name");
            MarkGraphError();
            return RGBufferHandle{};
        }

        auto existing = m_NamedResources.find(name);
        uint32_t version = 0;
        bool bVersionBeforeValid = false;
        uint32_t versionBefore = 0;
        if (existing != m_NamedResources.end())
        {
            if (existing->second.Kind != RGResourceKind::Buffer)
            {
                NORVES_LOG_ERROR("RenderGraph", "Named resource buffer write type mismatch");
                MarkGraphError();
                return RGBufferHandle{};
            }
            bVersionBeforeValid = true;
            versionBefore = existing->second.Version;
            version = versionBefore + 1;
        }

        RGBufferHandle handle = CreateBufferResourceHandle(desc);
        RGNamedResource resource;
        resource.Kind = RGResourceKind::Buffer;
        resource.CurrentHead = handle.ToResourceHandle();
        resource.Version = version;
        m_NamedResources[name] = resource;

        const uint32_t accessCountBefore = passIndex < m_PassDeclarations.size()
                                               ? static_cast<uint32_t>(m_PassDeclarations[passIndex].Accesses.size())
                                               : 0u;
        AddAccess(passIndex, handle.ToResourceHandle(), RGAccessMode::Write, state, finalState, name, offset, size);
        if (passIndex < m_PassDeclarations.size() &&
            m_PassDeclarations[passIndex].Accesses.size() > accessCountBefore)
        {
            RGPassAccess& access = m_PassDeclarations[passIndex].Accesses.back();
            access.bNamedResourceVersionBeforeValid = bVersionBeforeValid;
            access.NamedResourceVersionBefore = versionBefore;
            access.bNamedResourceVersionAfterValid = true;
            access.NamedResourceVersionAfter = version;
            access.bCreatesNewHead = true;
        }
        return handle;
    }

    bool RenderGraph::TryGetTextureResource(Identity name, RGTextureHandle& outHandle)
    {
        outHandle = RGTextureHandle{};
        const RGNamedResource* resource = nullptr;
        if (!ValidateNamedResource(name, RGResourceKind::Texture, resource))
        {
            MarkGraphError();
            return false;
        }

        outHandle = RGTextureHandle(resource->CurrentHead);
        return true;
    }

    bool RenderGraph::TryGetBufferResource(Identity name, RGBufferHandle& outHandle)
    {
        outHandle = RGBufferHandle{};
        const RGNamedResource* resource = nullptr;
        if (!ValidateNamedResource(name, RGResourceKind::Buffer, resource))
        {
            MarkGraphError();
            return false;
        }

        outHandle = RGBufferHandle(resource->CurrentHead);
        return true;
    }

    bool RenderGraph::ExportTextureResource(Identity name, RGTextureHandle handle)
    {
        if (!name.IsValid())
        {
            NORVES_LOG_ERROR("RenderGraph", "Cannot export texture with invalid name");
            MarkGraphError();
            return false;
        }

        if (!ValidateTextureHandle(handle))
        {
            NORVES_LOG_ERROR("RenderGraph", "Cannot export invalid texture handle");
            MarkGraphError();
            return false;
        }

        m_TextureExports[name] = handle;
        return true;
    }

    void RenderGraph::AddAccess(uint32_t passIndex,
                                RGResourceHandle handle,
                                RGAccessMode mode,
                                RHI::ResourceState state,
                                RHI::ResourceState finalState,
                                Identity namedResourceIdentity,
                                uint64_t bufferOffset,
                                uint64_t bufferSize)
    {
        if (!ValidatePassIndex(passIndex))
        {
            return;
        }

        uint64_t normalizedOffset = 0;
        uint64_t normalizedSize = 0;
        if (!NormalizeBufferRange(handle, bufferOffset, bufferSize, normalizedOffset, normalizedSize))
        {
            MarkGraphError();
            return;
        }

        RGPassAccess access;
        access.Resource = handle;
        access.Mode = mode;
        access.State = state;
        access.FinalState = finalState;
        access.BufferOffset = normalizedOffset;
        access.BufferSize = normalizedSize;
        access.NamedResourceIdentity = namedResourceIdentity;
        m_PassDeclarations[passIndex].Accesses.push_back(access);
    }

    void RenderGraph::AddAttachmentAccess(uint32_t passIndex,
                                          RGResourceHandle handle,
                                          RGAttachmentKind kind,
                                          RGAttachmentMutability mutability,
                                          RHI::AttachmentLoadOp loadOp,
                                          RHI::AttachmentStoreOp storeOp,
                                          RHI::ResourceState state,
                                          RHI::ResourceState finalState,
                                          Identity namedResourceIdentity)
    {
        if (!ValidatePassIndex(passIndex))
        {
            return;
        }

        RGPassAccess access;
        access.Resource = handle;
        access.Mode = mutability == RGAttachmentMutability::ReadOnly ? RGAccessMode::Read : RGAccessMode::Write;
        access.State = state;
        access.FinalState = finalState;
        access.bAttachment = true;
        access.AttachmentKind = kind;
        access.AttachmentMutability = mutability;
        access.LoadOp = loadOp;
        access.StoreOp = storeOp;
        access.NamedResourceIdentity = namedResourceIdentity;
        m_PassDeclarations[passIndex].Accesses.push_back(access);
    }

    bool RenderGraph::NormalizeBufferRange(RGResourceHandle handle,
                                           uint64_t offset,
                                           uint64_t size,
                                           uint64_t& outOffset,
                                           uint64_t& outSize) const
    {
        outOffset = 0;
        outSize = 0;

        if (!ValidateHandle(handle))
        {
            return true;
        }

        const RGResourceRecord& resource = m_Resources[handle.Index];
        if (resource.Kind != RGResourceKind::Buffer)
        {
            if (offset != 0 || size != 0)
            {
                NORVES_LOG_ERROR("RenderGraph",
                                 "Buffer range was provided for non-buffer resource '%s'",
                                 resource.DebugName ? resource.DebugName : "<unnamed>");
                return false;
            }
            return true;
        }

        const uint64_t resourceSize = resource.Lifetime == RGResourceLifetime::Imported && resource.ImportedBuffer
                                          ? resource.ImportedBuffer->GetSize()
                                          : resource.BufferDesc.Size;
        if (offset > resourceSize)
        {
            NORVES_LOG_ERROR("RenderGraph",
                             "Buffer range offset exceeds resource size for '%s'",
                             resource.DebugName ? resource.DebugName : "<unnamed>");
            return false;
        }

        const uint64_t normalizedSize = size == 0 ? resourceSize - offset : size;
        if (normalizedSize > UINT64_MAX - offset)
        {
            NORVES_LOG_ERROR("RenderGraph",
                             "Buffer range overflow for '%s'",
                             resource.DebugName ? resource.DebugName : "<unnamed>");
            return false;
        }

        if (offset + normalizedSize > resourceSize)
        {
            NORVES_LOG_ERROR("RenderGraph",
                             "Buffer range exceeds resource size for '%s'",
                             resource.DebugName ? resource.DebugName : "<unnamed>");
            return false;
        }

        outOffset = offset;
        outSize = normalizedSize;
        return true;
    }

    void RenderGraph::AddPreserveInsertionOrder(uint32_t passIndex)
    {
        if (ValidatePassIndex(passIndex))
        {
            m_PassDeclarations[passIndex].bPreserveInsertionOrder = true;
        }
    }

    uint32_t RenderGraph::GetDeclaredPassAccessCount(uint32_t passIndex) const
    {
        if (!ValidatePassIndex(passIndex) || passIndex >= m_PassDeclarations.size())
        {
            return 0;
        }

        return static_cast<uint32_t>(m_PassDeclarations[passIndex].Accesses.size());
    }

    bool RenderGraph::TryGetNamedResourceVersion(Identity name, uint32_t& outVersion) const
    {
        outVersion = 0;
        if (!name.IsValid())
        {
            return false;
        }

        auto existing = m_NamedResources.find(name);
        if (existing == m_NamedResources.end())
        {
            return false;
        }

        outVersion = existing->second.Version;
        return true;
    }

    bool RenderGraph::TryGetCompiledResourceLifetime(RGResourceHandle handle,
                                                     RGCompiledResourceLifetime& outLifetime) const
    {
        outLifetime = RGCompiledResourceLifetime{};
        if (!ValidateHandle(handle))
        {
            return false;
        }

        for (const RGCompiledResourceLifetime& lifetime : m_CompiledResourceLifetimes)
        {
            if (lifetime.Resource == handle)
            {
                outLifetime = lifetime;
                return true;
            }
        }

        return false;
    }

    bool RenderGraph::TryGetDeclaredPassAccess(uint32_t passIndex,
                                               uint32_t accessIndex,
                                               RGResourceHandle& outResource,
                                               RGAccessMode& outMode,
                                               RHI::ResourceState& outState,
                                               RHI::ResourceState& outFinalState,
                                               bool* outColorAttachmentLoadStore,
                                               RHI::AttachmentLoadOp* outLoadOp,
                                               RHI::AttachmentStoreOp* outStoreOp,
                                               RGAttachmentKind* outAttachmentKind,
                                               RGAttachmentMutability* outAttachmentMutability) const
    {
        if (!ValidatePassIndex(passIndex) || passIndex >= m_PassDeclarations.size())
        {
            return false;
        }

        const RGPassDeclaration& declaration = m_PassDeclarations[passIndex];
        if (accessIndex >= declaration.Accesses.size())
        {
            return false;
        }

        const RGPassAccess& access = declaration.Accesses[accessIndex];
        outResource = access.Resource;
        outMode = access.Mode;
        outState = access.State;
        outFinalState = access.FinalState;
        if (outColorAttachmentLoadStore)
        {
            *outColorAttachmentLoadStore = access.bAttachment &&
                                           access.AttachmentKind == RGAttachmentKind::Color &&
                                           access.AttachmentMutability == RGAttachmentMutability::Write;
        }
        if (outLoadOp)
        {
            *outLoadOp = access.LoadOp;
        }
        if (outStoreOp)
        {
            *outStoreOp = access.StoreOp;
        }
        if (outAttachmentKind)
        {
            *outAttachmentKind = access.AttachmentKind;
        }
        if (outAttachmentMutability)
        {
            *outAttachmentMutability = access.AttachmentMutability;
        }
        return true;
    }

    bool RenderGraph::ValidatePassAccesses() const
    {
        for (uint32_t passIndex = 0; passIndex < m_PassDeclarations.size(); ++passIndex)
        {
            const RGPassDeclaration& declaration = m_PassDeclarations[passIndex];
            for (uint32_t accessIndex = 0; accessIndex < declaration.Accesses.size(); ++accessIndex)
            {
                const RGPassAccess& access = declaration.Accesses[accessIndex];
                for (uint32_t otherIndex = accessIndex + 1;
                     otherIndex < declaration.Accesses.size();
                     ++otherIndex)
                {
                    const RGPassAccess& otherAccess = declaration.Accesses[otherIndex];
                    if (access.Resource != otherAccess.Resource)
                    {
                        continue;
                    }

                    if (access.bAttachment || otherAccess.bAttachment)
                    {
                        NORVES_LOG_ERROR("RenderGraph",
                                         "Attachment access must be the only same-pass access for resource %u in pass %u",
                                         access.Resource.Index,
                                         passIndex);
                        return false;
                    }

                    if (access.Mode != otherAccess.Mode)
                    {
                        NORVES_LOG_ERROR("RenderGraph",
                                         "Same-pass read/write access is invalid for resource %u in pass %u",
                                         access.Resource.Index,
                                         passIndex);
                        return false;
                    }
                }
            }
        }

        return true;
    }

    bool RenderGraph::ValidatePassIndex(uint32_t passIndex) const
    {
        return !IsInvalidPassIndex(passIndex) && passIndex < m_Passes.size();
    }

    bool RenderGraph::ValidateHandle(RGResourceHandle handle) const
    {
        if (!handle.IsValid() ||
            handle.Generation != m_HandleGeneration ||
            handle.Index >= m_Resources.size())
        {
            return false;
        }

        return m_Resources[handle.Index].Kind != RGResourceKind::Invalid;
    }

    bool RenderGraph::ValidateTextureHandle(RGTextureHandle handle) const
    {
        const RGResourceHandle resource = handle.ToResourceHandle();
        return ValidateHandle(resource) && m_Resources[resource.Index].Kind == RGResourceKind::Texture;
    }

    bool RenderGraph::ValidateBufferHandle(RGBufferHandle handle) const
    {
        const RGResourceHandle resource = handle.ToResourceHandle();
        return ValidateHandle(resource) && m_Resources[resource.Index].Kind == RGResourceKind::Buffer;
    }

    bool RenderGraph::ValidateNamedResource(Identity name,
                                            RGResourceKind expectedKind,
                                            const RGNamedResource*& outResource) const
    {
        outResource = nullptr;
        if (!name.IsValid())
        {
            NORVES_LOG_ERROR("RenderGraph", "Invalid named resource identity");
            return false;
        }

        auto existing = m_NamedResources.find(name);
        if (existing == m_NamedResources.end())
        {
            NORVES_LOG_ERROR("RenderGraph", "Named resource read failed because the name is not registered");
            return false;
        }

        if (existing->second.Kind != expectedKind)
        {
            NORVES_LOG_ERROR("RenderGraph", "Named resource type mismatch");
            return false;
        }

        if (!ValidateHandle(existing->second.CurrentHead))
        {
            NORVES_LOG_ERROR("RenderGraph", "Named resource current head is invalid");
            return false;
        }

        outResource = &existing->second;
        return true;
    }

    void RenderGraph::MarkGraphError()
    {
        m_bHasGraphError = true;
        m_bCompiled = false;
    }

    bool RenderGraph::AddEdge(uint32_t beforePassIndex,
                              uint32_t afterPassIndex,
                              Container::VariableArray<Container::VariableArray<uint32_t>>& adjacency,
                              Container::VariableArray<uint32_t>& indegree) const
    {
        if (beforePassIndex == afterPassIndex)
        {
            return true;
        }

        if (!ValidatePassIndex(beforePassIndex) || !ValidatePassIndex(afterPassIndex))
        {
            NORVES_LOG_ERROR("RenderGraph",
                             "Invalid edge: before=%u after=%u passCount=%zu",
                             beforePassIndex,
                             afterPassIndex,
                             m_Passes.size());
            return false;
        }

        for (uint32_t existingAfter : adjacency[beforePassIndex])
        {
            if (existingAfter == afterPassIndex)
            {
                return true;
            }
        }

        adjacency[beforePassIndex].push_back(afterPassIndex);
        ++indegree[afterPassIndex];
        return true;
    }

    bool RenderGraph::BuildDependencyGraph(Container::VariableArray<Container::VariableArray<uint32_t>>& adjacency,
                                           Container::VariableArray<uint32_t>& indegree) const
    {
        adjacency.clear();
        indegree.clear();
        adjacency.resize(m_Passes.size());
        indegree.resize(m_Passes.size(), 0);

        for (const RGPassDependency& dependency : m_ExplicitDependencies)
        {
            if (!AddEdge(dependency.BeforePassIndex, dependency.AfterPassIndex, adjacency, indegree))
            {
                return false;
            }
        }

        for (uint32_t passIndex = 0; passIndex < m_PassDeclarations.size(); ++passIndex)
        {
            if (m_PassDeclarations[passIndex].bPreserveInsertionOrder && passIndex > 0)
            {
                if (!AddEdge(passIndex - 1, passIndex, adjacency, indegree))
                {
                    return false;
                }
            }
        }

        Container::VariableArray<uint32_t> lastWritePass;
        lastWritePass.resize(m_Resources.size(), RGInvalidPassIndex);

        Container::VariableArray<Container::VariableArray<uint32_t>> readsSinceLastWrite;
        readsSinceLastWrite.resize(m_Resources.size());

        for (uint32_t passIndex = 0; passIndex < m_PassDeclarations.size(); ++passIndex)
        {
            const RGPassDeclaration& declaration = m_PassDeclarations[passIndex];
            for (const RGPassAccess& access : declaration.Accesses)
            {
                if (!ValidateHandle(access.Resource))
                {
                    NORVES_LOG_ERROR("RenderGraph",
                                     "Invalid resource handle in pass %u",
                                     passIndex);
                    return false;
                }

                const uint32_t resourceIndex = access.Resource.Index;
                if (access.Mode == RGAccessMode::Read)
                {
                    const uint32_t lastWrite = lastWritePass[resourceIndex];
                    if (!IsInvalidPassIndex(lastWrite))
                    {
                        if (!AddEdge(lastWrite, passIndex, adjacency, indegree))
                        {
                            return false;
                        }
                    }
                    readsSinceLastWrite[resourceIndex].push_back(passIndex);
                    continue;
                }

                const uint32_t lastWrite = lastWritePass[resourceIndex];
                if (!IsInvalidPassIndex(lastWrite))
                {
                    if (!AddEdge(lastWrite, passIndex, adjacency, indegree))
                    {
                        return false;
                    }
                }

                for (uint32_t readPassIndex : readsSinceLastWrite[resourceIndex])
                {
                    if (!AddEdge(readPassIndex, passIndex, adjacency, indegree))
                    {
                        return false;
                    }
                }
                readsSinceLastWrite[resourceIndex].clear();
                lastWritePass[resourceIndex] = passIndex;
            }
        }

        return true;
    }

    bool RenderGraph::TopologicalSort(const Container::VariableArray<Container::VariableArray<uint32_t>>& adjacency,
                                      Container::VariableArray<uint32_t> indegree)
    {
        m_CompiledPassOrder.clear();
        Container::VariableArray<bool> bProcessed;
        bProcessed.resize(m_Passes.size(), false);

        for (uint32_t processedCount = 0; processedCount < m_Passes.size(); ++processedCount)
        {
            uint32_t selectedPass = RGInvalidPassIndex;
            for (uint32_t passIndex = 0; passIndex < m_Passes.size(); ++passIndex)
            {
                if (!bProcessed[passIndex] && indegree[passIndex] == 0)
                {
                    selectedPass = passIndex;
                    break;
                }
            }

            if (IsInvalidPassIndex(selectedPass))
            {
                NORVES_LOG_ERROR("RenderGraph", "RenderGraph dependency cycle detected");
                m_CompiledPassOrder.clear();
                return false;
            }

            bProcessed[selectedPass] = true;
            m_CompiledPassOrder.push_back(selectedPass);

            for (uint32_t afterPassIndex : adjacency[selectedPass])
            {
                if (indegree[afterPassIndex] > 0)
                {
                    --indegree[afterPassIndex];
                }
            }
        }

        return true;
    }

    void RenderGraph::BuildResourceLifetimes()
    {
        m_CompiledResourceLifetimes.clear();
        m_CompiledResourceLifetimes.reserve(m_Resources.size());

        for (const RGResourceRecord& resource : m_Resources)
        {
            RGCompiledResourceLifetime lifetime;
            lifetime.Resource = resource.Handle;
            lifetime.Kind = resource.Kind;
            lifetime.Lifetime = resource.Lifetime;
            lifetime.DebugName = resource.DebugName;
            m_CompiledResourceLifetimes.push_back(lifetime);
        }

        for (uint32_t orderIndex = 0; orderIndex < m_CompiledPassOrder.size(); ++orderIndex)
        {
            const uint32_t passIndex = m_CompiledPassOrder[orderIndex];
            if (passIndex >= m_PassDeclarations.size())
            {
                continue;
            }

            const RGPassDeclaration& declaration = m_PassDeclarations[passIndex];
            for (const RGPassAccess& access : declaration.Accesses)
            {
                if (!ValidateHandle(access.Resource) ||
                    access.Resource.Index >= m_CompiledResourceLifetimes.size())
                {
                    continue;
                }

                RGCompiledResourceLifetime& lifetime = m_CompiledResourceLifetimes[access.Resource.Index];
                if (!lifetime.bHasUse)
                {
                    lifetime.FirstUsePassIndex = passIndex;
                    lifetime.FirstUseOrderIndex = orderIndex;
                    lifetime.bHasUse = true;
                }

                lifetime.LastUsePassIndex = passIndex;
                lifetime.LastUseOrderIndex = orderIndex;
            }
        }

        for (const auto& exportEntry : m_TextureExports)
        {
            const RGResourceHandle resource = exportEntry.second.ToResourceHandle();
            if (!ValidateHandle(resource) ||
                resource.Index >= m_CompiledResourceLifetimes.size())
            {
                continue;
            }

            RGCompiledResourceLifetime& lifetime = m_CompiledResourceLifetimes[resource.Index];
            lifetime.bExported = true;
            lifetime.bPinnedUntilGraphEnd = true;
        }

        const uint32_t graphEndOrderIndex = static_cast<uint32_t>(m_CompiledPassOrder.size());
        for (RGCompiledResourceLifetime& lifetime : m_CompiledResourceLifetimes)
        {
            if (lifetime.bPinnedUntilGraphEnd)
            {
                lifetime.LifetimeEndOrderIndex = graphEndOrderIndex;
                continue;
            }

            if (lifetime.bHasUse)
            {
                lifetime.LifetimeEndOrderIndex = lifetime.LastUseOrderIndex;
                continue;
            }

            NORVES_LOG_WARNING("RenderGraph",
                               "Resource '%s' was declared but not used or exported",
                               lifetime.DebugName ? lifetime.DebugName : "<unnamed>");
        }
    }

    bool RenderGraph::ValidateResourceLifetimes() const
    {
        Container::VariableArray<bool> bHasReadableContents;
        bHasReadableContents.reserve(m_Resources.size());
        for (const RGResourceRecord& resource : m_Resources)
        {
            bHasReadableContents.push_back(resource.Lifetime == RGResourceLifetime::Imported ||
                                           resource.Lifetime == RGResourceLifetime::Logical);
        }

        for (uint32_t orderIndex = 0; orderIndex < m_CompiledPassOrder.size(); ++orderIndex)
        {
            const uint32_t passIndex = m_CompiledPassOrder[orderIndex];
            if (passIndex >= m_PassDeclarations.size())
            {
                NORVES_LOG_ERROR("RenderGraph", "Invalid compiled pass order entry %u", passIndex);
                return false;
            }

            const RGPassDeclaration& declaration = m_PassDeclarations[passIndex];
            for (const RGPassAccess& access : declaration.Accesses)
            {
                if (!ValidateHandle(access.Resource))
                {
                    NORVES_LOG_ERROR("RenderGraph", "Invalid resource handle in compiled lifetime validation");
                    return false;
                }

                const RGResourceRecord& resource = m_Resources[access.Resource.Index];
                if (resource.Lifetime == RGResourceLifetime::Logical)
                {
                    continue;
                }

                const bool bRequiresReadableContents =
                    access.Mode == RGAccessMode::Read ||
                    (access.bAttachment && access.LoadOp == RHI::AttachmentLoadOp::Load);
                if (resource.Lifetime == RGResourceLifetime::Transient &&
                    bRequiresReadableContents &&
                    !bHasReadableContents[access.Resource.Index])
                {
                    NORVES_LOG_ERROR("RenderGraph",
                                     "Transient resource '%s' is read before it has been written",
                                     resource.DebugName ? resource.DebugName : "<unnamed>");
                    return false;
                }

                if (access.Mode == RGAccessMode::Write)
                {
                    bHasReadableContents[access.Resource.Index] = true;
                }
            }
        }

        return true;
    }

    void RenderGraph::BuildTransientAllocationPlan()
    {
        m_TransientAllocationPlan.clear();

        for (const RGCompiledResourceLifetime& lifetime : m_CompiledResourceLifetimes)
        {
            if (!lifetime.bHasUse ||
                lifetime.Lifetime != RGResourceLifetime::Transient ||
                (lifetime.Kind != RGResourceKind::Texture && lifetime.Kind != RGResourceKind::Buffer))
            {
                continue;
            }

            RGTransientAllocationStep step;
            step.Resource = lifetime.Resource;
            step.Kind = lifetime.Kind;
            step.AcquireBeforePassIndex = lifetime.FirstUsePassIndex;
            step.AcquireBeforeOrderIndex = lifetime.FirstUseOrderIndex;
            step.ReleaseAfterPassIndex = lifetime.bPinnedUntilGraphEnd ? RGInvalidPassIndex : lifetime.LastUsePassIndex;
            step.ReleaseAfterOrderIndex = lifetime.LifetimeEndOrderIndex;
            step.bPinnedUntilGraphEnd = lifetime.bPinnedUntilGraphEnd;
            step.DebugName = lifetime.DebugName;
            m_TransientAllocationPlan.push_back(step);
        }
    }

    void RenderGraph::BuildBarriers()
    {
        m_CompiledBarriers.clear();
        m_LastCompiledBarrierCount = 0;

        Container::VariableArray<RHI::ResourceState> currentStates;
        currentStates.reserve(m_Resources.size());
        for (const RGResourceRecord& resource : m_Resources)
        {
            currentStates.push_back(resource.InitialState);
        }

        for (uint32_t orderIndex = 0; orderIndex < m_CompiledPassOrder.size(); ++orderIndex)
        {
            const uint32_t passIndex = m_CompiledPassOrder[orderIndex];
            const RGPassDeclaration& declaration = m_PassDeclarations[passIndex];
            for (const RGPassAccess& access : declaration.Accesses)
            {
                if (!ValidateHandle(access.Resource))
                {
                    continue;
                }

                const RGResourceRecord& resource = m_Resources[access.Resource.Index];
                if (resource.Kind == RGResourceKind::Logical)
                {
                    continue;
                }

                if (currentStates[access.Resource.Index] != access.State)
                {
                    RGCompiledBarrier barrier;
                    barrier.Kind = resource.Kind == RGResourceKind::Texture ? RGBarrierKind::Texture : RGBarrierKind::Buffer;
                    barrier.Resource = access.Resource;
                    barrier.BeforeState = currentStates[access.Resource.Index];
                    barrier.AfterState = access.State;
                    barrier.PassName = passIndex < m_Passes.size() && m_Passes[passIndex]
                                           ? m_Passes[passIndex]->GetName()
                                           : nullptr;
                    barrier.ResourceDebugName = resource.DebugName;
                    barrier.NamedResourceIdentity = access.NamedResourceIdentity;
                    barrier.PassIndex = passIndex;
                    barrier.CompiledOrderIndex = orderIndex;
                    if (resource.Kind == RGResourceKind::Buffer)
                    {
                        barrier.BufferOffset = access.BufferOffset;
                        barrier.BufferSize = access.BufferSize;
                    }
                    m_CompiledBarriers.push_back(barrier);
                }
                currentStates[access.Resource.Index] = access.FinalState;
            }
        }
        m_LastCompiledBarrierCount = static_cast<uint32_t>(m_CompiledBarriers.size());
    }

    bool RenderGraph::AcquireTransientResourcesForOrderIndex(uint32_t orderIndex)
    {
        for (const RGTransientAllocationStep& step : m_TransientAllocationPlan)
        {
            if (step.AcquireBeforeOrderIndex != orderIndex)
            {
                continue;
            }

            if (!ValidateHandle(step.Resource))
            {
                NORVES_LOG_ERROR("RenderGraph", "Invalid transient allocation step resource");
                return false;
            }

            RGResourceRecord& resource = m_Resources[step.Resource.Index];
            if (!AcquireTransientResource(resource))
            {
                NORVES_LOG_ERROR("RenderGraph",
                                 "Failed to acquire transient resource '%s' before pass %u",
                                 step.DebugName ? step.DebugName : "<unnamed>",
                                 step.AcquireBeforePassIndex);
                return false;
            }
            ++m_LastTransientAcquireCount;
            NORVES_STAT_INC(NorvesLib::Debug::StatsManager::Get().GetRenderingStats().RenderGraphTransientAcquireCount);
        }

        return true;
    }

    bool RenderGraph::AcquireTransientResource(RGResourceRecord& resource)
    {
        if (resource.Lifetime != RGResourceLifetime::Transient)
        {
            return true;
        }

        if (resource.Kind == RGResourceKind::Texture)
        {
            return ResolveTexture(resource.Handle) != nullptr;
        }

        if (resource.Kind == RGResourceKind::Buffer)
        {
            return ResolveBuffer(resource.Handle) != nullptr;
        }

        return true;
    }

    bool RenderGraph::ExecuteBarrier(const RGCompiledBarrier& barrier, ViewRenderContext& context)
    {
        if (!context.CommandList)
        {
            NORVES_LOG_ERROR("RenderGraph", "Cannot execute barrier without command list");
            return false;
        }

        if (barrier.Kind == RGBarrierKind::Texture)
        {
            RHI::TexturePtr texture = ResolveTexture(barrier.Resource);
            if (!texture)
            {
                NORVES_LOG_ERROR("RenderGraph", "Texture barrier resource is null");
                return false;
            }

            context.CommandList->TextureBarrier(texture,
                                                barrier.BeforeState,
                                                barrier.AfterState,
                                                barrier.MipLevel,
                                                barrier.ArrayIndex,
                                                barrier.MipCount,
                                                barrier.ArrayCount);
            return true;
        }

        RHI::BufferPtr buffer = ResolveBuffer(barrier.Resource);
        if (!buffer)
        {
            NORVES_LOG_ERROR("RenderGraph", "Buffer barrier resource is null");
            return false;
        }

        context.CommandList->BufferBarrier(buffer,
                                           barrier.BeforeState,
                                           barrier.AfterState,
                                           barrier.BufferOffset,
                                           barrier.BufferSize);
        return true;
    }

    void RenderGraph::FlushPendingFrameCommands(ViewRenderContext& context) const
    {
        if (!context.Renderer || !context.PendingFrameCommands || !context.CommandList)
        {
            return;
        }

        if (context.PendingFrameCommands->empty())
        {
            return;
        }

        context.Renderer->ExecuteFrameCommands(*context.PendingFrameCommands, context.CommandList);
        context.PendingFrameCommands->clear();
    }

    RHI::TexturePtr RenderGraph::ResolveTexture(RGResourceHandle handle)
    {
        if (!ValidateHandle(handle))
        {
            return nullptr;
        }

        RGResourceRecord& resource = m_Resources[handle.Index];
        if (resource.Kind != RGResourceKind::Texture)
        {
            return nullptr;
        }

        if (resource.Lifetime == RGResourceLifetime::Imported)
        {
            return resource.ImportedTexture;
        }

        if (resource.ExecutionTexture)
        {
            return resource.ExecutionTexture;
        }

        if (!m_TransientPool)
        {
            NORVES_LOG_ERROR("RenderGraph", "TransientResourcePool is null for texture '%s'",
                             resource.DebugName ? resource.DebugName : "<unnamed>");
            return nullptr;
        }

        RHI::ITexture* texture = nullptr;
        if (HasResourceUsage(resource.TextureDesc.Usage, RHI::ResourceUsage::DepthStencil))
        {
            texture = m_TransientPool->AcquireDepthStencil(resource.TextureDesc.Width,
                                                           resource.TextureDesc.Height,
                                                           resource.TextureDesc.Format,
                                                           resource.TextureDesc.DebugName);
        }
        else
        {
            texture = m_TransientPool->AcquireRenderTarget(resource.TextureDesc.Width,
                                                           resource.TextureDesc.Height,
                                                           resource.TextureDesc.Format,
                                                           resource.TextureDesc.DebugName);
        }

        if (!texture)
        {
            NORVES_LOG_ERROR("RenderGraph", "Failed to allocate transient texture '%s'",
                             resource.DebugName ? resource.DebugName : "<unnamed>");
            return nullptr;
        }

        resource.ExecutionTexture = RHI::TexturePtr(texture, [](RHI::ITexture*) {});
        return resource.ExecutionTexture;
    }

    RHI::BufferPtr RenderGraph::ResolveBuffer(RGResourceHandle handle)
    {
        if (!ValidateHandle(handle))
        {
            return nullptr;
        }

        RGResourceRecord& resource = m_Resources[handle.Index];
        if (resource.Kind != RGResourceKind::Buffer)
        {
            return nullptr;
        }

        if (resource.Lifetime == RGResourceLifetime::Imported)
        {
            return resource.ImportedBuffer;
        }

        if (resource.ExecutionBuffer)
        {
            return resource.ExecutionBuffer;
        }

        if (!m_TransientPool)
        {
            NORVES_LOG_ERROR("RenderGraph", "TransientResourcePool is null for buffer '%s'",
                             resource.DebugName ? resource.DebugName : "<unnamed>");
            return nullptr;
        }

        RHI::IBuffer* buffer = m_TransientPool->AcquireBuffer(resource.BufferDesc.Size,
                                                              resource.BufferDesc.Usage,
                                                              resource.BufferDesc.DebugName);
        if (!buffer)
        {
            NORVES_LOG_ERROR("RenderGraph", "Failed to allocate transient buffer '%s'",
                             resource.DebugName ? resource.DebugName : "<unnamed>");
            return nullptr;
        }

        resource.ExecutionBuffer = RHI::BufferPtr(buffer, [](RHI::IBuffer*) {});
        return resource.ExecutionBuffer;
    }

    RHI::ITexture* RenderGraph::ResolveTextureRaw(RGResourceHandle handle)
    {
        RHI::TexturePtr texture = ResolveTexture(handle);
        return texture ? texture.get() : nullptr;
    }

    RHI::IBuffer* RenderGraph::ResolveBufferRaw(RGResourceHandle handle)
    {
        RHI::BufferPtr buffer = ResolveBuffer(handle);
        return buffer ? buffer.get() : nullptr;
    }

    RHI::TexturePtr RenderGraph::ResolveTexture(RGTextureHandle handle)
    {
        if (!ValidateTextureHandle(handle))
        {
            return nullptr;
        }

        return ResolveTexture(handle.ToResourceHandle());
    }

    RHI::BufferPtr RenderGraph::ResolveBuffer(RGBufferHandle handle)
    {
        if (!ValidateBufferHandle(handle))
        {
            return nullptr;
        }

        return ResolveBuffer(handle.ToResourceHandle());
    }

    RHI::ITexture* RenderGraph::ResolveTextureRaw(RGTextureHandle handle)
    {
        RHI::TexturePtr texture = ResolveTexture(handle);
        return texture ? texture.get() : nullptr;
    }

    RHI::IBuffer* RenderGraph::ResolveBufferRaw(RGBufferHandle handle)
    {
        RHI::BufferPtr buffer = ResolveBuffer(handle);
        return buffer ? buffer.get() : nullptr;
    }

    void RenderGraph::ClearCompileData()
    {
        m_Resources.clear();
        m_PassDeclarations.clear();
        ClearCompiledProducts();
        m_NamedResources.clear();
        m_TextureExports.clear();
        m_LastExecutionResult = RenderGraphExecutionResult{};
        m_LastExecutedPassCount = 0;
        m_LastTransientAcquireCount = 0;
        m_bCompiled = false;
        m_bHasGraphError = false;

        ++m_HandleGeneration;
        if (m_HandleGeneration == 0)
        {
            m_HandleGeneration = 1;
        }
    }

    void RenderGraph::ClearCompiledProducts()
    {
        m_CompiledPassOrder.clear();
        m_CompiledBarriers.clear();
        m_CompiledResourceLifetimes.clear();
        m_TransientAllocationPlan.clear();
        m_LastCompiledBarrierCount = 0;
        m_bCompiled = false;
    }

    bool RenderGraph::ShouldBuildDebugDump() const
    {
#if NORVES_ENABLE_RENDERGRAPH_DUMP
        return m_DebugDumpOptions.bEnabled;
#else
        return false;
#endif
    }

    bool RenderGraph::ShouldWriteDebugDump() const
    {
#if NORVES_ENABLE_RENDERGRAPH_DUMP
        if (!ShouldBuildDebugDump() ||
            !m_DebugDumpOptions.bWriteFiles)
        {
            return false;
        }

        if (m_DebugDumpOptions.MaxFrameCount == 0)
        {
            return false;
        }

        return m_DebugDumpFrameCount < m_DebugDumpOptions.MaxFrameCount;
#else
        return false;
#endif
    }

    bool RenderGraph::ShouldEmitDebugMarkers() const
    {
#if NORVES_ENABLE_RENDERGRAPH_DUMP
        return m_DebugDumpOptions.bEnabled && m_DebugDumpOptions.bDebugMarkers;
#else
        return false;
#endif
    }

    bool RenderGraph::HasResourceUsage(RHI::ResourceUsage usage, RHI::ResourceUsage flag) const
    {
        return static_cast<uint32_t>(usage & flag) != 0;
    }

} // namespace NorvesLib::Core::Rendering

#include "Rendering/RenderGraph/RenderGraph.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/ITexture.h"
#include "RHI/TransientResourcePool.h"
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
        if (m_Graph)
        {
            m_Graph->AddAccess(m_PassIndex, handle, RGAccessMode::Read, state, state);
        }
    }

    void RenderGraphBuilder::Write(RGResourceHandle handle, RHI::ResourceState state)
    {
        if (m_Graph)
        {
            m_Graph->AddAccess(m_PassIndex, handle, RGAccessMode::Write, state, state);
        }
    }

    void RenderGraphBuilder::Write(RGResourceHandle handle,
                                   RHI::ResourceState state,
                                   RHI::ResourceState finalState)
    {
        if (m_Graph)
        {
            m_Graph->AddAccess(m_PassIndex, handle, RGAccessMode::Write, state, finalState);
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

    RGBufferHandle RenderGraphBuilder::ReadBuffer(Identity name, RHI::ResourceState state)
    {
        return m_Graph ? m_Graph->ReadBufferResource(m_PassIndex, name, state) : RGBufferHandle{};
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
        return m_Graph ? m_Graph->WriteBufferResource(m_PassIndex, name, desc, state, finalState) : RGBufferHandle{};
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
        ClearCompileData();
        m_PassDeclarations.resize(m_Passes.size());

        for (uint32_t passIndex = 0; passIndex < m_Passes.size(); ++passIndex)
        {
            IRenderGraphPass* pass = m_Passes[passIndex];
            if (!pass)
            {
                NORVES_LOG_ERROR("RenderGraph", "Null pass at index %u", passIndex);
                return false;
            }

            RenderGraphBuilder builder(this, passIndex, context);
            pass->Declare(builder);
        }

        if (m_bHasGraphError)
        {
            m_CompiledPassOrder.clear();
            m_CompiledBarriers.clear();
            return false;
        }

        Container::VariableArray<Container::VariableArray<uint32_t>> adjacency;
        Container::VariableArray<uint32_t> indegree;
        if (!BuildDependencyGraph(adjacency, indegree))
        {
            return false;
        }

        if (!TopologicalSort(adjacency, indegree))
        {
            return false;
        }

        BuildBarriers();
        m_bCompiled = true;
        return true;
    }

    bool RenderGraph::Execute(ViewRenderContext& context)
    {
        return ExecuteWithResult(context).bSuccess;
    }

    RenderGraphExecutionResult RenderGraph::ExecuteWithResult(ViewRenderContext& context)
    {
        m_LastExecutedPassCount = 0;
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
            pass->Execute(resources, context);
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
        return result;
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
        AddAccess(passIndex, handle.ToResourceHandle(), RGAccessMode::Read, state, state);
        return handle;
    }

    RGBufferHandle RenderGraph::ReadBufferResource(uint32_t passIndex,
                                                   Identity name,
                                                   RHI::ResourceState state)
    {
        const RGNamedResource* resource = nullptr;
        if (!ValidateNamedResource(name, RGResourceKind::Buffer, resource))
        {
            MarkGraphError();
            return RGBufferHandle{};
        }

        RGBufferHandle handle(resource->CurrentHead);
        AddAccess(passIndex, handle.ToResourceHandle(), RGAccessMode::Read, state, state);
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
        if (existing != m_NamedResources.end())
        {
            if (existing->second.Kind != RGResourceKind::Texture)
            {
                NORVES_LOG_ERROR("RenderGraph", "Named resource texture write type mismatch");
                MarkGraphError();
                return RGTextureHandle{};
            }
            version = existing->second.Version + 1;
        }

        RGTextureHandle handle = CreateTextureResourceHandle(desc);
        RGNamedResource resource;
        resource.Kind = RGResourceKind::Texture;
        resource.CurrentHead = handle.ToResourceHandle();
        resource.Version = version;
        m_NamedResources[name] = resource;

        AddAccess(passIndex, handle.ToResourceHandle(), RGAccessMode::Write, state, finalState);
        return handle;
    }

    RGBufferHandle RenderGraph::WriteBufferResource(uint32_t passIndex,
                                                    Identity name,
                                                    const RGBufferDesc& desc,
                                                    RHI::ResourceState state,
                                                    RHI::ResourceState finalState)
    {
        if (!name.IsValid())
        {
            NORVES_LOG_ERROR("RenderGraph", "Cannot write buffer with invalid name");
            MarkGraphError();
            return RGBufferHandle{};
        }

        auto existing = m_NamedResources.find(name);
        uint32_t version = 0;
        if (existing != m_NamedResources.end())
        {
            if (existing->second.Kind != RGResourceKind::Buffer)
            {
                NORVES_LOG_ERROR("RenderGraph", "Named resource buffer write type mismatch");
                MarkGraphError();
                return RGBufferHandle{};
            }
            version = existing->second.Version + 1;
        }

        RGBufferHandle handle = CreateBufferResourceHandle(desc);
        RGNamedResource resource;
        resource.Kind = RGResourceKind::Buffer;
        resource.CurrentHead = handle.ToResourceHandle();
        resource.Version = version;
        m_NamedResources[name] = resource;

        AddAccess(passIndex, handle.ToResourceHandle(), RGAccessMode::Write, state, finalState);
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
                                RHI::ResourceState finalState)
    {
        if (!ValidatePassIndex(passIndex))
        {
            return;
        }

        RGPassAccess access;
        access.Resource = handle;
        access.Mode = mode;
        access.State = state;
        access.FinalState = finalState;
        m_PassDeclarations[passIndex].Accesses.push_back(access);
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

    bool RenderGraph::TryGetDeclaredPassAccess(uint32_t passIndex,
                                               uint32_t accessIndex,
                                               RGResourceHandle& outResource,
                                               RGAccessMode& outMode,
                                               RHI::ResourceState& outState,
                                               RHI::ResourceState& outFinalState) const
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

    void RenderGraph::BuildBarriers()
    {
        m_CompiledBarriers.clear();

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
                    barrier.PassIndex = passIndex;
                    barrier.CompiledOrderIndex = orderIndex;
                    if (resource.Kind == RGResourceKind::Buffer)
                    {
                        barrier.BufferSize = resource.BufferDesc.Size;
                    }
                    m_CompiledBarriers.push_back(barrier);
                }
                currentStates[access.Resource.Index] = access.FinalState;
            }
        }
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
        m_CompiledPassOrder.clear();
        m_CompiledBarriers.clear();
        m_NamedResources.clear();
        m_TextureExports.clear();
        m_LastExecutionResult = RenderGraphExecutionResult{};
        m_LastExecutedPassCount = 0;
        m_bCompiled = false;
        m_bHasGraphError = false;

        ++m_HandleGeneration;
        if (m_HandleGeneration == 0)
        {
            m_HandleGeneration = 1;
        }
    }

    bool RenderGraph::HasResourceUsage(RHI::ResourceUsage usage, RHI::ResourceUsage flag) const
    {
        return static_cast<uint32_t>(usage & flag) != 0;
    }

} // namespace NorvesLib::Core::Rendering

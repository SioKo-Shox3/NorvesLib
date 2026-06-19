#include "Rendering/RenderGraph/RenderGraph.h"
#include "Debug/DebugConfig.h"

#if NORVES_ENABLE_RENDERGRAPH_DUMP
#include <algorithm>
#include <filesystem>
#include <fstream>
#endif

namespace NorvesLib::Core::Rendering
{
#if NORVES_ENABLE_RENDERGRAPH_DUMP
    namespace
    {
        const char* ToString(RGResourceKind kind)
        {
            switch (kind)
            {
            case RGResourceKind::Texture:
                return "Texture";
            case RGResourceKind::Buffer:
                return "Buffer";
            case RGResourceKind::Logical:
                return "Logical";
            default:
                return "Invalid";
            }
        }

        const char* ToString(RGResourceLifetime lifetime)
        {
            switch (lifetime)
            {
            case RGResourceLifetime::Imported:
                return "Imported";
            case RGResourceLifetime::Transient:
                return "Transient";
            case RGResourceLifetime::Logical:
                return "Logical";
            default:
                return "Invalid";
            }
        }

        const char* ToString(RGAccessMode mode)
        {
            return mode == RGAccessMode::Write ? "Write" : "Read";
        }

        const char* ToString(RGBarrierKind kind)
        {
            return kind == RGBarrierKind::Buffer ? "Buffer" : "Texture";
        }

        const char* ToString(RGAttachmentKind kind)
        {
            return kind == RGAttachmentKind::DepthStencil ? "DepthStencil" : "Color";
        }

        const char* ToString(RGAttachmentMutability mutability)
        {
            return mutability == RGAttachmentMutability::ReadOnly ? "ReadOnly" : "Write";
        }

        const char* ToString(RHI::ResourceState state)
        {
            switch (state)
            {
            case RHI::ResourceState::Undefined:
                return "Undefined";
            case RHI::ResourceState::Common:
                return "Common";
            case RHI::ResourceState::VertexBuffer:
                return "VertexBuffer";
            case RHI::ResourceState::IndexBuffer:
                return "IndexBuffer";
            case RHI::ResourceState::ConstantBuffer:
                return "ConstantBuffer";
            case RHI::ResourceState::RenderTarget:
                return "RenderTarget";
            case RHI::ResourceState::DepthWrite:
                return "DepthWrite";
            case RHI::ResourceState::DepthRead:
                return "DepthRead";
            case RHI::ResourceState::ShaderResource:
                return "ShaderResource";
            case RHI::ResourceState::UnorderedAccess:
                return "UnorderedAccess";
            case RHI::ResourceState::IndirectArgument:
                return "IndirectArgument";
            case RHI::ResourceState::CopySource:
                return "CopySource";
            case RHI::ResourceState::CopyDest:
                return "CopyDest";
            case RHI::ResourceState::Present:
                return "Present";
            default:
                return "Unknown";
            }
        }

        const char* ToString(RHI::AttachmentLoadOp op)
        {
            switch (op)
            {
            case RHI::AttachmentLoadOp::Load:
                return "Load";
            case RHI::AttachmentLoadOp::Clear:
                return "Clear";
            default:
                return "DontCare";
            }
        }

        const char* ToString(RHI::AttachmentStoreOp op)
        {
            return op == RHI::AttachmentStoreOp::Store ? "Store" : "DontCare";
        }

        Container::String GetIdentityText(Identity identity)
        {
            return identity.IsValid() ? identity.ToString() : Container::String{};
        }

        const char* GetDebugName(const char* name)
        {
            return name ? name : "<unnamed>";
        }

        void AppendJsonEscaped(Container::StringBuilder& builder, const char* text)
        {
            builder.Append('"');
            if (text)
            {
                for (const char* cursor = text; *cursor != '\0'; ++cursor)
                {
                    const char ch = *cursor;
                    switch (ch)
                    {
                    case '"':
                        builder.Append("\\\"");
                        break;
                    case '\\':
                        builder.Append("\\\\");
                        break;
                    case '\n':
                        builder.Append("\\n");
                        break;
                    case '\r':
                        builder.Append("\\r");
                        break;
                    case '\t':
                        builder.Append("\\t");
                        break;
                    default:
                        builder.Append(ch);
                        break;
                    }
                }
            }
            builder.Append('"');
        }

        void AppendJsonEscaped(Container::StringBuilder& builder, const Container::String& text)
        {
            if (text.empty())
            {
                AppendJsonEscaped(builder, "");
                return;
            }

            AppendJsonEscaped(builder, text.c_str());
        }

        void AppendDotEscaped(Container::StringBuilder& builder, const char* text)
        {
            if (!text)
            {
                return;
            }

            for (const char* cursor = text; *cursor != '\0'; ++cursor)
            {
                const char ch = *cursor;
                if (ch == '"' || ch == '\\')
                {
                    builder.Append('\\');
                }
                builder.Append(ch);
            }
        }

        struct NamedDumpEntry
        {
            Identity Name;
            Container::String NameText;
            RGResourceHandle Resource;
            RGResourceKind Kind = RGResourceKind::Invalid;
            uint32_t Version = 0;
        };

        bool SortNamedDumpEntry(const NamedDumpEntry& lhs, const NamedDumpEntry& rhs)
        {
            if (lhs.NameText != rhs.NameText)
            {
                return lhs.NameText < rhs.NameText;
            }

            if (lhs.Name.GetHash() != rhs.Name.GetHash())
            {
                return lhs.Name.GetHash() < rhs.Name.GetHash();
            }

            return lhs.Resource.Index < rhs.Resource.Index;
        }
    } // namespace

    bool RenderGraph::TryGetDeclaredPassVersionDiagnostic(uint32_t passIndex,
                                                          uint32_t accessIndex,
                                                          RGNamedResourceVersionDiagnostic& outDiagnostic) const
    {
        outDiagnostic = RGNamedResourceVersionDiagnostic{};
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
        outDiagnostic.NamedResourceIdentity = access.NamedResourceIdentity;
        outDiagnostic.BeforeVersionValid = access.bNamedResourceVersionBeforeValid;
        outDiagnostic.BeforeVersion = access.NamedResourceVersionBefore;
        outDiagnostic.AfterVersion = access.bNamedResourceVersionAfterValid
                                         ? access.NamedResourceVersionAfter
                                         : (access.bNamedResourceVersionBeforeValid
                                                ? access.NamedResourceVersionBefore
                                                : 0u);
        outDiagnostic.bCreatesNewHead = access.bCreatesNewHead;
        outDiagnostic.bMutatesCurrentHead = access.bMutatesCurrentHead;
        return true;
    }

    RGDumpStrings RenderGraph::BuildDebugDump() const
    {
        RGDumpStrings strings;
        if (!ShouldBuildDebugDump())
        {
            return strings;
        }

        Container::VariableArray<NamedDumpEntry> namedResources;
        namedResources.reserve(m_NamedResources.size());
        for (const auto& entry : m_NamedResources)
        {
            NamedDumpEntry dumpEntry;
            dumpEntry.Name = entry.first;
            dumpEntry.NameText = GetIdentityText(entry.first);
            dumpEntry.Resource = entry.second.CurrentHead;
            dumpEntry.Kind = entry.second.Kind;
            dumpEntry.Version = entry.second.Version;
            namedResources.push_back(dumpEntry);
        }
        std::sort(namedResources.begin(), namedResources.end(), SortNamedDumpEntry);

        Container::VariableArray<NamedDumpEntry> textureExports;
        textureExports.reserve(m_TextureExports.size());
        for (const auto& entry : m_TextureExports)
        {
            NamedDumpEntry dumpEntry;
            dumpEntry.Name = entry.first;
            dumpEntry.NameText = GetIdentityText(entry.first);
            dumpEntry.Resource = entry.second.ToResourceHandle();
            dumpEntry.Kind = RGResourceKind::Texture;
            textureExports.push_back(dumpEntry);
        }
        std::sort(textureExports.begin(), textureExports.end(), SortNamedDumpEntry);

        auto getVersionDiagnostic = [&](const RGPassAccess& access)
        {
            RGNamedResourceVersionDiagnostic diagnostic;
            diagnostic.NamedResourceIdentity = access.NamedResourceIdentity;
            diagnostic.BeforeVersionValid = access.bNamedResourceVersionBeforeValid;
            diagnostic.BeforeVersion = access.NamedResourceVersionBefore;
            diagnostic.AfterVersion = access.bNamedResourceVersionAfterValid
                                          ? access.NamedResourceVersionAfter
                                          : (access.bNamedResourceVersionBeforeValid
                                                 ? access.NamedResourceVersionBefore
                                                 : 0u);
            diagnostic.bCreatesNewHead = access.bCreatesNewHead;
            diagnostic.bMutatesCurrentHead = access.bMutatesCurrentHead;
            return diagnostic;
        };

        auto appendPassListForResource = [&](Container::StringBuilder& builder,
                                             RGResourceHandle handle,
                                             bool bProducers)
        {
            bool bAppended = false;
            for (uint32_t passIndex = 0; passIndex < m_PassDeclarations.size(); ++passIndex)
            {
                const RGPassDeclaration& declaration = m_PassDeclarations[passIndex];
                for (const RGPassAccess& access : declaration.Accesses)
                {
                    const bool bMatchesUsage =
                        bProducers
                            ? access.Mode == RGAccessMode::Write
                            : (access.Mode == RGAccessMode::Read ||
                               (access.bAttachment && access.LoadOp == RHI::AttachmentLoadOp::Load));
                    if (access.Resource != handle || !bMatchesUsage)
                    {
                        continue;
                    }

                    if (bAppended)
                    {
                        builder.Append(", ");
                    }

                    builder.AppendFormat("%u", passIndex);
                    bAppended = true;
                }
            }

            if (!bAppended)
            {
                builder.Append("<none>");
            }
        };

        if (m_DebugDumpOptions.bText)
        {
            Container::StringBuilder text(4096);
            text.AppendFormat("RenderGraph Dump frame=%llu\n", static_cast<unsigned long long>(m_FrameIndex));
            text.AppendFormat("Stats: DeclaredPassCount=%u CompiledPassCount=%u ExecutedPassCount=%u TransientAcquireCount=%u BarrierCount=%u\n",
                              static_cast<uint32_t>(m_Passes.size()),
                              static_cast<uint32_t>(m_CompiledPassOrder.size()),
                              m_LastExecutedPassCount,
                              m_LastTransientAcquireCount,
                              m_LastCompiledBarrierCount);
            text.AppendLine("Pass Order:");
            for (uint32_t orderIndex = 0; orderIndex < m_CompiledPassOrder.size(); ++orderIndex)
            {
                const uint32_t passIndex = m_CompiledPassOrder[orderIndex];
                const char* passName = passIndex < m_Passes.size() && m_Passes[passIndex]
                                           ? m_Passes[passIndex]->GetName()
                                           : "<invalid>";
                text.AppendFormat("  [%u] pass=%u name=%s\n", orderIndex, passIndex, passName);
            }

            text.AppendLine("Resources:");
            for (const RGResourceRecord& resource : m_Resources)
            {
                text.AppendFormat("  r%u gen=%u kind=%s lifetime=%s name=%s",
                                  resource.Handle.Index,
                                  resource.Handle.Generation,
                                  ToString(resource.Kind),
                                  ToString(resource.Lifetime),
                                  GetDebugName(resource.DebugName));
                if (resource.Lifetime == RGResourceLifetime::Imported)
                {
                    text.Append(" producer=<imported>");
                }
                text.AppendLine();
                text.Append("    Producers: ");
                appendPassListForResource(text, resource.Handle, true);
                text.AppendLine();
                text.Append("    Consumers: ");
                appendPassListForResource(text, resource.Handle, false);
                text.AppendLine();
            }

            text.AppendLine("Accesses:");
            for (uint32_t passIndex = 0; passIndex < m_PassDeclarations.size(); ++passIndex)
            {
                const char* passName = passIndex < m_Passes.size() && m_Passes[passIndex]
                                           ? m_Passes[passIndex]->GetName()
                                           : "<invalid>";
                for (const RGPassAccess& access : m_PassDeclarations[passIndex].Accesses)
                {
                    Container::String nameText = GetIdentityText(access.NamedResourceIdentity);
                    const RGNamedResourceVersionDiagnostic diagnostic = getVersionDiagnostic(access);
                    text.AppendFormat("  pass=%u:%s %s r%u state=%s final=%s",
                                      passIndex,
                                      passName,
                                      ToString(access.Mode),
                                      access.Resource.Index,
                                      ToString(access.State),
                                      ToString(access.FinalState));
                    if (!nameText.empty())
                    {
                        text.AppendFormat(" name=%s", nameText.c_str());
                        text.AppendFormat(" BeforeVersionValid=%s BeforeVersion=%u AfterVersion=%u bCreatesNewHead=%s bMutatesCurrentHead=%s",
                                          diagnostic.BeforeVersionValid ? "true" : "false",
                                          diagnostic.BeforeVersion,
                                          diagnostic.AfterVersion,
                                          diagnostic.bCreatesNewHead ? "true" : "false",
                                          diagnostic.bMutatesCurrentHead ? "true" : "false");
                    }
                    if (access.bAttachment)
                    {
                        text.AppendFormat(" attachment=%s/%s load=%s store=%s",
                                          ToString(access.AttachmentKind),
                                          ToString(access.AttachmentMutability),
                                          ToString(access.LoadOp),
                                          ToString(access.StoreOp));
                    }
                    if (access.Mode == RGAccessMode::Read || (access.bAttachment && access.LoadOp == RHI::AttachmentLoadOp::Load))
                    {
                        text.Append(" Consumer");
                    }
                    if (access.Mode == RGAccessMode::Write)
                    {
                        text.Append(" Producer");
                    }
                    if (access.bCreatesNewHead)
                    {
                        text.Append(" createsNewHead");
                    }
                    if (access.bMutatesCurrentHead)
                    {
                        text.Append(" mutatesCurrentHead");
                    }
                    text.AppendLine();
                }
            }

            text.AppendLine("Named Resources:");
            for (const NamedDumpEntry& entry : namedResources)
            {
                text.AppendFormat("  %s hash=%llu r%u kind=%s version=%u\n",
                                  entry.NameText.c_str(),
                                  static_cast<unsigned long long>(entry.Name.GetHash()),
                                  entry.Resource.Index,
                                  ToString(entry.Kind),
                                  entry.Version);
            }

            text.AppendLine("Exports:");
            for (const NamedDumpEntry& entry : textureExports)
            {
                text.AppendFormat("  %s hash=%llu r%u\n",
                                  entry.NameText.c_str(),
                                  static_cast<unsigned long long>(entry.Name.GetHash()),
                                  entry.Resource.Index);
            }

            text.AppendLine("Lifetimes:");
            for (const RGCompiledResourceLifetime& lifetime : m_CompiledResourceLifetimes)
            {
                text.AppendFormat("  r%u firstPass=%u lastPass=%u firstOrder=%u lastOrder=%u endOrder=%u exported=%u pinned=%u name=%s\n",
                                  lifetime.Resource.Index,
                                  lifetime.FirstUsePassIndex,
                                  lifetime.LastUsePassIndex,
                                  lifetime.FirstUseOrderIndex,
                                  lifetime.LastUseOrderIndex,
                                  lifetime.LifetimeEndOrderIndex,
                                  lifetime.bExported ? 1u : 0u,
                                  lifetime.bPinnedUntilGraphEnd ? 1u : 0u,
                                  GetDebugName(lifetime.DebugName));
            }

            text.AppendLine("Barriers:");
            for (const RGCompiledBarrier& barrier : m_CompiledBarriers)
            {
                Container::String nameText = GetIdentityText(barrier.NamedResourceIdentity);
                text.AppendFormat("  order=%u pass=%u:%s kind=%s r%u %s->%s resource=%s",
                                  barrier.CompiledOrderIndex,
                                  barrier.PassIndex,
                                  GetDebugName(barrier.PassName),
                                  ToString(barrier.Kind),
                                  barrier.Resource.Index,
                                  ToString(barrier.BeforeState),
                                  ToString(barrier.AfterState),
                                  GetDebugName(barrier.ResourceDebugName));
                if (!nameText.empty())
                {
                    text.AppendFormat(" name=%s", nameText.c_str());
                }
                text.AppendLine();
            }

            text.AppendLine("Transient Allocations:");
            for (const RGTransientAllocationStep& step : m_TransientAllocationPlan)
            {
                text.AppendFormat("  r%u acquireOrder=%u releaseOrder=%u acquirePass=%u releasePass=%u pinned=%u name=%s\n",
                                  step.Resource.Index,
                                  step.AcquireBeforeOrderIndex,
                                  step.ReleaseAfterOrderIndex,
                                  step.AcquireBeforePassIndex,
                                  step.ReleaseAfterPassIndex,
                                  step.bPinnedUntilGraphEnd ? 1u : 0u,
                                  GetDebugName(step.DebugName));
            }
            strings.Text = text.ToString();
        }

        if (m_DebugDumpOptions.bDot)
        {
            Container::StringBuilder dot(4096);
            dot.AppendLine("digraph RenderGraph {");
            dot.AppendLine("  rankdir=LR;");
            for (uint32_t passIndex = 0; passIndex < m_Passes.size(); ++passIndex)
            {
                const char* passName = m_Passes[passIndex] ? m_Passes[passIndex]->GetName() : "<invalid>";
                dot.AppendFormat("  p%u [shape=box,label=\"", passIndex);
                AppendDotEscaped(dot, passName);
                dot.AppendFormat("\\npass %u\"];\n", passIndex);
            }

            for (const RGResourceRecord& resource : m_Resources)
            {
                dot.AppendFormat("  r%u [shape=ellipse,label=\"r%u\\n", resource.Handle.Index, resource.Handle.Index);
                AppendDotEscaped(dot, GetDebugName(resource.DebugName));
                dot.AppendFormat("\\n%s\\n%s", ToString(resource.Kind), ToString(resource.Lifetime));
                for (const RGCompiledResourceLifetime& lifetime : m_CompiledResourceLifetimes)
                {
                    if (lifetime.Resource == resource.Handle)
                    {
                        dot.AppendFormat("\\nlife %u-%u",
                                         lifetime.FirstUseOrderIndex,
                                         lifetime.LifetimeEndOrderIndex);
                        break;
                    }
                }
                dot.AppendLine("\"];");
            }

            for (uint32_t passIndex = 0; passIndex < m_PassDeclarations.size(); ++passIndex)
            {
                for (const RGPassAccess& access : m_PassDeclarations[passIndex].Accesses)
                {
                    const RGNamedResourceVersionDiagnostic diagnostic = getVersionDiagnostic(access);
                    if (access.Mode == RGAccessMode::Read || (access.bAttachment && access.LoadOp == RHI::AttachmentLoadOp::Load))
                    {
                        dot.AppendFormat("  r%u -> p%u [label=\"Consumer", access.Resource.Index, passIndex);
                        dot.AppendFormat(" v%u", diagnostic.AfterVersion);
                        dot.AppendLine("\"];");
                    }

                    if (access.Mode == RGAccessMode::Write)
                    {
                        dot.AppendFormat("  p%u -> r%u [label=\"Producer", passIndex, access.Resource.Index);
                        dot.AppendFormat(" v%u", diagnostic.AfterVersion);
                        dot.AppendLine("\"];");
                    }
                }
            }
            for (uint32_t barrierIndex = 0; barrierIndex < m_CompiledBarriers.size(); ++barrierIndex)
            {
                const RGCompiledBarrier& barrier = m_CompiledBarriers[barrierIndex];
                dot.AppendFormat("  b%u [shape=note,label=\"Barrier %u\\n%s\\n%s->%s\"];\n",
                                 barrierIndex,
                                 barrierIndex,
                                 ToString(barrier.Kind),
                                 ToString(barrier.BeforeState),
                                 ToString(barrier.AfterState));
                dot.AppendFormat("  r%u -> b%u [style=dotted];\n", barrier.Resource.Index, barrierIndex);
                dot.AppendFormat("  b%u -> p%u [style=dotted];\n", barrierIndex, barrier.PassIndex);
            }
            dot.AppendLine("}");
            strings.Dot = dot.ToString();
        }

        if (m_DebugDumpOptions.bJson)
        {
            Container::StringBuilder json(8192);
            json.Append("{");
            json.Append("\"frameIndex\":");
            json.AppendFormat("%llu", static_cast<unsigned long long>(m_FrameIndex));

            json.Append(",\"passes\":[");
            for (uint32_t orderIndex = 0; orderIndex < m_CompiledPassOrder.size(); ++orderIndex)
            {
                if (orderIndex > 0)
                {
                    json.Append(',');
                }
                const uint32_t passIndex = m_CompiledPassOrder[orderIndex];
                const char* passName = passIndex < m_Passes.size() && m_Passes[passIndex]
                                           ? m_Passes[passIndex]->GetName()
                                           : "<invalid>";
                json.Append("{\"index\":");
                json.AppendFormat("%u", passIndex);
                json.Append(",\"order\":");
                json.AppendFormat("%u", orderIndex);
                json.Append(",\"name\":");
                AppendJsonEscaped(json, passName);
                json.Append(",\"accesses\":[");
                if (passIndex < m_PassDeclarations.size())
                {
                    const RGPassDeclaration& declaration = m_PassDeclarations[passIndex];
                    for (uint32_t accessIndex = 0; accessIndex < declaration.Accesses.size(); ++accessIndex)
                    {
                        if (accessIndex > 0)
                        {
                            json.Append(',');
                        }
                        const RGPassAccess& access = declaration.Accesses[accessIndex];
                        Container::String nameText = GetIdentityText(access.NamedResourceIdentity);
                        const RGNamedResourceVersionDiagnostic diagnostic = getVersionDiagnostic(access);
                        json.Append("{\"resource\":");
                        json.AppendFormat("%u", access.Resource.Index);
                        json.Append(",\"mode\":");
                        AppendJsonEscaped(json, ToString(access.Mode));
                        json.Append(",\"state\":");
                        AppendJsonEscaped(json, ToString(access.State));
                        json.Append(",\"finalState\":");
                        AppendJsonEscaped(json, ToString(access.FinalState));
                        json.Append(",\"name\":");
                        AppendJsonEscaped(json, nameText);
                        json.Append(",\"BeforeVersionValid\":");
                        json.Append(diagnostic.BeforeVersionValid ? "true" : "false");
                        json.Append(",\"BeforeVersion\":");
                        json.AppendFormat("%u", diagnostic.BeforeVersion);
                        json.Append(",\"AfterVersion\":");
                        json.AppendFormat("%u", diagnostic.AfterVersion);
                        json.Append(",\"bCreatesNewHead\":");
                        json.Append(diagnostic.bCreatesNewHead ? "true" : "false");
                        json.Append(",\"bMutatesCurrentHead\":");
                        json.Append(diagnostic.bMutatesCurrentHead ? "true" : "false");
                        json.Append("}");
                    }
                }
                json.Append("]}");
            }
            json.Append("]");

            json.Append(",\"resources\":[");
            for (uint32_t resourceIndex = 0; resourceIndex < m_Resources.size(); ++resourceIndex)
            {
                if (resourceIndex > 0)
                {
                    json.Append(',');
                }
                const RGResourceRecord& resource = m_Resources[resourceIndex];
                json.Append("{\"index\":");
                json.AppendFormat("%u", resource.Handle.Index);
                json.Append(",\"generation\":");
                json.AppendFormat("%u", resource.Handle.Generation);
                json.Append(",\"kind\":");
                AppendJsonEscaped(json, ToString(resource.Kind));
                json.Append(",\"lifetime\":");
                AppendJsonEscaped(json, ToString(resource.Lifetime));
                json.Append(",\"debugName\":");
                AppendJsonEscaped(json, GetDebugName(resource.DebugName));
                json.Append(",\"producers\":[");
                {
                    bool bFirst = true;
                    for (uint32_t passIndex = 0; passIndex < m_PassDeclarations.size(); ++passIndex)
                    {
                        const RGPassDeclaration& declaration = m_PassDeclarations[passIndex];
                        for (const RGPassAccess& access : declaration.Accesses)
                        {
                            if (access.Resource != resource.Handle || access.Mode != RGAccessMode::Write)
                            {
                                continue;
                            }

                            if (!bFirst)
                            {
                                json.Append(',');
                            }
                            json.AppendFormat("%u", passIndex);
                            bFirst = false;
                        }
                    }
                }
                json.Append("],\"consumers\":[");
                {
                    bool bFirst = true;
                    for (uint32_t passIndex = 0; passIndex < m_PassDeclarations.size(); ++passIndex)
                    {
                        const RGPassDeclaration& declaration = m_PassDeclarations[passIndex];
                        for (const RGPassAccess& access : declaration.Accesses)
                        {
                            if (access.Resource != resource.Handle ||
                                !(access.Mode == RGAccessMode::Read ||
                                  (access.bAttachment && access.LoadOp == RHI::AttachmentLoadOp::Load)))
                            {
                                continue;
                            }

                            if (!bFirst)
                            {
                                json.Append(',');
                            }
                            json.AppendFormat("%u", passIndex);
                            bFirst = false;
                        }
                    }
                }
                json.Append("]");
                json.Append("}");
            }
            json.Append("]");

            json.Append(",\"barriers\":[");
            for (uint32_t barrierIndex = 0; barrierIndex < m_CompiledBarriers.size(); ++barrierIndex)
            {
                if (barrierIndex > 0)
                {
                    json.Append(',');
                }
                const RGCompiledBarrier& barrier = m_CompiledBarriers[barrierIndex];
                json.Append("{\"order\":");
                json.AppendFormat("%u", barrier.CompiledOrderIndex);
                json.Append(",\"pass\":");
                json.AppendFormat("%u", barrier.PassIndex);
                json.Append(",\"resource\":");
                json.AppendFormat("%u", barrier.Resource.Index);
                json.Append(",\"kind\":");
                AppendJsonEscaped(json, ToString(barrier.Kind));
                json.Append(",\"before\":");
                AppendJsonEscaped(json, ToString(barrier.BeforeState));
                json.Append(",\"after\":");
                AppendJsonEscaped(json, ToString(barrier.AfterState));
                json.Append("}");
            }
            json.Append("]");

            json.Append(",\"lifetimes\":[");
            for (uint32_t lifetimeIndex = 0; lifetimeIndex < m_CompiledResourceLifetimes.size(); ++lifetimeIndex)
            {
                if (lifetimeIndex > 0)
                {
                    json.Append(',');
                }
                const RGCompiledResourceLifetime& lifetime = m_CompiledResourceLifetimes[lifetimeIndex];
                json.Append("{\"resource\":");
                json.AppendFormat("%u", lifetime.Resource.Index);
                json.Append(",\"firstUsePass\":");
                json.AppendFormat("%u", lifetime.FirstUsePassIndex);
                json.Append(",\"lastUsePass\":");
                json.AppendFormat("%u", lifetime.LastUsePassIndex);
                json.Append(",\"firstUseOrder\":");
                json.AppendFormat("%u", lifetime.FirstUseOrderIndex);
                json.Append(",\"lastUseOrder\":");
                json.AppendFormat("%u", lifetime.LastUseOrderIndex);
                json.Append(",\"endOrder\":");
                json.AppendFormat("%u", lifetime.LifetimeEndOrderIndex);
                json.Append(",\"exported\":");
                json.Append(lifetime.bExported ? "true" : "false");
                json.Append(",\"pinnedUntilGraphEnd\":");
                json.Append(lifetime.bPinnedUntilGraphEnd ? "true" : "false");
                json.Append("}");
            }
            json.Append("]");

            json.Append(",\"transientAllocations\":[");
            for (uint32_t stepIndex = 0; stepIndex < m_TransientAllocationPlan.size(); ++stepIndex)
            {
                if (stepIndex > 0)
                {
                    json.Append(',');
                }
                const RGTransientAllocationStep& step = m_TransientAllocationPlan[stepIndex];
                json.Append("{\"resource\":");
                json.AppendFormat("%u", step.Resource.Index);
                json.Append(",\"acquireBeforePass\":");
                json.AppendFormat("%u", step.AcquireBeforePassIndex);
                json.Append(",\"acquireBeforeOrder\":");
                json.AppendFormat("%u", step.AcquireBeforeOrderIndex);
                json.Append(",\"releaseAfterPass\":");
                json.AppendFormat("%u", step.ReleaseAfterPassIndex);
                json.Append(",\"releaseAfterOrder\":");
                json.AppendFormat("%u", step.ReleaseAfterOrderIndex);
                json.Append(",\"pinnedUntilGraphEnd\":");
                json.Append(step.bPinnedUntilGraphEnd ? "true" : "false");
                json.Append("}");
            }
            json.Append("]");

            json.Append(",\"namedResources\":[");
            for (uint32_t entryIndex = 0; entryIndex < namedResources.size(); ++entryIndex)
            {
                if (entryIndex > 0)
                {
                    json.Append(',');
                }
                const NamedDumpEntry& entry = namedResources[entryIndex];
                json.Append("{\"name\":");
                AppendJsonEscaped(json, entry.NameText);
                json.Append(",\"hash\":");
                json.AppendFormat("%llu", static_cast<unsigned long long>(entry.Name.GetHash()));
                json.Append(",\"resource\":");
                json.AppendFormat("%u", entry.Resource.Index);
                json.Append(",\"version\":");
                json.AppendFormat("%u", entry.Version);
                json.Append("}");
            }
            json.Append("]}");
            strings.Json = json.ToString();
        }

        return strings;
    }

    bool RenderGraph::WriteDebugDumpFiles()
    {
        if (!ShouldWriteDebugDump())
        {
            return false;
        }

        RGDumpStrings strings = BuildDebugDump();
        if (strings.IsEmpty())
        {
            return false;
        }

        try
        {
            std::filesystem::path directory = m_DebugDumpOptions.OutputDirectory.empty()
                                                ? std::filesystem::path(".")
                                                : std::filesystem::path(m_DebugDumpOptions.OutputDirectory.c_str());
            std::filesystem::create_directories(directory);

            const Container::String prefix = m_DebugDumpOptions.FilePrefix.empty()
                                               ? Container::String("RenderGraph")
                                               : m_DebugDumpOptions.FilePrefix;
            auto writeFile = [&](const char* extension, const Container::String& content) -> bool
            {
                if (content.empty())
                {
                    return true;
                }

                Container::StringBuilder filename(128);
                filename.AppendFormat("%s_%llu.%s",
                                      prefix.c_str(),
                                      static_cast<unsigned long long>(m_FrameIndex),
                                      extension);
                std::filesystem::path filePath = directory / filename.ToString().c_str();
                std::ofstream stream(filePath, std::ios::out | std::ios::trunc | std::ios::binary);
                if (!stream.is_open())
                {
                    return false;
                }

                stream << content.c_str();
                return stream.good();
            };

            if (!writeFile("txt", strings.Text) ||
                !writeFile("dot", strings.Dot) ||
                !writeFile("json", strings.Json))
            {
                return false;
            }
        }
        catch (...)
        {
            return false;
        }

        ++m_DebugDumpFrameCount;
        return true;
    }
#else
    bool RenderGraph::TryGetDeclaredPassVersionDiagnostic(uint32_t passIndex,
                                                          uint32_t accessIndex,
                                                          RGNamedResourceVersionDiagnostic& outDiagnostic) const
    {
        (void)passIndex;
        (void)accessIndex;
        outDiagnostic = RGNamedResourceVersionDiagnostic{};
        return false;
    }

    RGDumpStrings RenderGraph::BuildDebugDump() const
    {
        return RGDumpStrings{};
    }

    bool RenderGraph::WriteDebugDumpFiles()
    {
        return false;
    }
#endif

} // namespace NorvesLib::Core::Rendering

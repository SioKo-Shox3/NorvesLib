#include "Component/ImpostorComponent.h"

#include "Rendering/RenderResources.h"
#include "Rendering/RenderWorld.h"
#include "Logging/LogMacros.h"
#include <cmath>

namespace NorvesLib::Core::Component
{
    bool ImpostorAtlasMetadataValue::SerializeToString(Container::String &out) const
    {
        const uint32_t fields[] = {
            Value.CellResolution, Value.AxisCellCountX, Value.AxisCellCountY,
            Value.AtlasWidth, Value.AtlasHeight, Value.VertexCount, Value.IndexCount,
            static_cast<uint32_t>(Value.PixelFormat)};
        Container::String serialized;
        for (uint32_t i = 0; i < 8; ++i)
        {
            Container::String field;
            if (!Detail::SerializeValue<uint32_t>(&fields[i], field))
            {
                return false;
            }
            if (i != 0)
            {
                serialized += ' ';
            }
            serialized += field;
        }
        out = serialized;
        return true;
    }

    bool ImpostorAtlasMetadataValue::DeserializeFromString(const Container::String &in)
    {
        const auto isWhitespace = [](TCHAR c)
        {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
        };
        uint32_t fields[8] = {};
        size_t position = 0;
        for (uint32_t &field : fields)
        {
            while (position < in.size() && isWhitespace(in[position]))
            {
                ++position;
            }
            const size_t start = position;
            while (position < in.size() && !isWhitespace(in[position]))
            {
                // 符号や桁以外を先に拒否し、符号なし変換での負数の巻き戻りを防ぐ。
                if (in[position] < '0' || in[position] > '9')
                {
                    return false;
                }
                ++position;
            }
            if (position == start ||
                !Detail::DeserializeValue<uint32_t>(Container::String(Container::StringView(in.data() + start, position - start)), &field))
            {
                return false;
            }
        }
        while (position < in.size() && isWhitespace(in[position]))
        {
            ++position;
        }
        if (position != in.size() ||
            fields[7] > static_cast<uint32_t>(Rendering::TextureCreateInfo::Format::BC6H_UFLOAT))
        {
            return false;
        }

        // 全項目を読み終えるまで元の値に触らない。全て0なら未設定の情報になる。
        Rendering::ImpostorBakeMetadata metadata;
        metadata.CellResolution = fields[0];
        metadata.AxisCellCountX = fields[1];
        metadata.AxisCellCountY = fields[2];
        metadata.AtlasWidth = fields[3];
        metadata.AtlasHeight = fields[4];
        metadata.VertexCount = fields[5];
        metadata.IndexCount = fields[6];
        metadata.PixelFormat = static_cast<Rendering::TextureCreateInfo::Format>(fields[7]);
        Value = metadata;
        return true;
    }

    bool ImpostorAtlasMetadataValue::operator==(const ImpostorAtlasMetadataValue &other) const
    {
        return Value.CellResolution == other.Value.CellResolution &&
               Value.AxisCellCountX == other.Value.AxisCellCountX &&
               Value.AxisCellCountY == other.Value.AxisCellCountY &&
               Value.AtlasWidth == other.Value.AtlasWidth &&
               Value.AtlasHeight == other.Value.AtlasHeight &&
               Value.VertexCount == other.Value.VertexCount &&
               Value.IndexCount == other.Value.IndexCount &&
               Value.PixelFormat == other.Value.PixelFormat;
    }

    IMPLEMENT_CLASS(ImpostorComponent, BillboardComponent)

    ImpostorComponent::ImpostorComponent()
        : BillboardComponent()
    {
        BakedAtlasAsset = TextureAssetRef{};
        BakedAtlasMetadata = ImpostorAtlasMetadataValue{};
        LODSwitchDistance = 0.0f;
        LODSwitchScreenSize = 0.0f;
        getSpace().Set(Rendering::BoardSpace::WorldSpace);
    }

    ImpostorComponent::ImpostorComponent(const FieldInitializer *initializer)
        : BillboardComponent(initializer)
    {
        BakedAtlasAsset = TextureAssetRef{};
        BakedAtlasMetadata = ImpostorAtlasMetadataValue{};
        LODSwitchDistance = 0.0f;
        LODSwitchScreenSize = 0.0f;
        getSpace().Set(Rendering::BoardSpace::WorldSpace);
    }

    ImpostorComponent::ImpostorComponent(const IUnknown *sourceObject)
        : BillboardComponent(sourceObject)
    {
        BakedAtlasAsset = TextureAssetRef{};
        BakedAtlasMetadata = ImpostorAtlasMetadataValue{};
        LODSwitchDistance = 0.0f;
        LODSwitchScreenSize = 0.0f;
        getSpace().Set(Rendering::BoardSpace::WorldSpace);
    }

    ImpostorComponent::~ImpostorComponent()
    {
        if (m_bBakedAtlasExternal && m_BakedAtlasTexture.IsValid())
        {
            NORVES_LOG_WARNING("ImpostorComponent",
                               "外部のアトラスが付いたまま破棄されます。先にTextureResourcesでReleaseBakedAtlasを呼んでください");
        }
    }

    void ImpostorComponent::BeginPlay()
    {
        BillboardComponent::BeginPlay();
        if (!BakedAtlasAsset->IsEmpty() && !HasBakedAtlas())
        {
            m_AtlasBinding.RequestTexture(BakedAtlasAsset.Get());
        }
        PollAssetBindings();
    }

    void ImpostorComponent::Finalize()
    {
        if (!m_bBakedAtlasExternal)
        {
            m_BakedAtlasTexture = Rendering::TextureHandle::Invalid();
            m_BakedNormalAtlasTexture = Rendering::TextureHandle::Invalid();
            m_BakedOrmAtlasTexture = Rendering::TextureHandle::Invalid();
            m_BakedDepthAtlasTexture = Rendering::TextureHandle::Invalid();
            MarkRenderStateDirty();
        }
        m_AtlasBinding.Reset();
        BillboardComponent::Finalize();
    }

    void ImpostorComponent::SetBakedAtlasAsset(const TextureAssetRef &reference)
    {
        BakedAtlasAsset = reference;
        OnPropertyApplied(*s_PropertyPtr_BakedAtlasAsset);
    }

    void ImpostorComponent::OnPropertyApplied(const ClassProperty &property)
    {
        BillboardComponent::OnPropertyApplied(property);
        if (&property == s_PropertyPtr_BakedAtlasMetadata)
        {
            MarkRenderStateDirty();
        }
        if (&property != s_PropertyPtr_BakedAtlasAsset ||
            BakedAtlasAsset->GetLogicalPath() == m_AtlasBinding.GetLogicalPath())
        {
            return;
        }

        // 外部のアトラスはReleaseBakedAtlasで返すまで保持する。
        if (!m_bBakedAtlasExternal)
        {
            m_BakedAtlasTexture = Rendering::TextureHandle::Invalid();
            m_BakedNormalAtlasTexture = Rendering::TextureHandle::Invalid();
            m_BakedOrmAtlasTexture = Rendering::TextureHandle::Invalid();
            m_BakedDepthAtlasTexture = Rendering::TextureHandle::Invalid();
        }
        m_AtlasBinding.Reset();
        MarkRenderStateDirty();
        if (!BakedAtlasAsset->IsEmpty() && bBegunPlay)
        {
            m_AtlasBinding.RequestTexture(BakedAtlasAsset.Get());
            PollAssetBindings();
        }
    }

    void ImpostorComponent::PollAssetBindings()
    {
        BillboardComponent::PollAssetBindings();
        if (m_bBakedAtlasExternal)
        {
            return;
        }
        Rendering::TextureHandle texture;
        if (m_AtlasBinding.TakeReadyTexture(texture))
        {
            if (!BakedAtlasMetadata->Value.IsValid())
            {
                NORVES_LOG_WARNING("ImpostorComponent", "保存されたアトラスのメタデータが無効なため束縛できません");
                return;
            }
            m_BakedAtlasTexture = texture;
            m_BakedNormalAtlasTexture = Rendering::TextureHandle::Invalid();
            m_BakedOrmAtlasTexture = Rendering::TextureHandle::Invalid();
            m_BakedDepthAtlasTexture = Rendering::TextureHandle::Invalid();
            m_bBakedAtlasExternal = false;
            MarkRenderStateDirty();
        }
    }

    bool ImpostorComponent::SetBakedAtlas(Rendering::TextureHandle texture,
                                          const Rendering::ImpostorBakeMetadata &metadata)
    {
        if (!texture.IsValid() || !metadata.IsValid())
        {
            return false;
        }

        if (m_BakedAtlasTexture.IsValid() && m_BakedAtlasTexture != texture)
        {
            NORVES_LOG_ERROR("ImpostorComponent",
                             "既存のアトラスを先にReleaseBakedAtlasで返してください");
            return false;
        }

        m_BakedAtlasTexture = texture;
        m_bBakedAtlasExternal = true;
        m_AtlasBinding.Reset();
        BakedAtlasAsset = TextureAssetRef{};
        BakedAtlasMetadata = ImpostorAtlasMetadataValue{metadata};
        MarkRenderStateDirty();
        return true;
    }

    bool ImpostorComponent::SetBakedAtlas(Rendering::TextureHandle texture,
                                         const Rendering::ImpostorBakeMetadata &metadata,
                                         Container::StringView sourcePath)
    {
        TextureAssetRef reference;
        if (!reference.TrySet(sourcePath) || !SetBakedAtlas(texture, metadata))
        {
            return false;
        }
        BakedAtlasAsset = reference;
        m_AtlasBinding.MarkExternal(Asset::AssetKind::Texture, BakedAtlasAsset->GetLogicalPath());
        return true;
    }

    bool ImpostorComponent::SetBakedMaterialAtlases(Rendering::TextureHandle normal,
                                                    Rendering::TextureHandle orm,
                                                    Rendering::TextureHandle depth)
    {
        if (!HasBakedAtlas() || !m_bBakedAtlasExternal)
        {
            return false;
        }

        if ((m_BakedNormalAtlasTexture.IsValid() && m_BakedNormalAtlasTexture != normal) ||
            (m_BakedOrmAtlasTexture.IsValid() && m_BakedOrmAtlasTexture != orm) ||
            (m_BakedDepthAtlasTexture.IsValid() && m_BakedDepthAtlasTexture != depth))
        {
            NORVES_LOG_ERROR("ImpostorComponent",
                             "既存の材質アトラスを先にReleaseBakedAtlasで返してください");
            return false;
        }

        m_BakedNormalAtlasTexture = normal;
        m_BakedOrmAtlasTexture = orm;
        m_BakedDepthAtlasTexture = depth;
        MarkRenderStateDirty();
        return true;
    }

    bool ImpostorComponent::SetBakedAtlases(const Rendering::ImpostorBakeResult& result)
    {
        return SetBakedAtlas(result.AtlasTexture, result.Metadata) &&
               SetBakedMaterialAtlases(result.NormalAtlasTexture, result.OrmAtlasTexture, result.DepthAtlasTexture);
    }

    void ImpostorComponent::SetSourceMeshComponentId(uint64_t componentId)
    {
        m_SourceMeshComponentId = componentId;
        MarkRenderStateDirty();
    }

    void ImpostorComponent::SetLODSwitchDistance(float distance)
    {
        LODSwitchDistance = std::isfinite(distance) && distance > 0.0f ? distance : 0.0f;
        MarkRenderStateDirty();
    }

    void ImpostorComponent::SetLODSwitchScreenSize(float screenSize)
    {
        LODSwitchScreenSize = std::isfinite(screenSize) && screenSize > 0.0f ? screenSize : 0.0f;
        MarkRenderStateDirty();
    }

    void ImpostorComponent::ReleaseBakedAtlas(Rendering::TextureResources &textures,
                                              Rendering::RenderWorld *renderWorld)
    {
        if (renderWorld)
        {
            renderWorld->WaitForRender();
        }

        if (m_bBakedAtlasExternal && m_BakedAtlasTexture.IsValid())
        {
            textures.ReleaseTexture(m_BakedAtlasTexture);
            if (m_BakedNormalAtlasTexture.IsValid())
            {
                textures.ReleaseTexture(m_BakedNormalAtlasTexture);
            }
            if (m_BakedOrmAtlasTexture.IsValid())
            {
                textures.ReleaseTexture(m_BakedOrmAtlasTexture);
            }
            if (m_BakedDepthAtlasTexture.IsValid())
            {
                textures.ReleaseTexture(m_BakedDepthAtlasTexture);
            }
        }

        m_BakedAtlasTexture = Rendering::TextureHandle::Invalid();
        m_BakedNormalAtlasTexture = Rendering::TextureHandle::Invalid();
        m_BakedOrmAtlasTexture = Rendering::TextureHandle::Invalid();
        m_BakedDepthAtlasTexture = Rendering::TextureHandle::Invalid();
        m_bBakedAtlasExternal = false;
        m_AtlasBinding.Reset();
        BakedAtlasMetadata = ImpostorAtlasMetadataValue{};
        MarkRenderStateDirty();
    }

    bool ImpostorComponent::BuildBoardProxy(Rendering::BoardProxy &outProxy,
                                            const Rendering::MaterialResources *materials) const
    {
        if (!BakedAtlasAsset->IsEmpty() && !HasBakedAtlas())
        {
            return false;
        }
        if (!BillboardComponent::BuildBoardProxy(outProxy, materials))
        {
            return false;
        }

        if (HasBakedAtlas())
        {
            const Rendering::ImpostorBakeMetadata &metadata = BakedAtlasMetadata->Value;
            outProxy.RenderSubtype = Rendering::BoardRenderSubtype::Impostor;
            outProxy.Texture = m_BakedAtlasTexture;
            outProxy.SourceMeshComponentId = m_SourceMeshComponentId;
            outProxy.LODSwitchDistance = LODSwitchDistance;
            outProxy.LODSwitchScreenSize = LODSwitchScreenSize;
            outProxy.ImpostorCellResolution = metadata.CellResolution;
            outProxy.ImpostorAxisCellCountX = metadata.AxisCellCountX;
            outProxy.ImpostorAxisCellCountY = metadata.AxisCellCountY;
            outProxy.ImpostorAtlasWidth = metadata.AtlasWidth;
            outProxy.ImpostorAtlasHeight = metadata.AtlasHeight;
            outProxy.ImpostorNormalTexture = m_BakedNormalAtlasTexture;
            outProxy.ImpostorOrmTexture = m_BakedOrmAtlasTexture;
            outProxy.ImpostorDepthTexture = m_BakedDepthAtlasTexture;
            outProxy.ImpostorLayout = metadata.Layout;
        }

        outProxy.Space = Rendering::BoardSpace::WorldSpace;
        return true;
    }

} // namespace NorvesLib::Core::Component

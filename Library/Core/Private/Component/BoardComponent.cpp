#include "Component/BoardComponent.h"
#include "Object/Entity.h"

namespace NorvesLib::Core::Component
{
    IMPLEMENT_CLASS(BoardComponent, Component)

    BoardComponent::BoardComponent()
        : Component()
    {
        TextureHandle = Rendering::TextureHandle::Invalid();
        bVisible = true;
        RenderLayerProp = Rendering::RenderLayer::UI;
        Space = Rendering::BoardSpace::ScreenSpace;
        BlendModeProp = Rendering::BlendMode::Translucent;
        Tint = Math::Vector4(1.0f, 1.0f, 1.0f, 0.75f);
        bFlipX = false;
        bFlipY = false;
        Pivot = Math::Vector2(0.0f, 0.0f);
        SizePx = Math::Vector2(0.0f, 0.0f);
        LayerPriority = 0;
        OrderInLayer = 0;
    }

    BoardComponent::BoardComponent(const FieldInitializer *initializer)
        : Component(initializer)
    {
        TextureHandle = Rendering::TextureHandle::Invalid();
        bVisible = true;
        RenderLayerProp = Rendering::RenderLayer::UI;
        Space = Rendering::BoardSpace::ScreenSpace;
        BlendModeProp = Rendering::BlendMode::Translucent;
        Tint = Math::Vector4(1.0f, 1.0f, 1.0f, 0.75f);
        bFlipX = false;
        bFlipY = false;
        Pivot = Math::Vector2(0.0f, 0.0f);
        SizePx = Math::Vector2(0.0f, 0.0f);
        LayerPriority = 0;
        OrderInLayer = 0;
    }

    BoardComponent::BoardComponent(const IUnknown *sourceObject)
        : Component(sourceObject)
    {
        TextureHandle = Rendering::TextureHandle::Invalid();
        bVisible = true;
        RenderLayerProp = Rendering::RenderLayer::UI;
        Space = Rendering::BoardSpace::ScreenSpace;
        BlendModeProp = Rendering::BlendMode::Translucent;
        Tint = Math::Vector4(1.0f, 1.0f, 1.0f, 0.75f);
        bFlipX = false;
        bFlipY = false;
        Pivot = Math::Vector2(0.0f, 0.0f);
        SizePx = Math::Vector2(0.0f, 0.0f);
        LayerPriority = 0;
        OrderInLayer = 0;
    }

    BoardComponent::~BoardComponent() = default;

    void BoardComponent::Initialize()
    {
        Component::Initialize();
        m_WorldTransform = Math::Matrix4x4::Identity;
        m_PreviousWorldTransform = Math::Matrix4x4::Identity;
        m_bTransformDirty = true;
    }

    void BoardComponent::Tick(float deltaTime)
    {
        (void)deltaTime;

        if (m_bTransformDirty)
        {
            UpdateWorldTransform();
        }
    }

    void BoardComponent::SetTextureHandle(Rendering::TextureHandle texture)
    {
        TextureHandle = texture;
        MarkRenderStateDirty();
    }

    void BoardComponent::SetVisible(bool bNewVisible)
    {
        bVisible = bNewVisible;
        MarkRenderStateDirty();
    }

    void BoardComponent::SetRenderLayer(Rendering::RenderLayer layer)
    {
        RenderLayerProp = layer;
        MarkRenderStateDirty();
    }

    void BoardComponent::SetBoardSpace(Rendering::BoardSpace space)
    {
        Space = space;
        MarkRenderStateDirty();
    }

    void BoardComponent::SetBlendMode(Rendering::BlendMode blendMode)
    {
        BlendModeProp = blendMode;
        MarkRenderStateDirty();
    }

    void BoardComponent::SetTint(const Math::Vector4 &tint)
    {
        Tint = tint;
        MarkRenderStateDirty();
    }

    void BoardComponent::SetFlipX(bool bInFlipX)
    {
        bFlipX = bInFlipX;
        MarkRenderStateDirty();
    }

    void BoardComponent::SetFlipY(bool bInFlipY)
    {
        bFlipY = bInFlipY;
        MarkRenderStateDirty();
    }

    void BoardComponent::SetPivot(const Math::Vector2 &pivot)
    {
        Pivot = pivot;
        MarkRenderStateDirty();
    }

    void BoardComponent::SetSizePx(const Math::Vector2 &sizePx)
    {
        SizePx = sizePx;
        MarkRenderStateDirty();
    }

    void BoardComponent::SetLayerPriority(uint32_t layerPriority)
    {
        LayerPriority = layerPriority;
        MarkRenderStateDirty();
    }

    void BoardComponent::SetOrderInLayer(uint32_t orderInLayer)
    {
        OrderInLayer = orderInLayer;
        MarkRenderStateDirty();
    }

    void BoardComponent::RefreshRenderTransformCache()
    {
        UpdateWorldTransform();
    }

    bool BoardComponent::BuildBoardProxy(Rendering::BoardProxy &outProxy,
                                         const Rendering::MaterialResources *materials) const
    {
        (void)materials;

        if (!IsVisible())
        {
            return false;
        }

        outProxy = Rendering::BoardProxy{};
        outProxy.ObjectId = GetOwnerId();
        outProxy.ComponentId = ComponentId;
        outProxy.Texture = TextureHandle;
        outProxy.WorldTransform = m_WorldTransform;
        outProxy.PreviousWorldTransform = m_PreviousWorldTransform;
        outProxy.LayerMask = RenderLayerProp;
        outProxy.Space = Space;
        outProxy.BlendModeProp = BlendModeProp;
        outProxy.Tint = Tint;
        outProxy.bFlipX = bFlipX;
        outProxy.bFlipY = bFlipY;
        outProxy.Pivot = Pivot;
        outProxy.SizePx = SizePx;
        outProxy.LayerPriority = LayerPriority;
        outProxy.OrderInLayer = OrderInLayer;
        outProxy.SortKey = Rendering::BoardProxy::ComputeSortKey(LayerPriority, OrderInLayer);
        outProxy.bVisible = bVisible;
        return true;
    }

    void BoardComponent::UpdateWorldTransform()
    {
        m_PreviousWorldTransform = m_WorldTransform;
        CalculateWorldMatrix(m_WorldTransform);
        m_bTransformDirty = false;
    }

    void BoardComponent::CalculateWorldMatrix(Math::Matrix4x4 &outMatrix) const
    {
        const auto *owner = GetOwner();
        if (!owner)
        {
            outMatrix = Math::Matrix4x4::Identity;
            return;
        }

        const Math::Transform &worldTransform = owner->GetWorldTransform();
        outMatrix = worldTransform.ToMatrix();
        outMatrix.m30 = worldTransform.position.x;
        outMatrix.m31 = worldTransform.position.y;
        outMatrix.m32 = worldTransform.position.z;
        outMatrix.m03 = 0.0f;
        outMatrix.m13 = 0.0f;
        outMatrix.m23 = 0.0f;
        outMatrix.m33 = 1.0f;
    }

} // namespace NorvesLib::Core::Component

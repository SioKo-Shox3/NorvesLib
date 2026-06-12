#include "Component/MegaGeometryComponent.h"

namespace NorvesLib::Core::Component
{
    IMPLEMENT_CLASS(MegaGeometryComponent, MeshComponent)

    MegaGeometryComponent::MegaGeometryComponent()
        : MeshComponent()
    {
    }

    MegaGeometryComponent::MegaGeometryComponent(const FieldInitializer *initializer)
        : MeshComponent(initializer)
    {
    }

    MegaGeometryComponent::MegaGeometryComponent(const IUnknown *sourceObject)
        : MeshComponent(sourceObject)
    {
    }

    MegaGeometryComponent::~MegaGeometryComponent()
    {
    }

    void MegaGeometryComponent::SetMegaMeshHandle(Rendering::MegaGeometry::MegaMeshHandle handle)
    {
        MegaMeshHandleValue = handle;
        MarkRenderStateDirty();
    }

    bool MegaGeometryComponent::BuildMegaGeometryProxy(Rendering::MegaGeometryProxy &outProxy) const
    {
        if (!MegaMeshHandleValue->IsValid())
        {
            return false;
        }

        if (!IsVisible())
        {
            return false;
        }

        outProxy.ObjectId = GetOwnerId();
        outProxy.ComponentId = ComponentId;
        outProxy.MegaMeshHandle = MegaMeshHandleValue;
        outProxy.WorldTransform = m_WorldTransform;
        outProxy.WorldBounds = m_WorldBounds;
        outProxy.bVisible = bVisible;
        outProxy.bCastShadow = bCastShadow;
        outProxy.LayerMask = RenderLayerProp;
        return true;
    }

} // namespace NorvesLib::Core::Component

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
        getMegaMeshHandleValue().Set(handle);
        MarkRenderStateDirty();
    }

    bool MegaGeometryComponent::BuildMegaGeometryProxy(Rendering::MegaGeometryProxy &outProxy) const
    {
        const Rendering::MegaGeometry::MegaMeshHandle megaMeshHandle = getMegaMeshHandleValue().Get();
        if (!megaMeshHandle.IsValid())
        {
            return false;
        }

        if (!IsVisible())
        {
            return false;
        }

        outProxy.ObjectId = GetOwnerId();
        outProxy.ComponentId = ComponentId;
        outProxy.MegaMeshHandle = megaMeshHandle;
        outProxy.WorldTransform = m_WorldTransform;
        outProxy.WorldBounds = m_WorldBounds;
        outProxy.bVisible = getbVisible().Get();
        outProxy.bCastShadow = getbCastShadow().Get();
        outProxy.LayerMask = getRenderLayerProp().Get();
        return true;
    }

} // namespace NorvesLib::Core::Component

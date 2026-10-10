#pragma once

#include "MeshComponent.h"
#include "Ecs/EcsRenderTypes.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/MegaGeometry/MegaGeometryTypes.h"

namespace NorvesLib::Core::Component
{

    /**
     * @brief MegaGeometry rendering component.
     *
     * Synchronizes MegaMesh instances through the standard World/Component path.
     * Transform and visibility state reuse MeshComponent behavior.
     */
    class MegaGeometryComponent : public MeshComponent
    {
        REFLECTION_CLASS(MegaGeometryComponent, MeshComponent)

    public:
        MegaGeometryComponent();
        explicit MegaGeometryComponent(const FieldInitializer *initializer);
        explicit MegaGeometryComponent(const IUnknown *sourceObject);
        virtual ~MegaGeometryComponent();

        Ecs::EcsComponentRowKind GetEcsRowKind() const override { return Ecs::EcsComponentRowKind::MegaGeometry; }

        void SetMegaMeshHandle(Rendering::MegaGeometry::MegaMeshHandle handle);
        Rendering::MegaGeometry::MegaMeshHandle GetMegaMeshHandle() const { return getMegaMeshHandleValue().Get(); }
        bool HasMegaMesh() const { return getMegaMeshHandleValue().Get().IsValid(); }

        /**
         * @brief Build a MegaGeometry proxy for SceneView synchronization.
         * @param outProxy Output proxy destination.
         * @return true when a valid proxy was produced.
         */
        bool BuildMegaGeometryProxy(Rendering::MegaGeometryProxy &outProxy) const;

    protected:
        // MegaMesh の handle は World の木にいる間 ECS の行（EcsMegaMesh）の列にあり、無い間は予備の値にある
        ECS_PROPERTY(Ecs::EcsMegaMesh, Handle, Rendering::MegaGeometry::MegaMeshHandle, MegaMeshHandleValue)
    };

    using MegaGeometryComponentPtr = Container::TSharedPtr<MegaGeometryComponent>;
    using MegaGeometryComponentWeakPtr = Container::TWeakPtr<MegaGeometryComponent>;

} // namespace NorvesLib::Core::Component

// Phase2: cast flag bit for this hot type (CastTo flag fast-path)
DECLARE_CLASS_CAST_FLAG(NorvesLib::Core::Component::MegaGeometryComponent, NorvesLib::Core::EClassCastFlags::MegaGeometryComponent)

#pragma once

#include "MeshComponent.h"
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

        void SetMegaMeshHandle(Rendering::MegaGeometry::MegaMeshHandle handle);
        Rendering::MegaGeometry::MegaMeshHandle GetMegaMeshHandle() const { return MegaMeshHandleValue; }
        bool HasMegaMesh() const { return MegaMeshHandleValue->IsValid(); }

        /**
         * @brief Build a MegaGeometry proxy for SceneView synchronization.
         * @param outProxy Output proxy destination.
         * @return true when a valid proxy was produced.
         */
        bool BuildMegaGeometryProxy(Rendering::MegaGeometryProxy &outProxy) const;

    protected:
        PROPERTY(Rendering::MegaGeometry::MegaMeshHandle, MegaMeshHandleValue)
    };

    using MegaGeometryComponentPtr = Container::TSharedPtr<MegaGeometryComponent>;
    using MegaGeometryComponentWeakPtr = Container::TWeakPtr<MegaGeometryComponent>;

} // namespace NorvesLib::Core::Component

// Phase2: cast flag bit for this hot type (CastTo flag fast-path)
DECLARE_CLASS_CAST_FLAG(NorvesLib::Core::Component::MegaGeometryComponent, NorvesLib::Core::EClassCastFlags::MegaGeometryComponent)

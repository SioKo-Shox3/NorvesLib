#pragma once

#include "Component/BoardComponent.h"

namespace NorvesLib::Core::Component
{
    class BillboardComponent : public BoardComponent
    {
        REFLECTION_CLASS(BillboardComponent, BoardComponent)

    public:
        BillboardComponent();
        explicit BillboardComponent(const FieldInitializer *initializer);
        explicit BillboardComponent(const IUnknown *sourceObject);
        virtual ~BillboardComponent();

        void SetSizeWorld(const Math::Vector2 &sizeWorld);
        const Math::Vector2 &GetSizeWorld() const { return SizeWorld; }

        bool BuildBoardProxy(Rendering::BoardProxy &outProxy,
                             const Rendering::MaterialResources *materials = nullptr) const override;

    protected:
        PROPERTY(Math::Vector2, SizeWorld)
    };

    using BillboardComponentPtr = Container::TSharedPtr<BillboardComponent>;
    using BillboardComponentWeakPtr = Container::TWeakPtr<BillboardComponent>;

} // namespace NorvesLib::Core::Component

DECLARE_CLASS_CAST_FLAG(NorvesLib::Core::Component::BillboardComponent, NorvesLib::Core::EClassCastFlags::BillboardComponent)

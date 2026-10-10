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
        Math::Vector2 GetSizeWorld() const { return getSizeWorld().Get(); }

        bool BuildBoardProxy(Rendering::BoardProxy &outProxy,
                             const Rendering::MaterialResources *materials = nullptr) const override;

        Ecs::EcsComponentRowKind GetEcsRowKind() const override { return Ecs::EcsComponentRowKind::Billboard; }

    protected:
        // 大きさは World の木にいる間 ECS の行（EcsBillboardSize）の列にあり、無い間は予備の値にある
        ECS_PROPERTY(Ecs::EcsBillboardSize, SizeWorld, Math::Vector2, SizeWorld)
    };

    using BillboardComponentPtr = Container::TSharedPtr<BillboardComponent>;
    using BillboardComponentWeakPtr = Container::TWeakPtr<BillboardComponent>;

} // namespace NorvesLib::Core::Component

DECLARE_CLASS_CAST_FLAG(NorvesLib::Core::Component::BillboardComponent, NorvesLib::Core::EClassCastFlags::BillboardComponent)

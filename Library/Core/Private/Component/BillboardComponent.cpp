#include "Component/BillboardComponent.h"
#include "Math/MatrixUtils.h"
#include <cmath>

namespace NorvesLib::Core::Component
{
    IMPLEMENT_CLASS(BillboardComponent, BoardComponent)

    namespace
    {
        float AbsFloat(float value)
        {
            return value < 0.0f ? -value : value;
        }

        float MaxFloat(float a, float b)
        {
            return a > b ? a : b;
        }

    } // namespace

    BillboardComponent::BillboardComponent()
        : BoardComponent()
    {
        RenderLayerProp = Rendering::RenderLayer::Default;
        Space = Rendering::BoardSpace::WorldSpace;
        SizeWorld = Math::Vector2(1.0f, 1.0f);
    }

    BillboardComponent::BillboardComponent(const FieldInitializer *initializer)
        : BoardComponent(initializer)
    {
        RenderLayerProp = Rendering::RenderLayer::Default;
        Space = Rendering::BoardSpace::WorldSpace;
        SizeWorld = Math::Vector2(1.0f, 1.0f);
    }

    BillboardComponent::BillboardComponent(const IUnknown *sourceObject)
        : BoardComponent(sourceObject)
    {
        RenderLayerProp = Rendering::RenderLayer::Default;
        Space = Rendering::BoardSpace::WorldSpace;
        SizeWorld = Math::Vector2(1.0f, 1.0f);
    }

    BillboardComponent::~BillboardComponent() = default;

    void BillboardComponent::SetSizeWorld(const Math::Vector2 &sizeWorld)
    {
        SizeWorld = sizeWorld;
        MarkRenderStateDirty();
    }

    bool BillboardComponent::BuildBoardProxy(Rendering::BoardProxy &outProxy,
                                             const Rendering::MaterialResources *materials) const
    {
        if (!BoardComponent::BuildBoardProxy(outProxy, materials))
        {
            return false;
        }

        outProxy.Space = Rendering::BoardSpace::WorldSpace;
        outProxy.LayerMask = RenderLayerProp;
        outProxy.SizeWorld = SizeWorld;
        const Math::Vector3 translation = outProxy.WorldTransform.GetTranslationRow();
        outProxy.WorldBounds.CenterX = translation.x;
        outProxy.WorldBounds.CenterY = translation.y;
        outProxy.WorldBounds.CenterZ = translation.z;

        const Math::Vector3 scale = Math::MatrixUtils::ExtractScale(outProxy.WorldTransform);
        const float scaleX = scale.x;
        const float scaleY = scale.y;
        const Math::Vector2 sizeWorld = SizeWorld;
        const Math::Vector2 pivot = Pivot;
        const float width = AbsFloat(sizeWorld.x) * scaleX;
        const float height = AbsFloat(sizeWorld.y) * scaleY;
        const float left = -pivot.x * width;
        const float right = (1.0f - pivot.x) * width;
        const float bottom = -pivot.y * height;
        const float top = (1.0f - pivot.y) * height;
        const float maxX = MaxFloat(AbsFloat(left), AbsFloat(right));
        const float maxY = MaxFloat(AbsFloat(bottom), AbsFloat(top));

        outProxy.WorldBounds.Radius = std::sqrt(maxX * maxX + maxY * maxY);
        return true;
    }

} // namespace NorvesLib::Core::Component

#pragma once

#include "Component.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/RenderResourcesFwd.h"
#include "Math/Matrix4x4.h"
#include "Math/Vector2.h"
#include "Math/Vector4.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include <cstdint>

namespace NorvesLib::Core::Component
{
    class BoardComponent : public Component
    {
        REFLECTION_CLASS(BoardComponent, Component)

    public:
        BoardComponent();
        explicit BoardComponent(const FieldInitializer *initializer);
        explicit BoardComponent(const IUnknown *sourceObject);
        virtual ~BoardComponent();

        virtual void Initialize() override;
        virtual void Tick(float deltaTime) override;

        void SetTextureHandle(Rendering::TextureHandle texture);
        Rendering::TextureHandle GetTextureHandle() const { return TextureHandle; }

        void SetVisible(bool bNewVisible);
        bool IsVisible() const { return bVisible && IsActive(); }

        void SetRenderLayer(Rendering::RenderLayer layer);
        Rendering::RenderLayer GetRenderLayer() const { return RenderLayerProp; }

        void SetBoardSpace(Rendering::BoardSpace space);
        Rendering::BoardSpace GetBoardSpace() const { return Space; }

        void SetBlendMode(Rendering::BlendMode blendMode);
        Rendering::BlendMode GetBlendMode() const { return BlendModeProp; }

        void SetTint(const Math::Vector4 &tint);
        const Math::Vector4 &GetTint() const { return Tint; }

        void SetFlipX(bool bInFlipX);
        bool GetFlipX() const { return bFlipX; }

        void SetFlipY(bool bInFlipY);
        bool GetFlipY() const { return bFlipY; }

        void SetPivot(const Math::Vector2 &pivot);
        const Math::Vector2 &GetPivot() const { return Pivot; }

        void SetSizePx(const Math::Vector2 &sizePx);
        const Math::Vector2 &GetSizePx() const { return SizePx; }

        void SetUVRect(const Math::Vector4 &uvRect);
        const Math::Vector4 &GetUVRect() const { return UVRectProp; }

        void SetLayerPriority(uint32_t layerPriority);
        uint32_t GetLayerPriority() const { return LayerPriority; }

        void SetOrderInLayer(uint32_t orderInLayer);
        uint32_t GetOrderInLayer() const { return OrderInLayer; }

        const Math::Matrix4x4 &GetWorldTransformCache() const { return m_WorldTransform; }
        const Math::Matrix4x4 &GetPreviousWorldTransformCache() const { return m_PreviousWorldTransform; }

        static Math::Vector4 GetFullTextureUVRect();
        static Container::VariableArray<Math::Vector4> ComputeSpriteSheetUVRects(uint32_t texWidth,
                                                                                 uint32_t texHeight,
                                                                                 uint32_t cellWidth,
                                                                                 uint32_t cellHeight);

        void RefreshRenderTransformCache();
        virtual bool BuildBoardProxy(Rendering::BoardProxy &outProxy,
                                     const Rendering::MaterialResources *materials = nullptr) const;

    protected:
        void UpdateWorldTransform();
        void CalculateWorldMatrix(Math::Matrix4x4 &outMatrix) const;

        PROPERTY(Rendering::TextureHandle, TextureHandle)
        PROPERTY(bool, bVisible)
        PROPERTY(Rendering::RenderLayer, RenderLayerProp)
        PROPERTY(Rendering::BoardSpace, Space)
        PROPERTY(Rendering::BlendMode, BlendModeProp)
        PROPERTY(Math::Vector4, Tint)
        PROPERTY(bool, bFlipX)
        PROPERTY(bool, bFlipY)
        PROPERTY(Math::Vector2, Pivot)
        PROPERTY(Math::Vector2, SizePx)
        PROPERTY(Math::Vector4, UVRectProp)
        PROPERTY(uint32_t, LayerPriority)
        PROPERTY(uint32_t, OrderInLayer)

        Math::Matrix4x4 m_WorldTransform;
        Math::Matrix4x4 m_PreviousWorldTransform;
        bool m_bTransformDirty = true;
    };

    using BoardComponentPtr = Container::TSharedPtr<BoardComponent>;
    using BoardComponentWeakPtr = Container::TWeakPtr<BoardComponent>;

} // namespace NorvesLib::Core::Component

DECLARE_CLASS_CAST_FLAG(NorvesLib::Core::Component::BoardComponent, NorvesLib::Core::EClassCastFlags::BoardComponent)

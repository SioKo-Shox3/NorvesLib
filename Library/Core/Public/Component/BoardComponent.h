#pragma once

#include "Component.h"
#include "Ecs/EcsRenderTypes.h"
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
        virtual void BeginPlay() override;
        virtual void Tick(float deltaTime) override;

        Ecs::EcsComponentRowKind GetEcsRowKind() const override { return Ecs::EcsComponentRowKind::Board; }

        void SetTextureHandle(Rendering::TextureHandle texture);
        Rendering::TextureHandle GetTextureHandle() const { return getTextureHandle().Get(); }

        void SetVisible(bool bNewVisible);
        bool IsVisible() const { return getbVisible().Get() && IsActive(); }

        void SetRenderLayer(Rendering::RenderLayer layer);
        Rendering::RenderLayer GetRenderLayer() const { return getRenderLayerProp().Get(); }

        void SetBoardSpace(Rendering::BoardSpace space);
        Rendering::BoardSpace GetBoardSpace() const { return getSpace().Get(); }

        void SetBlendMode(Rendering::BlendMode blendMode);
        Rendering::BlendMode GetBlendMode() const { return getBlendModeProp().Get(); }

        void SetTint(const Math::Vector4 &tint);
        Math::Vector4 GetTint() const { return getTint().Get(); }

        void SetFlipX(bool bInFlipX);
        bool GetFlipX() const { return getbFlipX().Get(); }

        void SetFlipY(bool bInFlipY);
        bool GetFlipY() const { return getbFlipY().Get(); }

        void SetPivot(const Math::Vector2 &pivot);
        Math::Vector2 GetPivot() const { return getPivot().Get(); }

        void SetSizePx(const Math::Vector2 &sizePx);
        Math::Vector2 GetSizePx() const { return getSizePx().Get(); }

        void SetUVRect(const Math::Vector4 &uvRect);
        Math::Vector4 GetUVRect() const { return getUVRectProp().Get(); }

        bool SetFlipbookGrid(uint32_t textureWidth,
                             uint32_t textureHeight,
                             uint32_t cellWidth,
                             uint32_t cellHeight,
                             uint32_t firstFrameIndex = 0u);
        void SetFrameCount(uint32_t frameCount);
        uint32_t GetFrameCount() const { return FrameCount; }
        void SetFramesPerSecond(float framesPerSecond);
        float GetFramesPerSecond() const { return FramesPerSecond; }
        void SetLoop(bool bInLoop);
        bool IsLooping() const { return bLoop; }
        void Play();
        void Pause();
        void Stop();
        void SetFrame(uint32_t frameIndex);
        uint32_t GetCurrentFrame() const { return m_CurrentFrame; }
        bool IsPlaying() const { return m_bPlaying; }
        void PrepareFlipbookForRenderSync();

        void SetLayerPriority(uint32_t layerPriority);
        uint32_t GetLayerPriority() const { return getLayerPriority().Get(); }

        void SetOrderInLayer(uint32_t orderInLayer);
        uint32_t GetOrderInLayer() const { return getOrderInLayer().Get(); }

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
        void InitializeFlipbookDefaults();
        bool ValidateFlipbookRange(uint32_t firstFrameIndex,
                                   uint32_t frameCount,
                                   size_t rectCount) const;
        bool IsFlipbookAuthoringCacheStale() const;
        void CaptureFlipbookAuthoringCache();
        void RebuildFlipbookUVRects();
        void InvalidateFlipbookCache();
        void EnsureFlipbookCacheReady();
        uint32_t ClampFlipbookFrame(uint32_t frameIndex) const;
        bool ApplyCurrentFlipbookFrameToUV();
        void AdvanceFlipbook(float deltaTime);

        // 描画の値は World の木にいる間 ECS の行（EcsBoardRender）の列にあり、無い間は予備の値にある
        ECS_PROPERTY(Ecs::EcsBoardRender, Texture, Rendering::TextureHandle, TextureHandle)
        ECS_PROPERTY(Ecs::EcsBoardRender, bVisible, bool, bVisible)
        ECS_PROPERTY(Ecs::EcsBoardRender, Layer, Rendering::RenderLayer, RenderLayerProp)
        ECS_PROPERTY(Ecs::EcsBoardRender, Space, Rendering::BoardSpace, Space)
        ECS_PROPERTY(Ecs::EcsBoardRender, Blend, Rendering::BlendMode, BlendModeProp)
        ECS_PROPERTY(Ecs::EcsBoardRender, Tint, Math::Vector4, Tint)
        ECS_PROPERTY(Ecs::EcsBoardRender, bFlipX, bool, bFlipX)
        ECS_PROPERTY(Ecs::EcsBoardRender, bFlipY, bool, bFlipY)
        ECS_PROPERTY(Ecs::EcsBoardRender, Pivot, Math::Vector2, Pivot)
        ECS_PROPERTY(Ecs::EcsBoardRender, SizePx, Math::Vector2, SizePx)
        ECS_PROPERTY(Ecs::EcsBoardRender, UVRect, Math::Vector4, UVRectProp)
        PROPERTY(uint32_t, FrameCount)
        PROPERTY(float, FramesPerSecond)
        PROPERTY(bool, bLoop)
        PROPERTY(bool, bPlayOnBeginPlay)
        PROPERTY(uint32_t, InitialFrame)
        PROPERTY(uint32_t, AtlasTextureWidth)
        PROPERTY(uint32_t, AtlasTextureHeight)
        PROPERTY(uint32_t, AtlasCellWidth)
        PROPERTY(uint32_t, AtlasCellHeight)
        PROPERTY(uint32_t, FirstFrameIndex)
        ECS_PROPERTY(Ecs::EcsBoardRender, LayerPriority, uint32_t, LayerPriority)
        ECS_PROPERTY(Ecs::EcsBoardRender, OrderInLayer, uint32_t, OrderInLayer)

        Math::Matrix4x4 m_WorldTransform;
        Math::Matrix4x4 m_PreviousWorldTransform;
        bool m_bTransformDirty = true;

        uint32_t m_CurrentFrame = 0;
        bool m_bPlaying = false;
        float m_FrameAccumulator = 0.0f;
        Container::VariableArray<Math::Vector4> m_FlipbookUVRects;
        bool m_bFlipbookCacheDirty = true;
        bool m_bFlipbookCacheValid = false;
        uint32_t m_CachedFrameCount = 0;
        uint32_t m_CachedInitialFrame = 0;
        uint32_t m_CachedAtlasTextureWidth = 0;
        uint32_t m_CachedAtlasTextureHeight = 0;
        uint32_t m_CachedAtlasCellWidth = 0;
        uint32_t m_CachedAtlasCellHeight = 0;
        uint32_t m_CachedFirstFrameIndex = 0;
    };

    using BoardComponentPtr = Container::TSharedPtr<BoardComponent>;
    using BoardComponentWeakPtr = Container::TWeakPtr<BoardComponent>;

} // namespace NorvesLib::Core::Component

DECLARE_CLASS_CAST_FLAG(NorvesLib::Core::Component::BoardComponent, NorvesLib::Core::EClassCastFlags::BoardComponent)

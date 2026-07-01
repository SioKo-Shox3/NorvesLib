#include "Component/BoardComponent.h"
#include "Math/MatrixUtils.h"
#include "Object/Entity.h"
#include <cmath>
#include <limits>

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
        UVRectProp = GetFullTextureUVRect();
        InitializeFlipbookDefaults();
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
        UVRectProp = GetFullTextureUVRect();
        InitializeFlipbookDefaults();
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
        UVRectProp = GetFullTextureUVRect();
        InitializeFlipbookDefaults();
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

        m_CurrentFrame = ClampFlipbookFrame(InitialFrame);
        m_FrameAccumulator = 0.0f;
        RebuildFlipbookUVRects();
        ApplyCurrentFlipbookFrameToUV();
    }

    void BoardComponent::BeginPlay()
    {
        Component::BeginPlay();
        if (bPlayOnBeginPlay)
        {
            Play();
        }
    }

    void BoardComponent::Tick(float deltaTime)
    {
        if (m_bTransformDirty)
        {
            UpdateWorldTransform();
        }

        AdvanceFlipbook(deltaTime);
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

    void BoardComponent::SetUVRect(const Math::Vector4 &uvRect)
    {
        UVRectProp = uvRect;
        MarkRenderStateDirty();
    }

    bool BoardComponent::SetFlipbookGrid(uint32_t textureWidth,
                                         uint32_t textureHeight,
                                         uint32_t cellWidth,
                                         uint32_t cellHeight,
                                         uint32_t firstFrameIndex)
    {
        const Container::VariableArray<Math::Vector4> rects =
            ComputeSpriteSheetUVRects(textureWidth, textureHeight, cellWidth, cellHeight);
        const uint32_t effectiveFrameCount = FrameCount.Get() == 0 ? 1u : FrameCount.Get();
        if (!ValidateFlipbookRange(firstFrameIndex, effectiveFrameCount, rects.size()))
        {
            AtlasTextureWidth = textureWidth;
            AtlasTextureHeight = textureHeight;
            AtlasCellWidth = cellWidth;
            AtlasCellHeight = cellHeight;
            FirstFrameIndex = firstFrameIndex;
            if (FrameCount == 0)
            {
                FrameCount = 1;
            }

            InvalidateFlipbookCache();
            CaptureFlipbookAuthoringCache();
            m_CurrentFrame = ClampFlipbookFrame(m_CurrentFrame);
            m_FrameAccumulator = 0.0f;
            m_bPlaying = false;
            return false;
        }

        AtlasTextureWidth = textureWidth;
        AtlasTextureHeight = textureHeight;
        AtlasCellWidth = cellWidth;
        AtlasCellHeight = cellHeight;
        FirstFrameIndex = firstFrameIndex;
        if (FrameCount == 0)
        {
            FrameCount = 1;
        }

        m_FlipbookUVRects = rects;
        m_bFlipbookCacheDirty = false;
        m_bFlipbookCacheValid = true;
        CaptureFlipbookAuthoringCache();
        m_CurrentFrame = ClampFlipbookFrame(m_CurrentFrame);
        ApplyCurrentFlipbookFrameToUV();
        return true;
    }

    void BoardComponent::SetFrameCount(uint32_t frameCount)
    {
        FrameCount = frameCount == 0 ? 1u : frameCount;
        m_bFlipbookCacheDirty = true;
        RebuildFlipbookUVRects();
        m_CurrentFrame = ClampFlipbookFrame(m_CurrentFrame);
        ApplyCurrentFlipbookFrameToUV();
    }

    void BoardComponent::SetFramesPerSecond(float framesPerSecond)
    {
        FramesPerSecond = std::isfinite(framesPerSecond) && framesPerSecond > 0.0f
                              ? framesPerSecond
                              : 0.0f;
    }

    void BoardComponent::SetLoop(bool bInLoop)
    {
        bLoop = bInLoop;
    }

    void BoardComponent::Play()
    {
        EnsureFlipbookCacheReady();
        m_CurrentFrame = ClampFlipbookFrame(m_CurrentFrame);

        if (m_bFlipbookCacheValid &&
            !bLoop &&
            FrameCount > 0 &&
            m_CurrentFrame + 1u >= FrameCount)
        {
            m_CurrentFrame = 0;
            m_FrameAccumulator = 0.0f;
        }

        ApplyCurrentFlipbookFrameToUV();
        m_bPlaying = true;
    }

    void BoardComponent::Pause()
    {
        m_bPlaying = false;
    }

    void BoardComponent::Stop()
    {
        m_bPlaying = false;
        m_FrameAccumulator = 0.0f;
        m_CurrentFrame = ClampFlipbookFrame(InitialFrame);
        ApplyCurrentFlipbookFrameToUV();
    }

    void BoardComponent::SetFrame(uint32_t frameIndex)
    {
        m_FrameAccumulator = 0.0f;
        m_CurrentFrame = ClampFlipbookFrame(frameIndex);
        ApplyCurrentFlipbookFrameToUV();
    }

    void BoardComponent::PrepareFlipbookForRenderSync()
    {
        const bool bAuthoringChanged = IsFlipbookAuthoringCacheStale();
        if (!m_bFlipbookCacheDirty && !bAuthoringChanged)
        {
            return;
        }

        EnsureFlipbookCacheReady();
        m_CurrentFrame = ClampFlipbookFrame(m_CurrentFrame);

        if (!m_bFlipbookCacheValid)
        {
            m_FrameAccumulator = 0.0f;
            m_bPlaying = false;
            return;
        }

        ApplyCurrentFlipbookFrameToUV();
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

    Math::Vector4 BoardComponent::GetFullTextureUVRect()
    {
        return Math::Vector4(0.0f, 0.0f, 1.0f, 1.0f);
    }

    Container::VariableArray<Math::Vector4> BoardComponent::ComputeSpriteSheetUVRects(uint32_t texWidth,
                                                                                       uint32_t texHeight,
                                                                                       uint32_t cellWidth,
                                                                                       uint32_t cellHeight)
    {
        Container::VariableArray<Math::Vector4> rects;
        if (texWidth == 0 ||
            texHeight == 0 ||
            cellWidth == 0 ||
            cellHeight == 0)
        {
            return rects;
        }

        const uint32_t columnCount = texWidth / cellWidth;
        const uint32_t rowCount = texHeight / cellHeight;
        if (columnCount == 0 || rowCount == 0)
        {
            return rects;
        }

        const float normalizedCellWidth = static_cast<float>(cellWidth) / static_cast<float>(texWidth);
        const float normalizedCellHeight = static_cast<float>(cellHeight) / static_cast<float>(texHeight);
        rects.reserve(static_cast<size_t>(columnCount) * static_cast<size_t>(rowCount));

        for (uint32_t row = 0; row < rowCount; ++row)
        {
            const float minV = static_cast<float>(row * cellHeight) / static_cast<float>(texHeight);
            for (uint32_t column = 0; column < columnCount; ++column)
            {
                const float minU = static_cast<float>(column * cellWidth) / static_cast<float>(texWidth);
                rects.push_back(Math::Vector4(minU,
                                              minV,
                                              normalizedCellWidth,
                                              normalizedCellHeight));
            }
        }

        return rects;
    }

    void BoardComponent::InitializeFlipbookDefaults()
    {
        FrameCount = 1;
        FramesPerSecond = 0.0f;
        bLoop = true;
        bPlayOnBeginPlay = false;
        InitialFrame = 0;
        AtlasTextureWidth = 0;
        AtlasTextureHeight = 0;
        AtlasCellWidth = 0;
        AtlasCellHeight = 0;
        FirstFrameIndex = 0;
        m_CurrentFrame = 0;
        m_bPlaying = false;
        m_FrameAccumulator = 0.0f;
        m_FlipbookUVRects.clear();
        m_bFlipbookCacheDirty = true;
        m_bFlipbookCacheValid = false;
        CaptureFlipbookAuthoringCache();
    }

    void BoardComponent::InvalidateFlipbookCache()
    {
        m_FlipbookUVRects.clear();
        m_bFlipbookCacheDirty = false;
        m_bFlipbookCacheValid = false;
    }

    bool BoardComponent::ValidateFlipbookRange(uint32_t firstFrameIndex,
                                               uint32_t frameCount,
                                               size_t rectCount) const
    {
        if (frameCount == 0)
        {
            return false;
        }

        const size_t firstFrame = static_cast<size_t>(firstFrameIndex);
        const size_t requestedFrameCount = static_cast<size_t>(frameCount);
        return firstFrame <= rectCount && requestedFrameCount <= rectCount - firstFrame;
    }

    bool BoardComponent::IsFlipbookAuthoringCacheStale() const
    {
        return m_CachedFrameCount != FrameCount ||
               m_CachedInitialFrame != InitialFrame ||
               m_CachedAtlasTextureWidth != AtlasTextureWidth ||
               m_CachedAtlasTextureHeight != AtlasTextureHeight ||
               m_CachedAtlasCellWidth != AtlasCellWidth ||
               m_CachedAtlasCellHeight != AtlasCellHeight ||
               m_CachedFirstFrameIndex != FirstFrameIndex;
    }

    void BoardComponent::CaptureFlipbookAuthoringCache()
    {
        m_CachedFrameCount = FrameCount;
        m_CachedInitialFrame = InitialFrame;
        m_CachedAtlasTextureWidth = AtlasTextureWidth;
        m_CachedAtlasTextureHeight = AtlasTextureHeight;
        m_CachedAtlasCellWidth = AtlasCellWidth;
        m_CachedAtlasCellHeight = AtlasCellHeight;
        m_CachedFirstFrameIndex = FirstFrameIndex;
    }

    void BoardComponent::RebuildFlipbookUVRects()
    {
        m_FlipbookUVRects = ComputeSpriteSheetUVRects(AtlasTextureWidth,
                                                       AtlasTextureHeight,
                                                       AtlasCellWidth,
                                                       AtlasCellHeight);
        m_bFlipbookCacheValid = ValidateFlipbookRange(FirstFrameIndex, FrameCount, m_FlipbookUVRects.size());
        if (!m_bFlipbookCacheValid)
        {
            m_FlipbookUVRects.clear();
            m_FrameAccumulator = 0.0f;
            m_bPlaying = false;
        }
        m_bFlipbookCacheDirty = false;
        CaptureFlipbookAuthoringCache();
    }

    void BoardComponent::EnsureFlipbookCacheReady()
    {
        const bool bAuthoringChanged = IsFlipbookAuthoringCacheStale();
        if (!m_bFlipbookCacheDirty && !bAuthoringChanged)
        {
            return;
        }

        RebuildFlipbookUVRects();
        if (bAuthoringChanged)
        {
            m_CurrentFrame = ClampFlipbookFrame(InitialFrame);
            m_FrameAccumulator = 0.0f;
        }
    }

    uint32_t BoardComponent::ClampFlipbookFrame(uint32_t frameIndex) const
    {
        if (FrameCount == 0)
        {
            return 0;
        }

        const uint32_t lastFrame = FrameCount - 1u;
        return frameIndex > lastFrame ? lastFrame : frameIndex;
    }

    bool BoardComponent::ApplyCurrentFlipbookFrameToUV()
    {
        EnsureFlipbookCacheReady();
        if (!m_bFlipbookCacheValid)
        {
            return false;
        }

        const uint32_t clampedFrame = ClampFlipbookFrame(m_CurrentFrame);
        if (m_CurrentFrame != clampedFrame)
        {
            m_CurrentFrame = clampedFrame;
        }

        const size_t atlasFrameIndex = static_cast<size_t>(FirstFrameIndex) + static_cast<size_t>(m_CurrentFrame);
        if (atlasFrameIndex >= m_FlipbookUVRects.size())
        {
            return false;
        }

        const Math::Vector4 nextUVRect = m_FlipbookUVRects[atlasFrameIndex];
        if (UVRectProp.Get() == nextUVRect)
        {
            return false;
        }

        UVRectProp = nextUVRect;
        MarkRenderStateDirty();
        return true;
    }

    void BoardComponent::AdvanceFlipbook(float deltaTime)
    {
        if (!m_bPlaying ||
            deltaTime <= 0.0f ||
            !std::isfinite(deltaTime))
        {
            return;
        }

        EnsureFlipbookCacheReady();
        if (!m_bFlipbookCacheValid ||
            FrameCount <= 1 ||
            FramesPerSecond <= 0.0f ||
            !std::isfinite(FramesPerSecond))
        {
            return;
        }

        m_FrameAccumulator += deltaTime;
        const float framesToAdvanceFloat = m_FrameAccumulator * FramesPerSecond;
        if (framesToAdvanceFloat < 1.0f)
        {
            return;
        }

        uint64_t framesToAdvance = framesToAdvanceFloat >= static_cast<float>(std::numeric_limits<uint64_t>::max())
                                       ? std::numeric_limits<uint64_t>::max()
                                       : static_cast<uint64_t>(framesToAdvanceFloat);
        if (framesToAdvance == 0)
        {
            return;
        }

        m_FrameAccumulator -= static_cast<float>(framesToAdvance) / FramesPerSecond;
        if (m_FrameAccumulator < 0.0f)
        {
            m_FrameAccumulator = 0.0f;
        }

        uint32_t nextFrame = m_CurrentFrame;
        if (bLoop)
        {
            nextFrame = static_cast<uint32_t>(
                (static_cast<uint64_t>(m_CurrentFrame) + framesToAdvance) % static_cast<uint64_t>(FrameCount));
        }
        else
        {
            const uint64_t lastFrame = static_cast<uint64_t>(FrameCount - 1u);
            const uint64_t advancedFrame = static_cast<uint64_t>(m_CurrentFrame) + framesToAdvance;
            if (advancedFrame >= lastFrame)
            {
                nextFrame = FrameCount - 1u;
                m_bPlaying = false;
                m_FrameAccumulator = 0.0f;
            }
            else
            {
                nextFrame = static_cast<uint32_t>(advancedFrame);
            }
        }

        if (nextFrame != m_CurrentFrame)
        {
            m_CurrentFrame = nextFrame;
            ApplyCurrentFlipbookFrameToUV();
        }
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
        outProxy.UVRect = UVRectProp;
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
        outMatrix = Math::MatrixUtils::CreateWorldRowVector(worldTransform.position,
                                                            worldTransform.rotation,
                                                            worldTransform.scale);
    }

} // namespace NorvesLib::Core::Component

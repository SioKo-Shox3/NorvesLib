#pragma once

/**
 * @file RenderExtensionShadowCaster.h
 * @brief 拡張から影を落とす物の束を受け取る。
 *
 * 拡張は影のマップへ直接描かず、RenderThread で View の描画前に Viewport ごとに 1 回提出する。
 * 一覧はそのフレームの描画が終わるまでバッファの参照を持つため、拡張は参照を手放すだけでよい。
 * 頂点は Mesh3DVertex（位置 3・法線 3・UV 2、32 バイト）、インデックスは 32 bit とする。
 * 頂点・インデックスのバッファにはそれぞれ VertexBuffer・IndexBuffer と StorageBuffer の用途を付け、
 * VSM が読めるように装置のアドレスを取れるように作る。アドレスを取れない束は VSM だけが省く。
 * LocalBounds は全頂点を含める。ContentRevision は中身を書き換えるたびに増やし、VSM の再描画を促す。
 */

#include "Container/Span.h"
#include "Container/VariableArray.h"
#include "Math/Matrix4x4.h"
#include "RHI/RHITypes.h"
#include "Rendering/RenderTypes.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    struct ViewRenderContext;
    class RenderExtensionRegistry;

    constexpr uint32_t RENDER_EXTENSION_SHADOW_CASTER_MAX_DRAWS = 4096;
    constexpr uint32_t RENDER_EXTENSION_SHADOW_CASTER_MAX_INSTANCES = 65536;

    /** @brief 拡張が提出する、同じメッシュと材質を使う影の束。 */
    struct RenderExtensionShadowCasterBatch
    {
        RHI::BufferPtr VertexBuffer;
        RHI::BufferPtr IndexBuffer;
        uint32_t FirstIndex = 0;
        uint32_t IndexCount = 0;
        uint32_t VertexOffset = 0;
        BoundingBox LocalBounds;
        // MeshProxy::WorldTransform と同じワールド行列。Submit の呼び出し中だけ有効でよい。
        Container::Span<const Math::Matrix4x4> Instances;
        // 無効なら不透明。Masked の材質なら切り抜く。
        MaterialHandle Material;
        // 負なら材質の値を使う。
        float CoverageMultiplier = -1.0f;
        bool bPointLightShadow = false;
        // 拡張内で束ごとに異なる安定した値。0 のときは提出の順番を使う。
        uint64_t Key = 0;
        uint64_t ContentRevision = 0;
    };

    /** @brief 検査を通ったインスタンスのワールド行列と境界。 */
    struct RenderExtensionShadowCasterInstance
    {
        // CopyToShaderData の並び。GPUSceneInstanceData::World と同じ。
        float World[16] = {};
        BoundingBox WorldBounds;
        // WorldBounds の中心と半対角線。半径が 0 の場合は 1e-4 m。
        BoundingSphere WorldSphere;
    };

    /** @brief 検査を通った束と、フレーム内のインスタンスの範囲。 */
    struct RenderExtensionShadowCasterDraw
    {
        RHI::BufferPtr VertexBuffer;
        RHI::BufferPtr IndexBuffer;
        uint32_t FirstIndex = 0;
        uint32_t IndexCount = 0;
        uint32_t VertexOffset = 0;
        BoundingBox LocalBounds;
        MaterialHandle Material;
        float CoverageMultiplier = -1.0f;
        bool bPointLightShadow = false;
        uint64_t Key = 0;
        uint64_t ContentRevision = 0;
        uint32_t FirstInstance = 0;
        uint32_t InstanceCount = 0;
    };

    /** @brief 1 回の View 描画で使う影の束。Clear は配列の容量を保つ。 */
    class RenderExtensionShadowCasterFrame
    {
    public:
        void Clear()
        {
            m_Draws.clear();
            m_Instances.clear();
        }

        const Container::VariableArray<RenderExtensionShadowCasterDraw>& GetDraws() const { return m_Draws; }
        const Container::VariableArray<RenderExtensionShadowCasterInstance>& GetInstances() const { return m_Instances; }
        bool IsEmpty() const { return m_Draws.empty(); }

    private:
        friend class RenderExtensionShadowCasterSink;
        friend class RenderExtensionRegistry;
        Container::VariableArray<RenderExtensionShadowCasterDraw> m_Draws;
        Container::VariableArray<RenderExtensionShadowCasterInstance> m_Instances;
    };

    /** @brief 1 拡張の提出口。呼び出し中だけ使い、name と loggedReasons をその間有効に保つ。 */
    class RenderExtensionShadowCasterSink
    {
    public:
        RenderExtensionShadowCasterSink(RenderExtensionShadowCasterFrame& frame, const char* name, uint32_t& loggedReasons);
        RenderExtensionShadowCasterSink(const RenderExtensionShadowCasterSink&) = delete;
        RenderExtensionShadowCasterSink& operator=(const RenderExtensionShadowCasterSink&) = delete;
        bool Submit(const RenderExtensionShadowCasterBatch& batch);

    private:
        bool Reject(uint32_t reasonBit, const char* reason);
        RenderExtensionShadowCasterFrame& m_Frame;
        const char* m_Name;
        uint32_t& m_LoggedReasons;
        uint64_t m_NameHash = 0;
        uint64_t m_SubmissionIndex = 0;
    };

    /** @brief 影を落とす束を提出する描画拡張の受け口。 */
    class IRenderExtensionShadowCaster
    {
    public:
        virtual ~IRenderExtensionShadowCaster() = default;

        /** @brief RenderThread で View の描画前に呼ぶ。context の FrameResources から借りてよい。登録表へ再入しない。 */
        virtual void SubmitShadowCasters(ViewRenderContext& context, RenderExtensionShadowCasterSink& sink) = 0;
    };
} // namespace NorvesLib::Core::Rendering

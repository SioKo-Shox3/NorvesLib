#pragma once

#include "Rendering/MaterialTypes.h"
#include "Container/PointerTypes.h"
#include "Container/UnorderedMap.h"
#include "RHI/RHITypes.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief GBuffer に使うシェーダーの組。
     *
     * 各シェーダーには ShaderManager に渡すファイル名を指定する。エンジンの
     * シェーダー置き場からの相対パスまたは絶対パスを使う。VertexShader と
     * SkinnedVertexShader が空なら、それぞれ gbuffer.vert と skinned_gbuffer.vert を使う。
     * FragmentShader は必須。
     *
     * フラグメントシェーダーは Assets/Shaders/Common/GBufferEncoding.glsl の
     * GBufferEncodeNormal などの書き込み関数に従い、Albedo・Normal・Material・
     * Emissive・Velocity の 5 枚へ書き込む。set 0 の UBO、PBR テクスチャ 6 枚、
     * インスタンス、スキン用 binding 8〜10、VT フィードバックと頂点入力の配置は
     * 標準の GBuffer シェーダーと同じものを受ける。
     */
    struct GBufferProgramDesc
    {
        Container::String Name;
        Container::String VertexShader;
        Container::String FragmentShader;
        Container::String SkinnedVertexShader;
    };

    struct GBufferPipelineKey
    {
        GBufferProgramId Program = kStandardGBufferProgramId;
        RHI::CullMode Cull = RHI::CullMode::Back;
        bool bAlphaClip = false;
        RHI::PolygonMode Polygon = RHI::PolygonMode::Fill;
        bool bSkinned = false;

        bool operator==(const GBufferPipelineKey& other) const = default;

        struct Hasher
        {
            size_t operator()(const GBufferPipelineKey& key) const noexcept
            {
                uint64_t hash = 14695981039346656037ull;
                constexpr uint64_t prime = 1099511628211ull;
                hash = (hash ^ key.Program) * prime;
                hash = (hash ^ static_cast<uint32_t>(key.Cull)) * prime;
                hash = (hash ^ static_cast<uint32_t>(key.bAlphaClip)) * prime;
                hash = (hash ^ static_cast<uint32_t>(key.Polygon)) * prime;
                hash = (hash ^ static_cast<uint32_t>(key.bSkinned)) * prime;
                return static_cast<size_t>(hash);
            }
        };
    };

    /** @brief 材質と描画の種類からパイプラインの鍵を作る。 */
    GBufferPipelineKey MakeGBufferPipelineKey(const MaterialResourceData* material, bool bSkinned, bool bWireframe);

    /** @brief 描画スレッド専用のパイプライン表。失敗も Clear まで記憶し、ロックは行わない。 */
    template <typename Pipeline>
    class TGBufferPipelineTable
    {
    public:
        /** @brief creator は鍵を受け取り、成功した値または null を返す。 */
        template <typename Creator>
        Pipeline GetOrCreate(const GBufferPipelineKey& key, Creator&& creator)
        {
            const auto found = m_Pipelines.find(key);
            if (found != m_Pipelines.end())
            {
                return found->second;
            }

            Pipeline pipeline = creator(key);
            m_Pipelines.emplace(key, pipeline);
            if (pipeline)
            {
                ++m_CreatedCount;
            }
            return pipeline;
        }

        /** @brief Clear 以降に作成に成功した数。 */
        uint32_t GetCreatedCount() const { return m_CreatedCount; }

        /** @brief 描画先の render pass の変更時や停止時に、成功と失敗の記録を消す。 */
        void Clear()
        {
            m_Pipelines.clear();
            m_CreatedCount = 0;
        }

    private:
        Container::UnorderedMap<GBufferPipelineKey, Pipeline, GBufferPipelineKey::Hasher> m_Pipelines;
        uint32_t m_CreatedCount = 0;
    };

    using GBufferPipelineTable = TGBufferPipelineTable<RHI::PipelinePtr>;

    /** @brief ゲームスレッドの登録操作を描画スレッドへ反映する登録簿。 */
    class GBufferProgramRegistry
    {
    public:
        GBufferProgramRegistry();
        ~GBufferProgramRegistry();

        GBufferProgramRegistry(const GBufferProgramRegistry&) = delete;
        GBufferProgramRegistry& operator=(const GBufferProgramRegistry&) = delete;

        /** @brief ゲームスレッドで登録する。初期化の窓だけ即時反映する。別名との ID 衝突も拒否する。 */
        bool Register(const GBufferProgramDesc& desc);
        /** @brief ゲームスレッドで登録解除する。初期化の窓だけ即時反映する。 */
        bool Unregister(const char* name);

        /** @brief ゲームスレッドが走り出す前に初期化の窓を閉じる。 */
        void CloseInitializationWindow();
        /** @brief 描画スレッドで保留操作を反映し、初期化の窓を閉じる。 */
        void ApplyPending();

        /** @brief 描画スレッド専用。結果は次の反映または破棄まで有効。初期化中の即時反映も含む。 */
        const GBufferProgramDesc* Find(GBufferProgramId id) const;
        /** @brief 描画スレッド専用。反映済みの内容が変わるたび増える版。 */
        uint64_t GetRevision() const;

    private:
        struct State;
        Container::TUniquePtr<State> m_State;
    };
} // namespace NorvesLib::Core::Rendering

// RenderThread が最後に描いたフレームの物体の変換を覚え、描画がゲームのフレームを飛ばしたときに、
// パケットの前の変換（velocity の基準）をそのフレームのものへ付け替える。
#pragma once

#include "Rendering/FramePacket.h"
#include "Rendering/SkinnedPoseHistory.h"
#include "Container/Containers.h"
#include "Math/Matrix4x4.h"

#include <cstdint>
#include <cstring>

namespace NorvesLib::Core::Rendering
{
    /** @brief RenderedObjectHistory::Apply の結果（velocity の基準がどのフレームを指すか）。 */
    struct RenderedObjectHistoryResult
    {
        // パケットの前の変換が指すゲームのフレーム番号
        uint64_t PreviousFrameNumber = 0u;
        // 描いた物体をすべて PreviousFrameNumber のフレームへ揃えられたか
        bool bComplete = true;
        // 前の変換を付け替えたか
        bool bRebased = false;
        // 付け替えられなかった（MeshProxy と照合できなかった）インスタンスの数
        uint32_t UnresolvedInstanceCount = 0u;
    };

    /**
     * @brief 最後に描いたフレームの物体の変換を覚え、飛んだフレームの velocity の基準をそこへ揃える
     *
     * GameThread はパケットの前の変換（MeshProxy・インスタンスの PreviousWorld、スキニングの前の変換と
     * パレット、MegaGeometry の前の変換）を直前のゲームのフレームのものにする。RenderThread は未描画の
     * パケットを新しいパケットで置き換えるので、描いたフレームが 10 → 12 と飛ぶと、velocity は 11 からの
     * 動きになり、10 で書いた TAA の履歴とは基準が食い違う（物体が 11 で動いて 12 で止まると velocity は0）。
     *
     * Apply は、パケットが最後に描いたフレームの直後でなく、bRewriteOnGap のときに、前の変換を最後に
     * 描いたフレームの変換へ付け替える。インスタンスは World・PreviousWorld が一致する MeshProxy の
     * ComponentId で見分ける。最後に描いたフレームに無かった物体（新しく現れた物体）は付け替えない
     * （その物体は履歴に写っていない）。照合できないインスタンスがあれば bComplete を false にする。
     * MegaGeometry の前の変換は、bRewriteOnGap に関わらず飛んだフレームでは最後に描いたフレームの変換にする。
     * 付け替えた後、このパケットの変換を次のフレームのために覚える。
     */
    class RenderedObjectHistory
    {
    public:
        RenderedObjectHistoryResult Apply(FramePacket& packet, bool bRewriteOnGap)
        {
            return Apply(packet, packet.Scene.MeshProxies, bRewriteOnGap);
        }

        /**
         * @param meshProxies 照合と記録に使う MeshProxy の配列。差分の同期では RenderThread の写し（パケットは全量を持たない）。
         *        パケットの Scene.MeshProxies 以外を渡したときは、その配列の前の変換は書き換えない
         *        （RenderThread の消費者は MeshProxy の前の変換を読まず、インスタンスの前の変換だけを付け替えれば足りる）
         */
        RenderedObjectHistoryResult Apply(FramePacket& packet,
                                          const Container::VariableArray<MeshProxy>& meshProxies,
                                          bool bRewriteOnGap)
        {
            RenderedObjectHistoryResult result;
            result.PreviousFrameNumber = packet.FrameNumber > 0u ? packet.FrameNumber - 1u : 0u;
            const bool bGap = m_bValid && packet.FrameNumber > m_FrameNumber + 1u;
            if (m_bValid && packet.FrameNumber == m_FrameNumber + 1u)
            {
                result.PreviousFrameNumber = m_FrameNumber;
            }
            else if (bGap && bRewriteOnGap)
            {
                Rebase(packet, meshProxies, result);
                result.PreviousFrameNumber = m_FrameNumber;
                result.bRebased = true;
            }
            // MegaGeometry の前の変換は TAA の有無に関わらず最後に描いたフレームの変換にする（差分の同期の
            // MegaGeometryMirror::NoteDrawn と同じ規約。続けて描くフレームでは GameThread の値と同じ）
            if (bGap)
            {
                RebaseMegaGeometry(packet);
            }
            Record(packet, meshProxies);
            return result;
        }

        /** @brief 覚えた変換を捨てる（次のパケットは付け替えない）。 */
        void Reset()
        {
            m_bValid = false;
            m_FrameNumber = 0u;
            m_MeshWorlds.clear();
            m_MegaGeometryWorlds.clear();
            m_SkinnedStates.Reset();
        }

        bool IsValid() const { return m_bValid; }
        uint64_t GetFrameNumber() const { return m_FrameNumber; }

    private:
        static uint64_t HashMatrixBytes(const float (&values)[16])
        {
            // FNV-1a（行列のビット列が同じものだけを同じ候補にする）。
            uint64_t hash = 14695981039346656037ull;
            const auto* bytes = reinterpret_cast<const uint8_t*>(values);
            for (size_t index = 0; index < sizeof(values); ++index)
            {
                hash = (hash ^ bytes[index]) * 1099511628211ull;
            }
            return hash;
        }

        static bool SameMatrix(const float (&lhs)[16], const float (&rhs)[16])
        {
            return std::memcmp(lhs, rhs, sizeof(lhs)) == 0;
        }

        void Rebase(FramePacket& packet,
                    const Container::VariableArray<MeshProxy>& meshProxies,
                    RenderedObjectHistoryResult& result) const
        {
            // インスタンスの World から、同じ World・PreviousWorld の MeshProxy を引く。
            Container::UnorderedMap<uint64_t, Container::VariableArray<uint32_t>> proxiesByWorld;
            for (uint32_t index = 0; index < static_cast<uint32_t>(meshProxies.size()); ++index)
            {
                if (meshProxies[index].ComponentId != 0u)
                {
                    proxiesByWorld[HashMatrixBytes(meshProxies[index].WorldTransform.values)].push_back(index);
                }
            }

            Container::VariableArray<uint8_t> visited(packet.InstanceData.size(), 0u);
            for (const DrawCommand& command : packet.DrawCommands)
            {
                const DrawParams& draw = command.Draw;
                if (draw.PayloadKind != DrawPayloadKind::Mesh)
                {
                    continue;
                }
                const uint64_t instanceCount = draw.bInstanced ? draw.InstanceCount : 1u;
                for (uint64_t offset = 0; offset < instanceCount; ++offset)
                {
                    const uint64_t dataIndex = static_cast<uint64_t>(draw.InstanceDataOffset) + offset;
                    if (dataIndex >= packet.InstanceData.size() || visited[dataIndex] != 0u)
                    {
                        continue;
                    }
                    visited[dataIndex] = 1u;
                    GPUSceneInstanceData& data = packet.InstanceData[dataIndex];
                    const Math::Matrix4x4* renderedWorld = nullptr;
                    if (!ResolveRenderedMeshWorld(data, meshProxies, proxiesByWorld, renderedWorld))
                    {
                        ++result.UnresolvedInstanceCount;
                        result.bComplete = false;
                        continue;
                    }
                    if (renderedWorld)
                    {
                        std::memcpy(data.PreviousWorld, renderedWorld->values, sizeof(data.PreviousWorld));
                    }
                }
            }

            if (&meshProxies == &packet.Scene.MeshProxies)
            {
                for (MeshProxy& proxy : packet.Scene.MeshProxies)
                {
                    const auto found = m_MeshWorlds.find(proxy.ComponentId);
                    if (proxy.ComponentId != 0u && found != m_MeshWorlds.end())
                    {
                        proxy.PreviousWorldTransform = found->second;
                    }
                }
            }
            m_SkinnedStates.Apply(packet,true);
        }

        // MegaGeometry は物体（ObjectId）ごとに覚える（散布の部品は 1 つの ComponentId が多数の物体を持つ）
        void RebaseMegaGeometry(FramePacket& packet) const
        {
            for (MegaGeometryProxy& proxy : packet.Scene.MegaGeometryProxies)
            {
                const auto found =
                    proxy.ObjectId != 0u ? m_MegaGeometryWorlds.find(proxy.ObjectId) : m_MegaGeometryWorlds.end();
                proxy.PreviousWorldTransform =
                    found != m_MegaGeometryWorlds.end() ? found->second : proxy.WorldTransform;
            }
        }

        /**
         * @brief インスタンスの最後に描いたフレームの変換を求める
         *
         * 照合できたら true。そのうち最後に描いたフレームに無かった物体は outRenderedWorld を null にする。
         * World・PreviousWorld が同じ MeshProxy が複数あり、最後に描いたフレームの変換が食い違うときは
         * 見分けられないので false。
         */
        bool ResolveRenderedMeshWorld(
            const GPUSceneInstanceData& data,
            const Container::VariableArray<MeshProxy>& meshProxies,
            const Container::UnorderedMap<uint64_t, Container::VariableArray<uint32_t>>& proxiesByWorld,
            const Math::Matrix4x4*& outRenderedWorld) const
        {
            outRenderedWorld = nullptr;
            const auto candidates = proxiesByWorld.find(HashMatrixBytes(data.World));
            if (candidates == proxiesByWorld.end())
            {
                return false;
            }
            bool bMatched = false;
            for (const uint32_t proxyIndex : candidates->second)
            {
                const MeshProxy& proxy = meshProxies[proxyIndex];
                if (!SameMatrix(proxy.WorldTransform.values, data.World) ||
                    !SameMatrix(proxy.PreviousWorldTransform.values, data.PreviousWorld))
                {
                    continue;
                }
                const auto found = m_MeshWorlds.find(proxy.ComponentId);
                const Math::Matrix4x4* renderedWorld = found != m_MeshWorlds.end() ? &found->second : nullptr;
                if (bMatched)
                {
                    const bool bSame = renderedWorld == outRenderedWorld ||
                                       (renderedWorld && outRenderedWorld &&
                                        SameMatrix(renderedWorld->values, outRenderedWorld->values));
                    if (!bSame)
                    {
                        return false;
                    }
                    continue;
                }
                bMatched = true;
                outRenderedWorld = renderedWorld;
            }
            return bMatched;
        }

        void Record(const FramePacket& packet, const Container::VariableArray<MeshProxy>& meshProxies)
        {
            m_MeshWorlds.clear();
            for (const MeshProxy& proxy : meshProxies)
            {
                if (proxy.ComponentId != 0u)
                {
                    m_MeshWorlds[proxy.ComponentId] = proxy.WorldTransform;
                }
            }
            m_MegaGeometryWorlds.clear();
            for (const MegaGeometryProxy& proxy : packet.Scene.MegaGeometryProxies)
            {
                if (proxy.ObjectId != 0u)
                {
                    m_MegaGeometryWorlds[proxy.ObjectId] = proxy.WorldTransform;
                }
            }
            m_SkinnedStates.Record(packet);
            m_FrameNumber = packet.FrameNumber;
            m_bValid = true;
        }

        bool m_bValid = false;
        uint64_t m_FrameNumber = 0u;
        // 最後に描いたフレームの変換（MeshProxy は ComponentId、MegaGeometry は ObjectId、スキニングは元の MeshComponent の ID ごと）。
        Container::UnorderedMap<uint64_t, Math::Matrix4x4> m_MeshWorlds;
        Container::UnorderedMap<uint64_t, Math::Matrix4x4> m_MegaGeometryWorlds;
        SkinnedPoseHistory m_SkinnedStates;
    };
} // namespace NorvesLib::Core::Rendering

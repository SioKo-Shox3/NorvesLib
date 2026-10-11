#pragma once

#include "RenderTypes.h"
#include "SceneProxy.h"
#include "DrawCommand.h"
#include "Rendering/DebugDrawQueue.h"
#include "ViewportSnapshot.h"
#include "Rendering/FrameCaptureTypes.h"
#include "Rendering/RTGIContract.h"
#include "Rendering/PointShadowSnapshot.h"
#include "Debug/Stats.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Thread/Atomic.h"
#include "RHI/IAccelerationStructure.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    // 前方宣言(overlay パスは非所有ビューとして借用ポインタのみ保持する)
    class IViewPass;

    /**
     * @brief フレームパケットの状態
     */
    enum class FramePacketState : uint8_t
    {
        Empty,     // 空（書き込み可能）
        Writing,   // 書き込み中
        Ready,     // 読み取り可能
        Queued,    // RenderThread待機中
        Reading,   // 読み取り中
        Recycling  // Clear中の一時状態
    };

    // ========================================
    // FramePacket
    // ========================================

    /**
     * @brief フレーム統計スナップショット
     */
    struct FrameStatsSnapshot
    {
        Debug::RenderingStats GameThreadStats;
        uint32_t VisibleObjects = 0;
        uint32_t BatchCount = 0;
        uint32_t SkinnedGBufferRecordedDraws = 0;
        uint32_t SkinnedShadowRecordedDraws = 0;
        uint32_t InstancedDrawCalls = 0;
        uint32_t SavedDrawCalls = 0;
        float CullingTimeMs = 0.0f;
        float BatchingTimeMs = 0.0f;
        bool bGameThreadTimingsAvailable = false;
    };

    /// 影を落とす不透明物体のinstance mask。影・DDGI・RTGIの光線はこのbitだけを調べる。
    /// Assets/Shaders/Common/RayTracingInstanceMask.glslと一致させる。
    inline constexpr uint8_t RayTracingInstanceMaskShadowCaster = 0x01u;
    /// 影を落とさない不透明物体のinstance mask。全bitで調べるパストレーサーだけが当たる。
    inline constexpr uint8_t RayTracingInstanceMaskNonShadowCaster = 0x02u;

    /**
     * @brief レイトレーシング用シーンのフレームスナップショット
     *
     * BLAS参照とTLASをFramePacketへ格納し、RenderThreadへ値として渡します。
     * 不透明・マスクの描画をすべて含め、影を落とすかどうかはinstance maskで区別します。
     */
    struct RayTracingSceneInstanceSnapshot
    {
        MeshDataHandle MeshHandle;
        /** @brief 元の描画のObjectIdと、その描画の中のinstance番号。フレームをまたぐ対応付けに使う。 */
        uint64_t ObjectId = 0;
        uint32_t ObjectInstanceIndex = 0;
        RHI::BufferPtr SourceVertexBuffer;
        RHI::BufferPtr SourceIndexBuffer;
        RHI::BufferPtr AccelerationStructureVertexBuffer;
        RHI::BufferPtr AccelerationStructureIndexBuffer;
        /**
         * @brief 頂点・インデックスが共有バッファ（ジオメトリのプール）の区画にあるときの、区画の先頭（バイト）
         *
         * VertexOffset・IndexOffset はこの先頭からの位置。専用のバッファを持つメッシュでは 0。
         * 加速構造・シェーダーが見るアドレスは、バッファのアドレス + この先頭 + VertexOffset・IndexOffset の位置になる。
         */
        uint64_t VertexBufferOffsetBytes = 0;
        uint64_t IndexBufferOffsetBytes = 0;
        /** @brief 共有バッファの区画を持つ MegaMesh の番号（0 は無し）。区画が別のメッシュに使い回されたときに BLAS を取り違えない */
        uint64_t MegaMeshId = 0;
        /**
         * @brief 共有バッファの区画の共有の持ち主。このスナップショットが消えるまで区画を空きへ戻さない
         *
         * フレームの描画（BLAS の構築・RTGI/DDGI の頂点・インデックスの参照）が終わる前に、元のメッシュが解放されて
         * 区画が別のメッシュへ使い回されないようにする。専用のバッファを持つメッシュでは null。
         */
        Container::TSharedPtr<void> GeometryRegionOwner;
        uint32_t IndexOffset = 0;
        uint32_t IndexCount = 0;
        uint32_t VertexOffset = 0;
        uint32_t VertexCount = 0;
        uint32_t VertexStride = 0;
        bool bGeometryOpaque = true;
        RHI::AccelerationStructureInstanceDesc Instance;
        float PreviousTransform[12] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f};
        bool bHasPreviousTransform = false;
        RHI::AccelerationStructurePtr BottomLevel;
        RayTracingHitMaterialSnapshot Material;
    };

    struct RayTracingSceneSnapshot
    {
        Container::VariableArray<RayTracingSceneInstanceSnapshot> Instances;
        RHI::AccelerationStructurePtr TopLevel;

        /**
         * @brief TLASと全インスタンスのray query入力が揃っているか判定
         *
         * R5のresource所有権は変更せず、FramePacketに保持された参照だけを検査する。
         */
        bool IsComplete() const
        {
            if (!TopLevel || Instances.empty())
            {
                return false;
            }

            for (const RayTracingSceneInstanceSnapshot& instance : Instances)
            {
                if (!instance.BottomLevel || !instance.AccelerationStructureVertexBuffer ||
                    !instance.AccelerationStructureIndexBuffer)
                {
                    return false;
                }
            }
            return true;
        }

        /**
         * @brief 影を落とす物体（影・DDGI・RTGIの光線が当たる物体）があるか
         *
         * 影を落とさない物体だけのシーンでは、影・DDGI・RTGIは従来の空のシーンと同じくfallbackする。
         */
        bool HasShadowCasters() const
        {
            for (const RayTracingSceneInstanceSnapshot& instance : Instances)
            {
                if ((instance.Instance.mask & RayTracingInstanceMaskShadowCaster) != 0u)
                {
                    return true;
                }
            }
            return false;
        }

        void Clear()
        {
            TopLevel.reset();
            Instances.clear();
        }
    };

    /**
     * @brief MegaGeometry の proxy 配列の差分（RenderThread の写しへ当てる）
     *
     * GameThread の SceneView の配列の「位置」を鍵にした状態の差分。Indices の位置の proxy を Proxies の同じ順で書き、
     * 配列の大きさを Size にそろえる。確認されていない送信の分も含む累積なので、何回当てても、飛ばされたパケットがあっても
     * 同じ結果になる。bBaseline のときは Indices が 0〜Size-1 の全部で、写しを捨ててから当てる（世代が替わった直後など）。
     * bActive が false のパケット（手で組んだもの・--mega-scene-full-sync）は、Scene.MegaGeometryProxies を全量として読む。
     */
    struct MegaSceneDelta
    {
        bool bActive = false;
        bool bBaseline = false;
        uint32_t Size = 0;
        Container::VariableArray<uint32_t> Indices;
        Container::VariableArray<MegaGeometryProxy> Proxies;
        // 検査 MEGA_SCENE_DELTA: 送った時点の GameThread の配列の hash。RenderThread が写しの hash と照合する
        bool bVerify = false;
        uint64_t VerifyHash = 0;

        void Clear()
        {
            bActive = false;
            bBaseline = false;
            Size = 0;
            Indices.clear();
            Proxies.clear();
            bVerify = false;
            VerifyHash = 0;
        }
    };

    /**
     * @brief フレームパケット
     *
     * 1フレーム分の描画データを格納する構造体。
     * Triple Bufferingで使用され、GameThreadとRenderThreadの
     * 同期を最小限に抑えます。
     */
    struct FramePacket
    {
        // ========================================
        // フレーム情報
        // ========================================

        uint64_t FrameNumber = 0; // フレーム番号
        float DeltaTime = 0.0f;   // 前フレームからの経過時間
        double TotalTime = 0.0;   // アプリケーション開始からの経過時間

        /** @brief 決定的な撮影（--capture-deterministic）のフレームか。経過時間は 1/60 秒の固定刻み。 */
        bool bDeterministicCapture = false;

        /** @brief 決定的な撮影のエポックの最初のフレームか。時間的な状態を捨てて数え直す合図。 */
        bool bTemporalEpochStart = false;

        /** @brief 決定的な撮影のエポックの最初のフレームから数えたフレーム数（エポック前は 0）。 */
        uint64_t EpochFrameIndex = 0;

        /** @brief RTGIの明示的な有効化。資源が不完全ならfallbackへ戻る。 */
        bool bRTGIEnabled = true;

        /** @brief シーン構成・ジオメトリ・環境のFramePacket値revision。 */
        uint64_t SceneRevision = 0;

        /** @brief ライト配列のFramePacket値revision。 */
        uint64_t LightRevision = 0;

        /** @brief 送った時点のシーンの世代。SceneView が内容を入れ替えたときに進む。差分は同じ世代のものだけが当たる。 */
        uint64_t SceneGeneration = 0;

        /** @brief 送った番号。FinishWrite が 1 から順に振る（0 は未送信）。RenderThread の適用の確認の単位。 */
        uint64_t SendSerial = 0;

        /** @brief GameThread が非破壊 snapshot した capture request 値。 */
        FrameCaptureRequestSnapshot CaptureRequest;

        // ========================================
        // シーンデータ
        // ========================================

        bool bHasMainCamera = false;
        bool bHasPreviousMainCamera = false;
        CameraProxy PreviousMainCamera;
        SceneProxy Scene;
        RayTracingSceneSnapshot RayTracingScene;

        /** @brief MegaGeometry の差分。有効なとき、Scene.MegaGeometryProxies は空で、消費者は RenderThread の写しを読む */
        MegaSceneDelta MegaDelta;

        /** @brief キューブシャドウを描く点光源の選択と6面の行列（Scene.LightProxiesとMainCameraから作る） */
        PointShadowSnapshot PointShadows;

        /**
         * @brief TLAS snapshotがray query入力として完全か判定
         *
         * R5の所有権とresource寿命は変更せず、FramePacket内の参照だけを判定する。
         */
        bool HasCompleteRayTracingScene() const
        {
            return RayTracingScene.IsComplete();
        }

        /**
         * @brief MegaGeometry のレイトレーシングのインスタンスを RenderThread が写しから足すか
         *
         * 差分の同期では、パケットは MegaGeometry の全量を持たないので、RenderThread が自分の写しから足す。
         * 連番のパストレーシング（メインカメラの SequenceFrame が 0 でない）は前の変換の持ち越しが GameThread の
         * snapshot の並びに依るため、GameThread で作る。
         */
        bool DefersMegaRayTracingToRenderThread() const
        {
            return MegaDelta.bActive && !(bHasMainCamera && Scene.MainCamera.SequenceFrame != 0);
        }

        // ========================================
        // DrawCommandスナップショット（GameThreadで生成、RenderThreadで読み取り専用）
        // ========================================

        /** @brief 全DrawCommandのスナップショット（Opaque→Transparent順の単一実体配列） */
        Container::VariableArray<DrawCommand> DrawCommands;

        /** @brief DrawCommands全体の範囲 */
        CommandRange DrawCommandRange;

        /** @brief DrawCommands内の不透明DrawCommand範囲 */
        CommandRange OpaqueCommandRange;

        /** @brief DrawCommands内の半透明DrawCommand範囲 */
        CommandRange TransparentCommandRange;

        /** @brief DrawCommandから参照するGPUシーンインスタンスデータ */
        /** @brief skinned drawが参照するFramePacket所有のmesh asset lease */
        Container::VariableArray<Container::TSharedPtr<const SkinnedMeshFrameLease>> SkinnedMeshFrameLeases;

        Container::VariableArray<GPUSceneInstanceData> InstanceData;

        /** @brief デバッグライン頂点スナップショット（GameThreadで生成、RenderThreadで読み取り専用） */
        Container::VariableArray<DebugLineVertex> DebugLineVertices;

        /** @brief 描画統計スナップショット（GameThreadで生成、RenderThreadで読み取り専用） */
        FrameStatsSnapshot Stats;

        /** @brief GameThreadが生成したDrawCommand数。RTの実描画DrawCallsとは別指標。 */
        uint32_t GeneratedDrawCommandCount = 0;

        // ========================================
        // View/Viewport render plan（新描画フロー用）
        // ========================================

        /** @brief Screen配下のView/Viewportごとの描画入力 */
        Container::VariableArray<ViewRenderPlan> Views;

        // ========================================
        // overlay パス（モジュール所有・非所有ビュー。GameThread が焼き RenderThread が消費）
        // ========================================

        /**
         * @brief このフレームで最終段に描く overlay パス集合(借用ポインタ)
         *
         * 寿命はモジュール側が所有する。GameThread が TickAll 後に書き込み、
         * RenderThread が RenderingCoordinator の seam で読む。空のときは描画シームが
         * 完全 no-op になる(F1 描画 baseline 不変条件の核)。
         */
        Container::VariableArray<IViewPass *> OverlayPasses;

        // ========================================
        // 状態管理
        // ========================================

        Thread::Atomic<uint8_t> State{static_cast<uint8_t>(FramePacketState::Empty)};

        // ========================================
        // メソッド
        // ========================================

        /**
         * @brief パケットをクリア
         */
        void Clear()
        {
            FrameNumber = 0;
            DeltaTime = 0.0f;
            TotalTime = 0.0;
            bDeterministicCapture = false;
            bTemporalEpochStart = false;
            EpochFrameIndex = 0;
            bRTGIEnabled = true;
            SceneRevision = 0;
            LightRevision = 0;
            SceneGeneration = 0;
            SendSerial = 0;
            CaptureRequest = FrameCaptureRequestSnapshot{};
            bHasMainCamera = false;
            bHasPreviousMainCamera = false;
            PreviousMainCamera = CameraProxy{};
            Scene.Clear();
            RayTracingScene.Clear();
            MegaDelta.Clear();
            PointShadows.Clear();
            DrawCommands.clear();
            DrawCommandRange = CommandRange{};
            OpaqueCommandRange = CommandRange{};
            SkinnedMeshFrameLeases.clear();
            TransparentCommandRange = CommandRange{};
            InstanceData.clear();
            DebugLineVertices.clear();
            Stats = FrameStatsSnapshot{};
            GeneratedDrawCommandCount = 0;
            Views.clear();
            OverlayPasses.clear();
        }

        /**
         * @brief 現在の状態を取得
         */
        FramePacketState GetState() const
        {
            return static_cast<FramePacketState>(State.Load(std::memory_order_acquire));
        }

        /**
         * @brief 状態を設定
         */
        void SetState(FramePacketState newState)
        {
            State.Store(static_cast<uint8_t>(newState), std::memory_order_release);
        }

        /**
         * @brief 状態をアトミックに比較・交換
         * @param expected 期待される状態
         * @param desired 設定したい状態
         * @return 交換に成功した場合true
         */
        bool CompareExchangeState(FramePacketState expected, FramePacketState desired)
        {
            uint8_t expectedVal = static_cast<uint8_t>(expected);
            return State.CompareExchangeStrong(expectedVal, static_cast<uint8_t>(desired),
                                               std::memory_order_acq_rel);
        }
    };

    // ========================================
    // 連番の1フレームの前の値の固定
    // ========================================

    /**
     * @brief 連番の1フレームの間、各パケットへ同じ前の値を書くためのGameThread側の状態
     *
     * パストレーサーの連番の1フレームは複数のパケットにわたって累積する。RenderThreadはパケットを
     * 取りこぼすことがあるため、前後のカメラ・instance変換はGameThreadがパケットを書くたびに
     * 同じ値を入れ、どのパケットを描いても同じ前の値になるようにする。
     */
    struct PathTracingSequenceCarry
    {
        /** @brief フレームをまたいでinstanceを対応付ける鍵と変換（行優先3x4） */
        struct InstanceState
        {
            uint64_t ObjectId = 0;
            uint64_t MeshId = 0;
            uint32_t ObjectInstanceIndex = 0;
            uint32_t IndexOffset = 0;
            uint32_t IndexCount = 0;
            uint32_t VertexOffset = 0;
            /** @brief 同じ鍵が並んだときの出現順 */
            uint32_t Ordinal = 0;
            bool bHasTransform = false;
            float Transform[12] = {};
        };

        /** @brief 前の値を固定している連番のフレーム（0はなし） */
        uint64_t Frame = 0;
        bool bHasPreviousCamera = false;
        CameraProxy PreviousCamera;
        Container::VariableArray<InstanceState> PreviousInstances;

        /** @brief 直近に書いた連番のパケットの現在の状態（次の連番のフレームの前の値になる） */
        uint64_t LastFrame = 0;
        CameraProxy LastCamera;
        Container::VariableArray<InstanceState> LastInstances;

        void Reset()
        {
            Frame = 0;
            bHasPreviousCamera = false;
            PreviousCamera = CameraProxy{};
            PreviousInstances.clear();
            LastFrame = 0;
            LastCamera = CameraProxy{};
            LastInstances.clear();
        }
    };

    /**
     * @brief インスタンシング描画の各instanceの元の物体IDを、パケットのMeshProxyとの照合で求める
     *
     * インスタンシング描画のDrawParams::ObjectIdはバッチ先頭の物体のIDだけを持つ。連番の前の値の固定は
     * 物体ごとに対応付けるため、各instanceを、メッシュ・材質・影の有無が同じで、現在と前の変換と
     * カスタムデータがbit一致するMeshProxyへ対応付ける。一致が複数あるときはまだ使っていない候補を
     * バッチへの追加順（instanceの並び）で割り当てる。そうした候補同士は描画に使う値が同じなので、
     * 入れ替わっても像は変わらない。見つからないinstanceは0にする。
     */
    inline void ResolveInstancedDrawObjectIds(const FramePacket& packet, const DrawParams& draw,
                                              Container::VariableArray<uint64_t>& outObjectIds)
    {
        outObjectIds.clear();
        const uint32_t instanceCount = draw.bInstanced ? draw.InstanceCount : 1u;
        outObjectIds.resize(instanceCount, 0u);
        if (!draw.bInstanced ||
            static_cast<uint64_t>(draw.InstanceDataOffset) + instanceCount > packet.InstanceData.size())
        {
            return;
        }
        Container::VariableArray<const MeshProxy*> candidates;
        for (const MeshProxy& proxy : packet.Scene.MeshProxies)
        {
            if (!proxy.IsValid() || proxy.MeshHandle.Id != draw.MeshHandle.Id ||
                proxy.bCastShadow != draw.bCastShadow)
            {
                continue;
            }
            bool bMaterialMatches = false;
            for (uint32_t slot = 0; slot < MAX_MATERIAL_SLOTS; ++slot)
            {
                bMaterialMatches = bMaterialMatches || proxy.Materials[slot].Id == draw.MaterialHandle.Id;
            }
            if (bMaterialMatches)
            {
                candidates.push_back(&proxy);
            }
        }
        Container::VariableArray<uint8_t> used(candidates.size(), 0u);
        for (uint32_t instanceIndex = 0; instanceIndex < instanceCount; ++instanceIndex)
        {
            const GPUSceneInstanceData& data = packet.InstanceData[draw.InstanceDataOffset + instanceIndex];
            // 並びが変わっていなければ同じ位置の候補がまず一致する。
            for (size_t step = 0; step < candidates.size(); ++step)
            {
                const size_t index = (instanceIndex + step) % candidates.size();
                const MeshProxy& proxy = *candidates[index];
                if (used[index] ||
                    std::memcmp(proxy.WorldTransform.values, data.World, sizeof(data.World)) != 0 ||
                    std::memcmp(proxy.PreviousWorldTransform.values, data.PreviousWorld,
                                sizeof(data.PreviousWorld)) != 0 ||
                    std::memcmp(proxy.CustomData, data.CustomData, sizeof(data.CustomData)) != 0)
                {
                    continue;
                }
                used[index] = 1u;
                outObjectIds[instanceIndex] = proxy.ObjectId;
                break;
            }
        }
    }

    namespace PathTracingSequenceCarryDetail
    {
        inline bool SameKey(const PathTracingSequenceCarry::InstanceState& a,
                            const PathTracingSequenceCarry::InstanceState& b)
        {
            return a.ObjectId == b.ObjectId && a.MeshId == b.MeshId &&
                   a.ObjectInstanceIndex == b.ObjectInstanceIndex &&
                   a.IndexOffset == b.IndexOffset && a.IndexCount == b.IndexCount &&
                   a.VertexOffset == b.VertexOffset && a.Ordinal == b.Ordinal;
        }

        /** @brief パケットのinstanceの並びから鍵を作る。同じ鍵には出現順を振る。 */
        inline void MakeKeys(const RayTracingSceneSnapshot& scene,
                             Container::VariableArray<PathTracingSequenceCarry::InstanceState>& outStates)
        {
            outStates.clear();
            outStates.reserve(scene.Instances.size());
            for (const RayTracingSceneInstanceSnapshot& instance : scene.Instances)
            {
                PathTracingSequenceCarry::InstanceState state;
                state.ObjectId = instance.ObjectId;
                state.MeshId = instance.MeshHandle.Id;
                state.ObjectInstanceIndex = instance.ObjectInstanceIndex;
                state.IndexOffset = instance.IndexOffset;
                state.IndexCount = instance.IndexCount;
                state.VertexOffset = instance.VertexOffset;
                for (const PathTracingSequenceCarry::InstanceState& earlier : outStates)
                {
                    if (SameKey(earlier, state))
                    {
                        ++state.Ordinal;
                    }
                }
                outStates.push_back(state);
            }
        }

        /** @brief 鍵の全順序（物体ID・メッシュ・描画内番号・部分範囲・出現順の辞書順） */
        inline bool KeyLess(const PathTracingSequenceCarry::InstanceState& a,
                            const PathTracingSequenceCarry::InstanceState& b)
        {
            if (a.ObjectId != b.ObjectId) return a.ObjectId < b.ObjectId;
            if (a.MeshId != b.MeshId) return a.MeshId < b.MeshId;
            if (a.ObjectInstanceIndex != b.ObjectInstanceIndex)
                return a.ObjectInstanceIndex < b.ObjectInstanceIndex;
            if (a.IndexOffset != b.IndexOffset) return a.IndexOffset < b.IndexOffset;
            if (a.IndexCount != b.IndexCount) return a.IndexCount < b.IndexCount;
            if (a.VertexOffset != b.VertexOffset) return a.VertexOffset < b.VertexOffset;
            return a.Ordinal < b.Ordinal;
        }

        /**
         * @brief snapshotのinstanceを鍵の順へ並べ替え、customIndexを並びの番号に振り直す。
         *
         * 材質texture表・発光instance表・TLASはsnapshotの並びとcustomIndexから作られるため、
         * 物体ごとの状態が同じなら描画の並びが変わっても同じsnapshotになり、PTの累積が続く。
         */
        inline void SortByKey(RayTracingSceneSnapshot& scene,
                              Container::VariableArray<PathTracingSequenceCarry::InstanceState>& keys)
        {
            const size_t count = keys.size();
            Container::VariableArray<uint32_t> order(count);
            for (size_t index = 0; index < count; ++index)
            {
                order[index] = static_cast<uint32_t>(index);
            }
            // 鍵は出現順で一意にしてあるので、並べ替え後の順は元の並びに依らない。
            std::sort(order.begin(), order.end(), [&keys](uint32_t a, uint32_t b)
                      { return KeyLess(keys[a], keys[b]); });
            Container::VariableArray<RayTracingSceneInstanceSnapshot> sortedInstances;
            Container::VariableArray<PathTracingSequenceCarry::InstanceState> sortedKeys;
            sortedInstances.reserve(count);
            sortedKeys.reserve(count);
            for (size_t index = 0; index < count; ++index)
            {
                sortedInstances.push_back(std::move(scene.Instances[order[index]]));
                sortedInstances.back().Instance.customIndex = static_cast<uint32_t>(index);
                sortedKeys.push_back(keys[order[index]]);
            }
            scene.Instances = std::move(sortedInstances);
            keys = std::move(sortedKeys);
        }

        inline const PathTracingSequenceCarry::InstanceState* Find(
            const Container::VariableArray<PathTracingSequenceCarry::InstanceState>& states,
            const PathTracingSequenceCarry::InstanceState& key)
        {
            for (const PathTracingSequenceCarry::InstanceState& state : states)
            {
                if (SameKey(state, key))
                {
                    return &state;
                }
            }
            return nullptr;
        }
    }

    /**
     * @brief 連番の1フレームの前の値をパケットへ書く（GameThreadでパケットを書き終える直前に呼ぶ）
     *
     * メインカメラのSequenceFrameが0なら何もせず状態を捨てる。SequenceFrameが変わった最初のパケットで
     * 前の値を決めて覚える。直前に連番のパケットを書いていれば、その現在の状態（直前のフレームの最後の
     * 状態）を前の値にし、なければこのパケットの前の値を使う。同じSequenceFrameの間は、覚えた前の値で
     * パケットの前のカメラ・instance変換を上書きする。instanceは物体ID・メッシュ・部分範囲・描画内の
     * instance番号（同じ鍵は出現順）で対応付け、覚えた値にないinstanceは前の変換なし（動かない）にする。
     * 同じフレームの中で描画の並びが変わっても累積を捨てないよう、snapshotのinstanceを鍵の順へ並べ、
     * customIndexを並びの番号に振り直す。
     */
    inline void ApplyPathTracingSequenceCarry(FramePacket& packet, PathTracingSequenceCarry& carry)
    {
        using namespace PathTracingSequenceCarryDetail;
        const CameraProxy& camera = packet.Scene.MainCamera;
        if (!packet.bHasMainCamera || camera.SequenceFrame == 0)
        {
            carry.Reset();
            return;
        }
        Container::VariableArray<PathTracingSequenceCarry::InstanceState> current;
        MakeKeys(packet.RayTracingScene, current);
        SortByKey(packet.RayTracingScene, current);
        for (size_t index = 0; index < current.size(); ++index)
        {
            std::memcpy(current[index].Transform,
                        packet.RayTracingScene.Instances[index].Instance.transform,
                        sizeof(current[index].Transform));
            current[index].bHasTransform = true;
        }

        if (carry.Frame != camera.SequenceFrame)
        {
            const bool bFromLast = carry.LastFrame != 0 &&
                                   carry.LastCamera.CameraId == camera.CameraId;
            carry.Frame = camera.SequenceFrame;
            if (bFromLast)
            {
                carry.bHasPreviousCamera = true;
                carry.PreviousCamera = carry.LastCamera;
            }
            else
            {
                carry.bHasPreviousCamera = packet.bHasPreviousMainCamera &&
                                           packet.PreviousMainCamera.CameraId == camera.CameraId;
                carry.PreviousCamera = carry.bHasPreviousCamera ? packet.PreviousMainCamera
                                                                : CameraProxy{};
            }
            carry.PreviousInstances = current;
            for (size_t index = 0; index < current.size(); ++index)
            {
                PathTracingSequenceCarry::InstanceState& previous = carry.PreviousInstances[index];
                const RayTracingSceneInstanceSnapshot& instance =
                    packet.RayTracingScene.Instances[index];
                const PathTracingSequenceCarry::InstanceState* last =
                    bFromLast ? Find(carry.LastInstances, previous) : nullptr;
                if (last)
                {
                    std::memcpy(previous.Transform, last->Transform, sizeof(previous.Transform));
                    previous.bHasTransform = true;
                }
                else if (!bFromLast && instance.bHasPreviousTransform)
                {
                    std::memcpy(previous.Transform, instance.PreviousTransform,
                                sizeof(previous.Transform));
                    previous.bHasTransform = true;
                }
                else
                {
                    previous.bHasTransform = false;
                }
            }
        }

        packet.bHasPreviousMainCamera = carry.bHasPreviousCamera;
        packet.PreviousMainCamera = carry.PreviousCamera;
        for (size_t index = 0; index < current.size(); ++index)
        {
            RayTracingSceneInstanceSnapshot& instance = packet.RayTracingScene.Instances[index];
            const PathTracingSequenceCarry::InstanceState* previous =
                Find(carry.PreviousInstances, current[index]);
            if (previous && previous->bHasTransform)
            {
                std::memcpy(instance.PreviousTransform, previous->Transform,
                            sizeof(instance.PreviousTransform));
                instance.bHasPreviousTransform = true;
            }
            else
            {
                std::memcpy(instance.PreviousTransform, instance.Instance.transform,
                            sizeof(instance.PreviousTransform));
                instance.bHasPreviousTransform = false;
            }
        }
        carry.LastFrame = camera.SequenceFrame;
        carry.LastCamera = camera;
        carry.LastInstances = std::move(current);
    }

    // ========================================
    // パケットの適用の確認
    // ========================================

    /** @brief RenderThread がパケットを適用しようとした結果 */
    enum class PacketApplyResult : uint8_t
    {
        Applied,         // 適用した（確認を進めた）
        Unnumbered,      // 番号の無いパケット（手で組んだものなど）。確認しない
        StaleSerial,     // 適用済みより古い番号。捨てる
        StaleGeneration, // 適用済みより古い世代。捨てる
    };

    /**
     * @brief RenderThread の適用の確認
     *
     * 適用したパケットの番号・シーンの世代・GPU の提出の完了を覚え、atomic で GameThread へ公開する。
     * マルチ・シングルのどちらの経路でも RenderFrame が同じ場所で呼ぶ（描画を見送っても、適用したら確認する）。
     * 書くのは描画を行うスレッドだけ。GameThread は Get 系だけを読む。
     *
     * 確認の番号 N は「N までに送ったパケットのうち、適用されたものは N 以前に適用済み」を意味する。
     * 飛ばされたパケットの差分は、GameThread が確認の番号より後の分として積み直す（PacketDeltaLedger）。
     */
    class PacketConfirmation
    {
    public:
        /** @brief 初期化（描画が止まっている間に呼ぶ） */
        void Reset()
        {
            m_Entries.clear();
            m_LastCompletedSubmission = 0;
            m_ConfirmedGeneration.Store(0, std::memory_order_release);
            m_ConfirmedSerial.Store(0, std::memory_order_release);
            m_CompletedSerial.Store(0, std::memory_order_release);
            m_StaleDropped.Store(0, std::memory_order_release);
        }

        /**
         * @brief パケットを適用する（描画するスレッドから、パケットを取った直後に呼ぶ）
         *
         * 適用済みより古い番号・世代のパケットは Stale を返す（呼び出し側は描かずに捨てる）。
         * Applied のときは、描画を終えたあとで NoteFinished を呼ぶ。
         */
        PacketApplyResult TryApply(uint64_t sendSerial, uint64_t sceneGeneration)
        {
            if (sendSerial == 0)
            {
                return PacketApplyResult::Unnumbered;
            }
            if (sceneGeneration < m_ConfirmedGeneration.Load(std::memory_order_relaxed))
            {
                m_StaleDropped.FetchAdd(1, std::memory_order_relaxed);
                return PacketApplyResult::StaleGeneration;
            }
            if (sendSerial < m_ConfirmedSerial.Load(std::memory_order_relaxed))
            {
                m_StaleDropped.FetchAdd(1, std::memory_order_relaxed);
                return PacketApplyResult::StaleSerial;
            }
            m_Entries.push_back({sendSerial, 0, false});
            // 世代を先に公開する: 番号を読んだ側が、それ以後の世代を読めるようにする
            m_ConfirmedGeneration.Store(sceneGeneration, std::memory_order_release);
            m_ConfirmedSerial.Store(sendSerial, std::memory_order_release);
            return PacketApplyResult::Applied;
        }

        /** @brief 適用したパケットの描画を GPU へ提出した（提出番号との対応を持つ） */
        void NoteSubmitted(uint64_t sendSerial, uint64_t submissionSerial)
        {
            if (Entry* entry = FindLast(sendSerial))
            {
                entry->SubmissionSerial = submissionSerial;
            }
        }

        /** @brief 適用したパケットの処理を終えた（提出が無くても呼ぶ） */
        void NoteFinished(uint64_t sendSerial)
        {
            if (Entry* entry = FindLast(sendSerial))
            {
                entry->bFinished = true;
            }
            RetireCompleted(false);
        }

        /**
         * @brief GPU の完了した提出番号を取り込む
         * @param bAllCompleted スワップチェーンの作り直しで提出番号が戻ったとき true（持っていた提出は完了済み）
         */
        void Retire(uint64_t completedSubmissionSerial, bool bAllCompleted)
        {
            m_LastCompletedSubmission = completedSubmissionSerial;
            RetireCompleted(bAllCompleted);
        }

        /** @brief 適用した最後のパケットの番号（読んでよいスレッド: どこでも） */
        [[nodiscard]] uint64_t GetConfirmedSerial() const { return m_ConfirmedSerial.Load(std::memory_order_acquire); }
        /** @brief 適用した最後のパケットのシーンの世代 */
        [[nodiscard]] uint64_t GetConfirmedGeneration() const { return m_ConfirmedGeneration.Load(std::memory_order_acquire); }
        /** @brief GPU の描画が完了した番号。これ以前に適用したパケットはすべて、提出が完了したか提出が無かった */
        [[nodiscard]] uint64_t GetCompletedSerial() const { return m_CompletedSerial.Load(std::memory_order_acquire); }
        /** @brief 古いため捨てたパケットの数 */
        [[nodiscard]] uint64_t GetStaleDropCount() const { return m_StaleDropped.Load(std::memory_order_acquire); }

        /** @brief この世代の差分を、適用済みの写しへ当ててよいか（世代が替わったら古い差分は拒む） */
        [[nodiscard]] bool CanApplyDelta(uint64_t deltaGeneration) const
        {
            return deltaGeneration == m_ConfirmedGeneration.Load(std::memory_order_acquire);
        }

    private:
        struct Entry
        {
            uint64_t SendSerial = 0;
            uint64_t SubmissionSerial = 0; // 0 は提出が無い
            bool bFinished = false;
        };

        Entry* FindLast(uint64_t sendSerial)
        {
            for (size_t index = m_Entries.size(); index > 0; --index)
            {
                if (m_Entries[index - 1].SendSerial == sendSerial)
                {
                    return &m_Entries[index - 1];
                }
            }
            return nullptr;
        }

        // 先頭から、処理を終えて提出が完了した（または提出が無い）ものを外し、完了の番号を進める
        void RetireCompleted(bool bAllCompleted)
        {
            size_t retired = 0;
            uint64_t completed = m_CompletedSerial.Load(std::memory_order_relaxed);
            for (const Entry& entry : m_Entries)
            {
                const bool bSubmissionDone =
                    entry.SubmissionSerial == 0 || bAllCompleted || entry.SubmissionSerial <= m_LastCompletedSubmission;
                if (!entry.bFinished || !bSubmissionDone)
                {
                    break;
                }
                completed = std::max(completed, entry.SendSerial);
                ++retired;
            }
            if (retired > 0)
            {
                m_Entries.erase(m_Entries.begin(), m_Entries.begin() + static_cast<std::ptrdiff_t>(retired));
                m_CompletedSerial.Store(completed, std::memory_order_release);
            }
        }

        // 描画するスレッドだけが触る（適用した順 = 番号の順）
        Container::VariableArray<Entry> m_Entries;
        uint64_t m_LastCompletedSubmission = 0;

        Thread::Atomic<uint64_t> m_ConfirmedGeneration{0};
        Thread::Atomic<uint64_t> m_ConfirmedSerial{0};
        Thread::Atomic<uint64_t> m_CompletedSerial{0};
        Thread::Atomic<uint64_t> m_StaleDropped{0};
    };

    /**
     * @brief GameThread が持つ、確認されていない差分の台帳
     *
     * 送ったパケットが運んだ変化の鍵（行の識別子など）を番号つきで覚える。確認の番号以前の分は捨て、
     * それより後の分（RenderThread に飛ばされたパケットの分を含む）は次のパケットが積み直して運ぶ。
     * 世代が替わったら、古い世代の差分は拒んで捨てる（写しごと入れ替わるため意味を持たない）。
     * GameThread だけが触る。
     */
    class PacketDeltaLedger
    {
    public:
        void Reset() { m_Sent.clear(); }

        /** @brief 送ったパケットの新しい変化の鍵を覚える。新しい世代になったら古い世代の分を捨てる */
        void NoteSent(uint64_t sendSerial, uint64_t sceneGeneration, const uint64_t* keys, size_t keyCount)
        {
            DiscardOlderGenerations(sceneGeneration);
            SentEntry entry;
            entry.SendSerial = sendSerial;
            entry.SceneGeneration = sceneGeneration;
            entry.Keys.reserve(keyCount);
            for (size_t index = 0; index < keyCount; ++index)
            {
                entry.Keys.push_back(keys[index]);
            }
            m_Sent.push_back(std::move(entry));
        }

        /** @brief RenderThread の確認を取り込む。確認の番号以前と、確認の世代より古い世代の分を捨てる */
        void NoteConfirmed(uint64_t confirmedSerial, uint64_t confirmedGeneration)
        {
            size_t kept = 0;
            for (size_t index = 0; index < m_Sent.size(); ++index)
            {
                const SentEntry& entry = m_Sent[index];
                const bool bConfirmed = entry.SceneGeneration == confirmedGeneration && entry.SendSerial <= confirmedSerial;
                if (bConfirmed || entry.SceneGeneration < confirmedGeneration)
                {
                    continue;
                }
                if (kept != index)
                {
                    m_Sent[kept] = std::move(m_Sent[index]);
                }
                ++kept;
            }
            m_Sent.resize(kept);
        }

        /** @brief 次のパケットが積み直す鍵（確認されていない、現在の世代の送信の分。重複は除く） */
        void CollectPending(uint64_t currentGeneration, Container::VariableArray<uint64_t>& outKeys) const
        {
            outKeys.clear();
            // 最初に現れた順を保ち、重複は除く（鍵の数に比例。変化の数が多い送信が続いても二乗にならない）
            Container::UnorderedSet<uint64_t> seen;
            for (const SentEntry& entry : m_Sent)
            {
                if (entry.SceneGeneration != currentGeneration)
                {
                    continue;
                }
                for (uint64_t key : entry.Keys)
                {
                    if (seen.insert(key).second)
                    {
                        outKeys.push_back(key);
                    }
                }
            }
        }

        /** @brief 確認を待っている送信の数 */
        [[nodiscard]] size_t GetUnconfirmedSendCount() const { return m_Sent.size(); }

    private:
        struct SentEntry
        {
            uint64_t SendSerial = 0;
            uint64_t SceneGeneration = 0;
            Container::VariableArray<uint64_t> Keys;
        };

        void DiscardOlderGenerations(uint64_t sceneGeneration)
        {
            size_t kept = 0;
            for (size_t index = 0; index < m_Sent.size(); ++index)
            {
                if (m_Sent[index].SceneGeneration < sceneGeneration)
                {
                    continue;
                }
                if (kept != index)
                {
                    m_Sent[kept] = std::move(m_Sent[index]);
                }
                ++kept;
            }
            m_Sent.resize(kept);
        }

        Container::VariableArray<SentEntry> m_Sent;
    };

    /**
     * @brief GameThread が確認の公開値を見張る（検査 MEGA_SCENE_CONFIRM）
     *
     * 確認の番号・世代・完了の番号が単調で、送った番号・世代を超えず、完了が確認を超えないことを確かめる。
     */
    class PacketConfirmationMonitor
    {
    public:
        /** @return 守られていれば true。破れたときは detail に理由を書く */
        bool Observe(const PacketConfirmation& confirmation, uint64_t lastSentSerial, uint64_t currentGeneration,
                     char* detail, size_t detailSize)
        {
            // 書く側は 世代 → 番号 → 完了 の順に公開するので、読む側は逆の順に読む
            const uint64_t completed = confirmation.GetCompletedSerial();
            const uint64_t serial = confirmation.GetConfirmedSerial();
            const uint64_t generation = confirmation.GetConfirmedGeneration();
            ++m_ObserveCount;

            const char* reason = nullptr;
            if (serial < m_LastSerial)
            {
                reason = "確認の番号が戻った";
            }
            else if (generation < m_LastGeneration)
            {
                reason = "確認の世代が戻った";
            }
            else if (completed < m_LastCompleted)
            {
                reason = "完了の番号が戻った";
            }
            else if (completed > serial)
            {
                reason = "完了の番号が確認の番号を超えた";
            }
            else if (serial > lastSentSerial)
            {
                reason = "確認の番号が送った番号を超えた";
            }
            else if (generation > currentGeneration)
            {
                reason = "確認の世代が送った世代を超えた";
            }
            m_LastSerial = std::max(m_LastSerial, serial);
            m_LastGeneration = std::max(m_LastGeneration, generation);
            m_LastCompleted = std::max(m_LastCompleted, completed);
            if (reason != nullptr && detail != nullptr && detailSize > 0)
            {
                std::snprintf(detail, detailSize, "%s: confirmed=%llu generation=%llu completed=%llu sent=%llu current_generation=%llu",
                              reason, static_cast<unsigned long long>(serial), static_cast<unsigned long long>(generation),
                              static_cast<unsigned long long>(completed), static_cast<unsigned long long>(lastSentSerial),
                              static_cast<unsigned long long>(currentGeneration));
            }
            return reason == nullptr;
        }

        [[nodiscard]] uint64_t GetObserveCount() const { return m_ObserveCount; }
        [[nodiscard]] uint64_t GetLastSerial() const { return m_LastSerial; }
        [[nodiscard]] uint64_t GetLastCompleted() const { return m_LastCompleted; }

    private:
        uint64_t m_LastSerial = 0;
        uint64_t m_LastGeneration = 0;
        uint64_t m_LastCompleted = 0;
        uint64_t m_ObserveCount = 0;
    };

    // ========================================
    // FramePacketManager
    // ========================================

    /**
     * @brief フレームパケットバッファ数
     *
     * GameThread/RenderThread間のproducer/consumerキューのスロット数です。
     * SwapChainやRHIのframes-in-flight数とは独立して管理します。
     */
    constexpr uint32_t FRAME_PACKET_BUFFER_COUNT = 3;

    /**
     * @brief フレームパケットマネージャー
     *
     * Triple Bufferingを管理し、GameThreadとRenderThread間の
     * ロックフリーな同期を提供します。
     */
    class FramePacketManager
    {
    public:
        /**
         * @brief デフォルトコンストラクタ
         */
        FramePacketManager() = default;

        /**
         * @brief 初期化
         */
        void Initialize()
        {
            for (uint32_t i = 0; i < FRAME_PACKET_BUFFER_COUNT; ++i)
            {
                m_Packets[i].Clear();
                m_Packets[i].SetState(FramePacketState::Empty);
            }
            m_WriteIndex.Store(0, std::memory_order_release);
            m_ReadIndex.Store(0, std::memory_order_release);
            m_CurrentFrameNumber.Store(0, std::memory_order_release);
            m_SentSerial.Store(0, std::memory_order_release);
            m_Confirmation.Reset();
            m_SceneGeneration = 1;
            m_ObservedGenerationSource = nullptr;
            m_ObservedGenerationValue = 0;
            m_bGenerationObserved = false;
            m_ConfirmationMonitor = PacketConfirmationMonitor{};
            m_SequenceCarry.Reset();
        }

        /**
         * @brief 終了処理
         */
        void Shutdown()
        {
            for (uint32_t i = 0; i < FRAME_PACKET_BUFFER_COUNT; ++i)
            {
                m_Packets[i].Clear();
            }
        }

        // ========================================
        // GameThread用インターフェース
        // ========================================

        /**
         * @brief 書き込み用パケットを取得（GameThread用）
         *
         * 書き込み可能なパケットを探し、Writing状態にして返します。
         * 全てのバッファが使用中の場合はnullptrを返します。
         *
         * @return 書き込み可能なパケット、なければnullptr
         */
        FramePacket *AcquireForWrite()
        {
            uint32_t writeIdx = m_WriteIndex.Load(std::memory_order_acquire);

            // 全スロットをスキャン（writeIdxから順に）して最初の空きを取得
            for (uint32_t attempt = 0; attempt < FRAME_PACKET_BUFFER_COUNT; ++attempt)
            {
                uint32_t idx = (writeIdx + attempt) % FRAME_PACKET_BUFFER_COUNT;
                if (m_Packets[idx].CompareExchangeState(FramePacketState::Empty,
                                                        FramePacketState::Writing))
                {
                    m_WriteIndex.Store(idx, std::memory_order_release);
                    m_Packets[idx].FrameNumber = m_CurrentFrameNumber.FetchAdd(1, std::memory_order_relaxed);
                    return &m_Packets[idx];
                }
            }

            // 全バッファが使用中
            return nullptr;
        }

        /**
         * @brief 書き込み完了を通知（GameThread用）
         *
         * パケットをReady状態にし、RenderThreadが読み取り可能にします。
         *
         * @param packet 書き込み完了したパケット
         */
        void FinishWrite(FramePacket *packet)
        {
            if (packet)
            {
                // 送った番号を振る（飛ばされたパケットがあっても番号は欠けない）
                packet->SendSerial = m_SentSerial.FetchAdd(1, std::memory_order_relaxed) + 1;
                packet->SetState(FramePacketState::Ready);
                // 次のバッファへ進む
                uint32_t currentIdx = static_cast<uint32_t>(packet - m_Packets.data());
                m_WriteIndex.Store((currentIdx + 1) % FRAME_PACKET_BUFFER_COUNT, std::memory_order_release);
            }
        }

        /**
         * @brief 書き込みをキャンセル（GameThread用）
         *
         * @param packet キャンセルするパケット
         */
        void CancelWrite(FramePacket *packet)
        {
            if (packet)
            {
                packet->Clear();
                packet->SetState(FramePacketState::Empty);
            }
        }

        // ========================================
        // RenderThread用インターフェース
        // ========================================

        /**
         * @brief 読み取り用パケットを取得（RenderThread用）
         *
         * 読み取り可能な最新のパケットを探し、Reading状態にして返します。
         * 利用可能なパケットがない場合はnullptrを返します。
         *
         * @return 読み取り可能なパケット、なければnullptr
         */
        FramePacket *AcquireForRead()
        {
            // 最新のReadyパケットを探す
            FramePacket *latestReady = nullptr;
            uint64_t latestFrame = 0;

            for (uint32_t i = 0; i < FRAME_PACKET_BUFFER_COUNT; ++i)
            {
                if (m_Packets[i].GetState() == FramePacketState::Ready)
                {
                    if (m_Packets[i].FrameNumber >= latestFrame)
                    {
                        latestReady = &m_Packets[i];
                        latestFrame = m_Packets[i].FrameNumber;
                    }
                }
            }

            if (latestReady)
            {
                if (latestReady->CompareExchangeState(FramePacketState::Ready,
                                                      FramePacketState::Reading))
                {
                    return latestReady;
                }

                // CASレースに負けた場合、Readyな任意のパケットを取得（フレーム番号順不問）
                for (uint32_t i = 0; i < FRAME_PACKET_BUFFER_COUNT; ++i)
                {
                    if (m_Packets[i].CompareExchangeState(FramePacketState::Ready,
                                                          FramePacketState::Reading))
                    {
                        return &m_Packets[i];
                    }
                }
            }

            return nullptr;
        }

        /**
         * @brief 読み取り完了を通知（RenderThread用）
         *
         * パケットをEmpty状態にし、再利用可能にします。
         *
         * @param packet 読み取り完了したパケット
         */
        void FinishRead(FramePacket *packet)
        {
            if (packet)
            {
                if (!packet->CompareExchangeState(FramePacketState::Reading,
                                                  FramePacketState::Recycling))
                {
                    return;
                }
                packet->Clear();
                packet->SetState(FramePacketState::Empty);
            }
        }

        // ========================================
        // シャットダウン・リサイズ補助
        // ========================================

        /**
         * @brief Writing状態のパケットをEmptyに戻す
         *
         * シャットダウンやリサイズ前にGameThread側の書き込みを安全に中断する。
         * Reading中のパケットはRenderThread完了を待つ必要があるため変更しない。
         */
        void CancelInflightWrites()
        {
            for (uint32_t i = 0; i < FRAME_PACKET_BUFFER_COUNT; ++i)
            {
                // Writing→Recyclingへ遷移して排他的に回収する
                if (m_Packets[i].CompareExchangeState(FramePacketState::Writing,
                                                      FramePacketState::Recycling))
                {
                    m_Packets[i].Clear();
                    m_Packets[i].SetState(FramePacketState::Empty);
                }
            }
        }

        /**
         * @brief Ready状態の未消費パケットをEmptyに戻す
         *
         * RenderThreadが動作していない（シングルスレッドモード）場合や
         * AcquireForWriteが失敗する前にGameThread側から呼び出し、
         * 蓄積したReadyパケットを再利用可能にします。
         *
         * 安全性: Ready→Writing（一時確保）→Clear→Emptyの順で遷移するため、
         * 並行するAcquireForWriteがClear前に同一スロットを取得しません。
         *
         * @return 解放したパケット数
         */
        uint32_t DrainUnconsumedPackets()
        {
            uint32_t count = 0;
            for (uint32_t i = 0; i < FRAME_PACKET_BUFFER_COUNT; ++i)
            {
                // Ready→Writingに遷移して一時確保（AcquireForWriteとの競合防止）
                if (m_Packets[i].CompareExchangeState(FramePacketState::Ready,
                                                      FramePacketState::Writing))
                {
                    m_Packets[i].Clear();
                    m_Packets[i].SetState(FramePacketState::Empty);
                    ++count;
                }
            }
            return count;
        }

        /**
         * @brief Reading状態のパケットが全てなくなるまでスピン待機
         *
         * RenderThreadがAcquireForRead()/FinishRead()経由で
         * Reading状態を運用している構成向けの補助です。
         * 現在のシングルスレッド経路では同期境界としては使いません。
         *
         * @param maxSpinCount 最大スピン回数（デフォルト: 50000）
         * @return Readingパケットが全て消えた場合true、タイムアウトでfalse
         */
        bool SpinUntilReadingDrained(uint32_t maxSpinCount = 50000u) const
        {
            for (uint32_t spin = 0u; spin <= maxSpinCount; ++spin)
            {
                bool bHasReading = false;
                for (uint32_t i = 0; i < FRAME_PACKET_BUFFER_COUNT; ++i)
                {
                    const auto state = m_Packets[i].GetState();
                    if (state == FramePacketState::Queued ||
                        state == FramePacketState::Reading)
                    {
                        bHasReading = true;
                        break;
                    }
                }
                if (!bHasReading)
                {
                    return true;
                }
                // キャッシュ同期フェンスを挿入して過剰なキャッシュ衝突を軽減
                std::atomic_thread_fence(std::memory_order_seq_cst);
            }
            return false;
        }

        /**
         * @brief Writing/Reading状態のパケットが存在するか
         *
         * シャットダウン・リサイズ前のドレイン確認に使用する。
         * @return inflight（処理中）のパケットがある場合true
         */
        bool HasInflightPackets() const
        {
            for (uint32_t i = 0; i < FRAME_PACKET_BUFFER_COUNT; ++i)
            {
                auto s = m_Packets[i].GetState();
                if (s == FramePacketState::Writing ||
                    s == FramePacketState::Queued ||
                    s == FramePacketState::Reading ||
                    s == FramePacketState::Recycling)
                {
                    return true;
                }
            }
            return false;
        }

        // ========================================
        // ステータス
        // ========================================

        /**
         * @brief 現在のフレーム番号を取得
         */
        uint64_t GetCurrentFrameNumber() const
        {
            return m_CurrentFrameNumber.Load(std::memory_order_acquire);
        }

        /**
         * @brief パケットが属するスロット index を返す
         *
         * packet は m_Packets（FixedArray）内の要素を指す借用ポインタである前提。
         * GameThread（書き込み中パケット）と RenderThread（処理中パケット）の双方が、
         * 同一スロットを安定した index で参照するための写像。プール外/null は
         * FRAME_PACKET_BUFFER_COUNT（無効値）を返す。
         *
         * @param packet m_Packets 内の要素を指すポインタ
         * @return スロット index（0..FRAME_PACKET_BUFFER_COUNT-1）。無効時は FRAME_PACKET_BUFFER_COUNT
         */
        uint32_t GetSlotIndex(const FramePacket *packet) const
        {
            if (packet == nullptr)
            {
                return FRAME_PACKET_BUFFER_COUNT;
            }
            const FramePacket *base = m_Packets.data();
            if (packet < base || packet >= base + FRAME_PACKET_BUFFER_COUNT)
            {
                return FRAME_PACKET_BUFFER_COUNT;
            }
            return static_cast<uint32_t>(packet - base);
        }

        /**
         * @brief Readyなパケット数を取得
         */
        uint32_t GetReadyPacketCount() const
        {
            uint32_t count = 0;
            for (uint32_t i = 0; i < FRAME_PACKET_BUFFER_COUNT; ++i)
            {
                if (m_Packets[i].GetState() == FramePacketState::Ready)
                {
                    ++count;
                }
            }
            return count;
        }

        /**
         * @brief 全パケットがEmptyかどうか
         */
        bool IsEmpty() const
        {
            for (uint32_t i = 0; i < FRAME_PACKET_BUFFER_COUNT; ++i)
            {
                if (m_Packets[i].GetState() != FramePacketState::Empty)
                {
                    return false;
                }
            }
            return true;
        }

        /** @brief 連番の1フレームの前の値の固定状態（GameThreadでパケットを書くときだけ触る） */
        PathTracingSequenceCarry& GetSequenceCarry() { return m_SequenceCarry; }

        /** @brief 最後に送ったパケットの番号（FinishWrite が振った最大。送っていなければ 0） */
        [[nodiscard]] uint64_t GetLastSentSerial() const { return m_SentSerial.Load(std::memory_order_acquire); }

        /** @brief RenderThread の適用の確認（RenderFrame が書き、GameThread が公開値を読む） */
        PacketConfirmation& GetConfirmation() { return m_Confirmation; }
        const PacketConfirmation& GetConfirmation() const { return m_Confirmation; }

        /** @brief GameThread が確認の公開値を見張る（検査 MEGA_SCENE_CONFIRM 用。GameThread だけが触る） */
        PacketConfirmationMonitor& GetConfirmationMonitor() { return m_ConfirmationMonitor; }

        /**
         * @brief パケットへ書くシーンの世代を決める（GameThread）
         *
         * 差分の源（SceneView）が替わった、またはその世代が進んだら、シーンの世代を 1 つ進める。
         * @param source 世代の源の識別（SceneView のアドレスなど。無ければ nullptr）
         * @param sourceGeneration 源が持つ世代
         */
        uint64_t ResolveSceneGeneration(const void* source, uint64_t sourceGeneration)
        {
            if (m_bGenerationObserved && (source != m_ObservedGenerationSource || sourceGeneration != m_ObservedGenerationValue))
            {
                ++m_SceneGeneration;
            }
            m_ObservedGenerationSource = source;
            m_ObservedGenerationValue = sourceGeneration;
            m_bGenerationObserved = true;
            return m_SceneGeneration;
        }

        /** @brief 最後に決めたシーンの世代 */
        [[nodiscard]] uint64_t GetSceneGeneration() const { return m_SceneGeneration; }

    private:
        Container::FixedArray<FramePacket, FRAME_PACKET_BUFFER_COUNT> m_Packets;
        PathTracingSequenceCarry m_SequenceCarry;
        Thread::Atomic<uint32_t> m_WriteIndex{0};
        Thread::Atomic<uint32_t> m_ReadIndex{0};
        Thread::Atomic<uint64_t> m_CurrentFrameNumber{0};
        Thread::Atomic<uint64_t> m_SentSerial{0};
        PacketConfirmation m_Confirmation;
        PacketConfirmationMonitor m_ConfirmationMonitor;
        // GameThread だけが触る
        uint64_t m_SceneGeneration = 1;
        const void* m_ObservedGenerationSource = nullptr;
        uint64_t m_ObservedGenerationValue = 0;
        bool m_bGenerationObserved = false;
    };

} // namespace NorvesLib::Core::Rendering

#pragma once

#include "RHI/IAccelerationStructure.h"
#include "Rendering/FramePacket.h"

#include <cstdint>

namespace NorvesLib::RHI
{
    class ICommandList;
}

namespace NorvesLib::Core::Rendering
{
    class MeshResources;
    class MaterialResources;
    class MegaGeometryResources;
    class MegaGeometryMirror;

    /**
     * @brief TLAS に入れるインスタンス（描画コマンドから作るメッシュと MegaGeometry の全部）を選ぶ規則
     *
     * カメラ（メインカメラの位置）からインスタンスの境界の球の表面までの距離が RayTracingSelectRadiusMeters 以内のものを、
     * 近い順に RayTracingSelectMaxInstances 本まで入れる。メッシュと MegaGeometry を区別せずに同じ規則で選ぶ。
     * 共有の TLAS は RT の影・RTGI・DDGI・パストレーサーが使うので、外れたインスタンスはどれからも見えない。
     */
    inline constexpr float RayTracingSelectRadiusMeters = 120.0f;
    inline constexpr uint32_t RayTracingSelectMaxInstances = 65536u;

    /**
     * @brief FramePacketの描画スナップショットから加速構造を管理
     *
     * GameThreadではDrawCommandからgeometryとtransformをコピーし、
     * RenderThreadではFramePacketだけを使ってBLAS/TLAS buildを記録します。
     */
    class RayTracingSceneSubsystem
    {
    public:
        RayTracingSceneSubsystem() = default;
        ~RayTracingSceneSubsystem();

        RayTracingSceneSubsystem(const RayTracingSceneSubsystem&) = delete;
        RayTracingSceneSubsystem& operator=(const RayTracingSceneSubsystem&) = delete;

        /**
         * @brief DrawスナップショットからFramePacketのレイトレーシングシーンを構築
         * @param meshResources DrawCommandのメッシュハンドルを解決するGPUメッシュリソース
         * @param packet 構築元および出力先のフレームスナップショット
         * @param materialResources DrawCommandのマテリアル値を解決するリソース
         * @param megaGeometryResources MegaGeometry（岩・小屋など）のLOD0を解決するリソース。nullなら含めない
         *        （差分の同期では RenderThread が AppendMegaGeometryInstancesFromMirror で足すので null を渡す）。
         *        null でなければ、メッシュと MegaGeometry の全部に TLAS に入れる規則を当てる。null なら規則を当てず、
         *        RenderThread の AppendMegaGeometryInstancesFromMirror がメッシュも含めて当てる
         * @param megaGeometryProxies MegaGeometry の proxy の配列。nullなら packet.Scene.MegaGeometryProxies を読む
         * @param meshProxies インスタンシング描画の物体IDの照合に使う MeshProxy の配列。nullなら packet.Scene.MeshProxies を読む
         *        （差分の同期ではパケットが全量を持たないので、GameThread の SceneView の配列を渡す）
         * @return 有効なスナップショットを構築できた場合true
         */
        bool BuildFrameSnapshot(const MeshResources* meshResources,
                                FramePacket& packet,
                                const MaterialResources* materialResources = nullptr,
                                const MegaGeometryResources* megaGeometryResources = nullptr,
                                const Container::VariableArray<MegaGeometryProxy>* megaGeometryProxies = nullptr,
                                const Container::VariableArray<MeshProxy>* meshProxies = nullptr);

        /**
         * @brief MegaGeometry（岩・小屋など）のインスタンスを、パケットのレイトレーシングシーンの末尾へ足す（全量の同期）
         * @param meshResources パケットにすでにあるメッシュのインスタンスの境界（ローカルの AABB）を引くリソース
         * @param megaGeometryResources LOD0 を解決するリソース。nullなら何もしない
         * @param megaProxies 足す proxy の配列（全量）
         * @param packet 足し先のフレームスナップショット
         *
         * 全量の配列を毎回読む経路（--mega-scene-full-sync）。すでにあるメッシュのインスタンスと MegaGeometry を合わせて、
         * AppendMegaGeometryInstancesFromMirror と同じ規則で選ぶ（外れたメッシュのインスタンスはパケットから除く）。
         */
        void AppendMegaGeometryInstances(const MeshResources* meshResources,
                                         const MegaGeometryResources* megaGeometryResources,
                                         const Container::VariableArray<MegaGeometryProxy>& megaProxies,
                                         FramePacket& packet);

        /**
         * @brief 差分の同期の MegaGeometry のインスタンスを、席ごとの配列から選んでパケットの末尾へ足す（RenderThread）
         * @param meshResources パケットにすでにあるメッシュのインスタンスの境界（ローカルの AABB）を引くリソース
         * @param megaGeometryResources LOD0 を解決するリソース。nullなら MegaGeometry は足さない（メッシュには規則を当てる）
         * @param mirror RenderThread の写し。NoteDrawn の後（前の変換が決まった後）に渡す
         * @param packet 足し先のフレームスナップショット（描くパケット）
         *
         * 席（SceneSlot）ごとの配列は、TLAS のインスタンスの入力（境界・変換・前の変換・マスク・物体 ID・メッシュ）を持ち、
         * 写しが前に見た版から数えた変わった席だけを直す（直せないときは作り直す）。変わらない席は写しを読まない。
         * GameThread が足したメッシュのインスタンスと合わせて規則（RayTracingSelectRadiusMeters・RayTracingSelectMaxInstances）
         * で選び、外れたメッシュのインスタンスはパケットから除き、選んだ席は席の入力とメッシュの GPU データからインスタンスを足す。
         * 連番のパストレーシングの前の値の持ち越しも、選んだ後のこの場所で当てる。BuildAccelerationStructures の前に呼ぶ。
         */
        void AppendMegaGeometryInstancesFromMirror(const MeshResources* meshResources,
                                                   const MegaGeometryResources* megaGeometryResources,
                                                   const MegaGeometryMirror& mirror,
                                                   FramePacket& packet);

        /**
         * @brief FramePacketのgeometryからBLAS/TLAS buildをRenderThreadへ記録
         * @param device 加速構造を作成するRHIデバイス
         * @param commandList buildを記録するコマンドリスト
         * @param frameSlot FramePacketManagerのslot index
         * @param packet GameThreadが確定したシーンスナップショット
         * @return buildを記録できた場合true
         */
        bool BuildAccelerationStructures(RHI::DevicePtr device,
                                         RHI::ICommandList& commandList,
                                         uint32_t frameSlot,
                                         FramePacket& packet);

        /** @brief サブシステムが保持するGPU資源を解放 */
        void Shutdown();

    private:
        struct BottomLevelCacheEntry
        {
            MeshDataHandle MeshHandle;
            RHI::BufferPtr SourceVertexBuffer;
            RHI::BufferPtr SourceIndexBuffer;
            uint64_t VertexBufferOffsetBytes = 0;
            uint64_t IndexBufferOffsetBytes = 0;
            uint64_t MegaMeshId = 0;
            uint32_t IndexOffset = 0;
            uint32_t IndexCount = 0;
            uint32_t VertexOffset = 0;
            uint32_t VertexCount = 0;
            uint32_t VertexStride = 0;
            bool bGeometryOpaque = true;
            RHI::BufferPtr VertexBuffer;
            RHI::BufferPtr IndexBuffer;
            RHI::AccelerationStructurePtr Structure;
        };

        /**
         * @brief frame slot ごとの TLAS と、最後に記録したインスタンスの入力
         *
         * Instances は最後に Build / Update を記録したときのインスタンス（選んだ集合と並び・変換・マスク・customIndex・
         * BLAS）。次にその slot で描くフレームの入力が全部一致すれば Update を記録しない。BLAS の参照を持つので、
         * 準備完了で増えた・交換で作り直した BLAS は別の参照になり、一致しない。
         */
        struct TopLevelCacheEntry
        {
            RHI::AccelerationStructurePtr Structure;
            uint32_t InstanceCount = 0;
            Container::VariableArray<RHI::AccelerationStructureInstanceDesc> Instances;
        };

        /** @brief TLAS の記録の内訳（RT_SCENE update_skipped= に出す。出したら 0 に戻す） */
        struct TopLevelUpdateStats
        {
            uint32_t Frames = 0;
            uint32_t Skipped = 0; // 入力が slot の前回と一致して Update を記録しなかった
            uint32_t Updated = 0; // 同じ数のインスタンスで Update を記録した
            uint32_t Built = 0;   // 作り直して Build を記録した
            uint32_t LastInstances = 0;
        };

        /** @brief 2 つの TLAS のインスタンスの入力が全部一致するか（BLAS は参照、変換はビットで比べる） */
        static bool SameTopLevelInstances(
            const Container::VariableArray<RHI::AccelerationStructureInstanceDesc>& lhs,
            const Container::VariableArray<RHI::AccelerationStructureInstanceDesc>& rhs);

        /** @brief TLAS の記録の内訳を出す（最初と、120 フレームごと。bFlush なら出していない残りを出す） */
        void LogTopLevelUpdates(bool bFlush);

        /**
         * @brief 席ごとの MegaGeometry の TLAS のインスタンスの入力
         *
         * 席が変わったときだけ写しの proxy から作る。メッシュに属する値（頂点・インデックス・材質）は持たず、
         * フレームごとにメッシュの GPU データ（GPU への書き込みの完了で変わる）から足す。
         */
        struct MegaSlotEntry
        {
            /** @brief 境界の球（ワールド）の中心 xyz + 半径。境界が無い proxy は変換の並進と半径 0 */
            float BoundsSphere[4] = {};
            /** @brief TLAS のインスタンスの変換（3x4）と、前の変換（bHasPreviousTransform のときだけ使う） */
            float Transform[12] = {};
            float PreviousTransform[12] = {};
            uint64_t ObjectId = 0;
            MegaGeometry::MegaMeshHandle MegaMeshHandle;
            uint8_t Mask = 0;
            bool bHasPreviousTransform = false;
            /** @brief proxy があり、描けて（IsValid）、変換が有限 */
            bool bUsable = false;
        };

        /** @brief 規則で選んだ結果（RT_SCENE に出す） */
        struct SelectionStats
        {
            uint32_t Instances = 0;    // TLAS に入れたインスタンスの合計
            uint32_t MeshInstances = 0;
            uint32_t MegaInstances = 0;
            uint32_t Candidates = 0;   // 選ぶ対象のインスタンスの数（メッシュと MegaGeometry）
            uint32_t OutOfRange = 0;   // 距離で外れた数
            uint32_t OverLimit = 0;    // 上限で外れた数
            uint32_t NotReady = 0;     // 選んだがメッシュの GPU データがまだ使えない数
            uint32_t PatchedSlots = 0; // 席ごとの配列を直した席の数（作り直したときは全部）
            bool bRebuilt = false;
        };

        /** @brief 選ぶ候補（境界の球の表面までの距離と、並びの番号） */
        struct SelectCandidate
        {
            float Distance = 0.0f;
            uint32_t Index = 0;
        };

        /**
         * @brief 候補から規則で選ぶ
         * @param candidates 距離が半径以内の候補。近い順に budget 個（同じ距離は番号の小さい方）を、番号の昇順で残す
         * @param budget 入れられる数
         * @return 上限で外れた数
         */
        static uint32_t SelectNearest(Container::VariableArray<SelectCandidate>& candidates, uint32_t budget);

        /** @brief 境界の球の表面までの距離が半径以内なら候補に入れる（外れたら OutOfRange を数える） */
        static void ConsiderCandidate(const float origin[3],
                                      bool bHasOrigin,
                                      const float sphere[4],
                                      uint32_t index,
                                      Container::VariableArray<SelectCandidate>& candidates,
                                      SelectionStats& stats);

        /** @brief パケットの先頭 meshCount 個（メッシュのインスタンス）を候補に入れる。番号はパケットの並び */
        static void CollectMeshCandidates(const MeshResources* meshResources,
                                          const FramePacket& packet,
                                          uint32_t meshCount,
                                          const float origin[3],
                                          bool bHasOrigin,
                                          Container::VariableArray<SelectCandidate>& candidates,
                                          SelectionStats& stats);

        /**
         * @brief 選んだメッシュのインスタンスだけをパケットの先頭に詰めて残す（並びを保ち、customIndex を振り直す）
         * @param candidates SelectNearest の結果（番号の昇順）。meshCount 未満の番号がメッシュ
         */
        static void KeepSelectedMeshInstances(FramePacket& packet,
                                              const Container::VariableArray<SelectCandidate>& candidates,
                                              uint32_t meshCount);

        /**
         * @brief proxy から席の入力を作る（使えない proxy は bUsable が false）
         * @param localBounds proxy のメッシュ全体の境界の球（ローカル）。null なら proxy の WorldBounds で選ぶ範囲を測る
         */
        static void MakeMegaSlotEntry(const MegaGeometryProxy* proxy,
                                      const BoundingSphere* localBounds,
                                      MegaSlotEntry& entry);

        /** @brief 席の入力とメッシュの GPU データから、インスタンスをパケットの末尾へ足す */
        static void AppendMegaInstance(const MegaGeometry::MegaMeshGPUData& gpuData,
                                       const MegaSlotEntry& entry,
                                       FramePacket& packet);

        /** @brief 写しの変わった席で席ごとの配列を直す（直せないときは作り直す）。直した席の数を返す */
        uint32_t SyncMegaSlots(const MegaGeometryMirror& mirror,
                               const MegaGeometryResources* megaGeometryResources,
                               bool& outRebuilt);

        /** @brief 選んだ結果を RT_SCENE に出す（最初と、120 回ごと） */
        static void LogSelection(const char* path, const SelectionStats& stats, uint32_t& counter);

        Container::VariableArray<BottomLevelCacheEntry> m_BottomLevelCache;
        Container::FixedArray<TopLevelCacheEntry, FRAME_PACKET_BUFFER_COUNT> m_TopLevelCache;
        TopLevelUpdateStats m_TopLevelUpdateStats;
        uint32_t m_TopLevelLogCounter = 0;

        // ここから下は RenderThread だけが触る（AppendMegaGeometryInstancesFromMirror）
        /** @brief 席（SceneSlot の値。0 は使わない）ごとの入力 */
        Container::VariableArray<MegaSlotEntry> m_MegaSlots;
        /** @brief 席を持たない proxy の入力（写しの中で変わると全部が変わったものとして扱われるので、作り直すときだけ集める） */
        Container::VariableArray<MegaSlotEntry> m_UnslottedMegaEntries;
        const MegaGeometryMirror* m_MegaSlotSource = nullptr;
        uint64_t m_MegaSlotRevision = 0;
        uint64_t m_MegaSlotGeneration = 0;
        bool m_bMegaSlotsValid = false;
        /** @brief 選ぶ候補の作業配列（フレームごとに確保し直さない） */
        Container::VariableArray<SelectCandidate> m_MegaSelectCandidates;
        /** @brief 連番のパストレーシングの前の値の持ち越し（描くパケットごとに RenderThread が当てる） */
        PathTracingSequenceCarry m_RenderThreadSequenceCarry;
        uint32_t m_MirrorSelectionLogCounter = 0;
        // 全量の同期の経路（GameThread の BuildFrameSnapshot から呼ぶ）だけが触る
        uint32_t m_FullSelectionLogCounter = 0;
    };

} // namespace NorvesLib::Core::Rendering

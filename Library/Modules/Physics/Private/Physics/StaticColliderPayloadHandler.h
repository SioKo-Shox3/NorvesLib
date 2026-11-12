// セルの静的コライダーを準備し、物理への登録・休眠・解放を管理する。
// JSON は format: "norves.static-colliders.v1" と、省略可能な colliders / scatter の配列を持つ。
// colliders は形状と変換、scatter は散布名・種名と球またはカプセルの寸法を指定する。
#pragma once

#include "Container/UnorderedMap.h"
#include "Physics/IPhysicsModule.h"
#include "Streaming/IStreamingPayloadHandler.h"
#include "Thread/Mutex.h"

namespace NorvesLib::Core::Scatter
{
    class ScatterSystem;
}

namespace NorvesLib::Modules::Physics
{
    inline constexpr uint64_t kStreamingColliderKeyTag = 0x01;

    /// Assets/ からの論理パスにある次の JSON を読む（各配列は省略可能）。
    /// {"format":"norves.static-colliders.v1","colliders":[
    ///   {"shape":"sphere","position":[0,0,0],"radius":1},
    ///   {"shape":"box","position":[0,0,0],"halfExtents":[1,1,1],"rotation":[0,0,0,1]},
    ///   {"shape":"capsule","position":[0,0,0],"radius":0.5,"halfHeight":1,"scale":[1,1,1],"surface":3}],
    ///  "scatter":[{"name":"grassland","species":"rock","shape":"sphere","radius":1,"centerHeight":0.5}]}
    /// rotation は単位回転、scale は 1、surface は無効値、centerHeight は 0 が既定。
    class StaticColliderPayloadHandler final : public Core::Streaming::IStreamingPayloadHandler
    {
    public:
        explicit StaticColliderPayloadHandler(IPhysicsModule& physics);
        /// 散布の参照先を借りる。設定とコミット・解放は GameThread で行う。
        void SetScatterSystem(Core::Scatter::ScatterSystem* pScatterSystem);
        /// 読み込み先の試験用設定。空なら既定の Assets/ を使う。
        void SetAssetRoot(Core::Container::AnsiString root);
        /// 上位 8 ビットは鍵の出どころ。0x01 はセルのストリーミング。
        /// 散布のチャンクの当たり（G12-GR47）は別の値を使う。
        static StaticColliderChunkKey MakeChunkKey(Core::Streaming::StreamingCellId cell, uint32_t payloadIndex);

        const char* GetKind() const override;
        Core::Streaming::EStreamingPrepareResult Prepare(const Core::Streaming::StreamingPayloadContext& context,
            const Core::Streaming::StreamingCancelToken& cancel) override;
        Core::Streaming::EStreamingStepResult CommitStep(const Core::Streaming::StreamingPayloadContext& context,
            Core::Streaming::StreamingStepCursor& cursor, uint64_t& outCommittedBytes) override;
        void SetDormant(const Core::Streaming::StreamingPayloadContext& context, bool bDormant) override;
        Core::Streaming::EStreamingStepResult UnloadStep(const Core::Streaming::StreamingPayloadContext& context,
            Core::Streaming::StreamingStepCursor& cursor) override;
        void DiscardPrepared(const Core::Streaming::StreamingPayloadContext& context) override;
        Core::Streaming::StreamingPayloadBytes GetResidentBytes(
            const Core::Streaming::StreamingPayloadContext& context) const override;

    private:
        struct ScatterEntry
        {
            Core::Container::AnsiString Name;
            Core::Container::AnsiString Species;
            EStaticColliderShape Shape = EStaticColliderShape::Sphere;
            float Radius = 1.0f;
            float HalfHeight = 1.0f;
            float CenterHeight = 0.0f;
        };

        struct PayloadState
        {
            Core::Container::VariableArray<StaticColliderDesc> Colliders;
            Core::Container::VariableArray<ScatterEntry> Scatter;
            bool bScatterResolved = false;
            bool bRegistered = false;
        };

        bool AppendScatter(const Core::Streaming::StreamingPayloadContext& context, const PayloadState& state,
            Core::Container::VariableArray<StaticColliderDesc>& outColliders) const;
        bool Register(const Core::Streaming::StreamingPayloadContext& context, PayloadState& state);
        bool Unregister(const Core::Streaming::StreamingPayloadContext& context, PayloadState& state);

        IPhysicsModule& m_Physics;
        Core::Scatter::ScatterSystem* m_pScatterSystem = nullptr;
        Core::Container::AnsiString m_AssetRoot;
        // 表へのアクセスと状態の使用を同じロックで守り、ワーカーへ表内の参照を渡さない。
        mutable Thread::Mutex m_Mutex;
        Core::Container::UnorderedMap<uint64_t, PayloadState> m_States;
    };
} // namespace NorvesLib::Modules::Physics

// 当たりから表面に応じたエフェクトの放出要求を蓄積する型と CPU 部品を定義する。
#pragma once

#include "Particle/VfxEffectSystem.h"

namespace NorvesLib::Core::Particle
{
    /** @brief 呼び手が通知する当たりの情報。 */
    struct HitEffectHit
    {
        /** @brief 当たりの位置。 */
        Math::Vector3 Position = Math::Vector3(0.0f, 0.0f, 0.0f);
        /** @brief 被弾面から攻撃側へ向く法線（GR16 の CombatHitEvent の Normal と同じ向き）。 */
        Math::Vector3 Normal = Math::Vector3(0.0f, 1.0f, 0.0f);
        /** @brief エフェクトの選択に使う表面の名前。 */
        Container::String SurfaceName;
    };

    /** @brief 表面の名前とエフェクトの添字の対応。 */
    struct HitEffectTableEntry
    {
        /** @brief 完全一致で照合する表面の名前。 */
        Container::String SurfaceName;
        /** @brief 対応するエフェクトの添字。 */
        uint32_t EffectIndex = 0u;
    };

    /** @brief 当たりで選択するエフェクトと表面の対応表。 */
    struct HitEffectTable
    {
        /** @brief 放出要求から参照するエフェクトの配列。 */
        VariableArray<VfxEffectAsset> Effects;
        /** @brief 表面の名前ごとの対応。 */
        VariableArray<HitEffectTableEntry> Entries;
        /** @brief 名前が一致しない場合に使うエフェクトの添字。 */
        uint32_t DefaultEffectIndex = 0u;
    };

    /** @brief 当たり一回に対応する放出要求。 */
    struct HitEffectEmitRequest
    {
        /** @brief 放出するエフェクトの添字。 */
        uint32_t EffectIndex = 0u;
        /** @brief 放出する位置。 */
        Math::Vector3 Position;
        /** @brief 攻撃側へ向く単位法線。 */
        Math::Vector3 Normal;
    };

    /** @brief 当たりを表で選択したエフェクトの放出要求に変換する。 */
    class HitEffectEmitter
    {
    public:
        /** @brief 表が未設定であることを示す添字。 */
        static constexpr uint32_t InvalidEffectIndex = 0xFFFFFFFFu;
        /** @brief 待機できる放出要求の上限。 */
        static constexpr uint32_t DefaultMaxPendingRequests = 256u;
        /** @brief 当たりの放出に使うイベント名。 */
        static constexpr const char* HitEventName = "Hit";

        /** @brief 火花と衝撃の既定表を設定する。 */
        HitEffectEmitter();
        /** @brief 火花と衝撃の既定表を作る。 */
        static HitEffectTable MakeDefaultTable();

        /** @brief 検証に成功した表だけを設定し、失敗時は理由を返す。 */
        bool SetTable(const HitEffectTable& table, Container::String* outError = nullptr);
        /** @brief 現在の表を取得する。 */
        const HitEffectTable& GetTable() const;
        /** @brief 表面の名前からエフェクトの添字を選ぶ。 */
        uint32_t SelectEffect(const char* surfaceName) const;
        /** @brief 有効な当たりから放出要求を一つ積む。 */
        bool SubmitHit(const HitEffectHit& hit);
        /** @brief 待機中の放出要求を参照する。 */
        const VariableArray<HitEffectEmitRequest>& GetPendingRequests() const;
        /** @brief 出力を待機列で置き換え、待機列を空にする。 */
        void ConsumeRequests(VariableArray<HitEffectEmitRequest>& outRequests);
        /** @brief 待機列を積んだ順に VFX へ送り、空にする。 */
        void Dispatch(VfxEffectSystem& effects);
        /** @brief VFX の Clear 後にハンドルと待機列を忘れる。 */
        void ResetInstances();
        /** @brief エフェクトに対応するハンドルを取得する。未作成なら無効値を返す。 */
        VfxEffectHandle GetInstanceHandle(uint32_t effectIndex) const;
        /** @brief 落とした数を保ったまま待機列を空にする。 */
        void ClearRequests();
        /** @brief 上限に達して積めなかった要求の累計を取得する。 */
        uint32_t GetDroppedRequestCount() const;

    private:
        /** @brief 検証済みの対応表。 */
        HitEffectTable m_Table;
        /** @brief 消費を待つ放出要求の列。 */
        VariableArray<HitEffectEmitRequest> m_PendingRequests;
        /** @brief エフェクトの添字ごとに遅延作成するインスタンス。 */
        VariableArray<VfxEffectHandle> m_InstanceHandles;
        /** @brief 表の差し替えで古いインスタンスの破棄が必要かどうか。 */
        bool m_bInstancesNeedReset = false;
        /** @brief 待機列の上限で落とした要求の累計。 */
        uint32_t m_DroppedRequestCount = 0u;
        /** @brief 有効な表が設定済みかどうか。 */
        bool m_bHasTable = false;
    };
} // namespace NorvesLib::Core::Particle

#pragma once

#include "Gameplay/Enemy/EnemyDefinition.h"
#include "Random/Random.h"

namespace NorvesLib::Modules::Gameplay::Enemy
{
    /// run seed のリトルエンディアン表現と敵 ID の全バイトから再現用シードを作る。
    uint32_t MakeEnemyRandomSeed(uint64_t runSeed, const Core::Container::String& enemyId);

    /// 敵ごとの乱数列とクールダウンを保持し、次の攻撃パターンを選ぶ。
    class EnemyPatternSelector
    {
    public:
        /// 定義を借用する。定義は所有者の EnemyComponent と選択器より長く生存させ、
        /// 次の Reset まで内容を変更しないこと。
        void Reset(const EnemyDefinition& definition, uint32_t seed);
        /// 負または有限でない経過時間は無視する。
        void Advance(float deltaSeconds);
        /// 選択不能なら -1 を返し、乱数列を進めない。単独候補でも乱数を一度引く。
        int32_t Select(float distance, bool bHasDistance);
        /// 開幕パターンを選び、乱数列を進めずにクールダウンと直前の番号を更新する。
        int32_t SelectOpening();
        int32_t GetLastPatternIndex() const;
        /// 範囲外の番号には 0 を返す。
        float GetCooldownRemaining(int32_t index) const;

    private:
        float GetCandidateWeight(int32_t index, float distance, bool bHasDistance) const;
        int32_t CommitSelection(int32_t index);

        const EnemyDefinition* m_Definition = nullptr;
        Random::Generator m_Generator{0};
        Core::Container::VariableArray<float> m_Cooldowns;
        int32_t m_LastPatternIndex = -1;
    };
} // namespace NorvesLib::Modules::Gameplay::Enemy

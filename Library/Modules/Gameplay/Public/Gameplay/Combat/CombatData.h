// 戦闘判定のデータ定義と JSON 読み込みを提供する。
#pragma once

#include "Container/PointerTypes.h"
#include "Container/String.h"
#include "Container/VariableArray.h"
#include "Gameplay/Combat/AttackProfile.h"
#include "Gameplay/Combat/CombatJoint.h"
#include "Gameplay/Combat/CombatShape.h"
#include "Gameplay/Combat/HurtboxSetComponent.h"

#include <cstdint>

namespace NorvesLib::Modules::Gameplay::Combat
{
    struct CombatFactionTable
    {
        Core::Container::VariableArray<Core::Container::String> Names;
        Core::Container::VariableArray<uint32_t> Bits;

        // 名前に対応する単一ビットを返す。未定義の場合は outBit を変更しない。
        bool FindBit(const Core::Container::String& name, uint32_t& outBit) const;
    };

    struct CombatHitboxDefinition
    {
        // 空の場合は持ち主の姿勢を使う。
        Core::Identity JointName;
        CombatShape Shape;
        Core::Container::TSharedPtr<const AttackProfile> Profile;
    };

    struct CombatAttackDefinition
    {
        Core::Container::String SourceName;
        Core::Container::String Id;
        Core::Container::VariableArray<CombatHitboxDefinition> Hitboxes;
    };

    struct CombatHurtboxDefinition
    {
        Core::Container::String SourceName;
        Core::Container::String Id;
        CombatJointInfluenceThreshold WeakJoint;
        Core::Container::VariableArray<HurtboxPart> Parts;
    };

    // 読み込みに失敗した場合、out は変更せず outError にソース名とキーの道筋を返す。
    bool LoadCombatFactionsFromJson(const Core::Container::String& text, const Core::Container::String& sourceName,
                                   CombatFactionTable& out, Core::Container::String& outError);
    bool LoadCombatAttackFromJson(const Core::Container::String& text, const Core::Container::String& sourceName,
                                 const CombatFactionTable& factions, CombatAttackDefinition& out,
                                 Core::Container::String& outError);
    bool LoadCombatHurtboxesFromJson(const Core::Container::String& text, const Core::Container::String& sourceName,
                                    const CombatFactionTable& factions, CombatHurtboxDefinition& out,
                                    Core::Container::String& outError);

    // assetPath は Assets からの相対パス。
    bool LoadCombatFactionsFromAsset(const Core::Container::String& assetPath, CombatFactionTable& out,
                                    Core::Container::String& outError);
    bool LoadCombatAttackFromAsset(const Core::Container::String& assetPath, const CombatFactionTable& factions,
                                  CombatAttackDefinition& out, Core::Container::String& outError);
    bool LoadCombatHurtboxesFromAsset(const Core::Container::String& assetPath, const CombatFactionTable& factions,
                                     CombatHurtboxDefinition& out, Core::Container::String& outError);
} // namespace NorvesLib::Modules::Gameplay::Combat

#pragma once

#include "Container/Span.h"
#include "Module/IModule.h"
#include "Module/ModuleRegistry.h"
#include "Physics/PhysicsTypes.h"

namespace NorvesLib::Modules::Physics
{
    /**
     * @brief Physics module の公開寿命境界
     *
     * SceneQuery provider や具象実装は公開しない。query は Engine が保持する
     * SceneQuery façade だけを通じて利用する。
     */
    class IPhysicsModule : public Core::Module::IModule
    {
    public:
        virtual ~IPhysicsModule() = default;

        /**
         * @brief Entity を持たない、常に有効な静的コライダーのチャンクを登録する。
         *
         * 所有者の GameThread 以外は WrongThread、未初期化は InvalidState。
         * 遅延操作を順に反映した状態で同じ鍵があれば Duplicate。
         * トリガー、非有限な変換、不正な寸法や合成できない形状は InvalidArgument とし、
         * その場で全件検証して、不正な項目があればチャンク全体を登録しない。
         * 空の範囲は鍵だけを予約する。通知先と Body は持たず、問い合わせと接触には参加する。
         * 固定ステップ・可変キャラクター処理・入力収集中は操作を順に保留し、
         * 次の StepPreFixed または StepFixed の入口の検査後に反映する。それ以外は即時に反映する。
         */
        virtual EPhysicsResult RegisterStaticColliderChunk(StaticColliderChunkKey key,
            Core::Container::Span<const StaticColliderDesc> colliders) = 0;

        /**
         * @brief 静的コライダーのチャンクを解除し、以前のハンドルを無効にする。
         *
         * 所有者の GameThread 以外は WrongThread、未初期化は InvalidState。
         * 遅延操作を順に反映した状態で未知の鍵なら NotRegistered。
         * 登録と同じ時点で検証し、処理中は次のステップの入口まで順に保留する。
         */
        virtual EPhysicsResult UnregisterStaticColliderChunk(StaticColliderChunkKey key) = 0;

        /**
         * @brief 固定ステップ 1 歩の前半（剛体の前の位置の写し）
         *
         * 通常は Install で World の段 FixedBegin に登録したシステムが呼ぶ。テスト・ツールは直接呼んで 1 歩を進めてよい。
         */
        virtual void StepPreFixed(float fixedDeltaTime) = 0;

        /**
         * @brief 固定ステップ 1 歩の物理（積分・キャラクター・接触・イベント）
         *
         * 通常は段 FixedTick の、Component の FixedTick と CharacterBody の要求の後のシステムが呼ぶ。
         */
        virtual void StepFixed(float fixedDeltaTime) = 0;
    };

    /**
     * @brief Physics module を生成して registry へ登録する
     * @return 登録された借用ポインタ。重複時は既存 Physics module の借用を返す。
     */
    IPhysicsModule* RegisterPhysicsModule(Core::Module::ModuleRegistry& registry);

    /**
     * @brief registry から登録済み Physics module の借用を検索する
     */
    IPhysicsModule* FindPhysicsModule(Core::Module::ModuleRegistry& registry);
} // namespace NorvesLib::Modules::Physics

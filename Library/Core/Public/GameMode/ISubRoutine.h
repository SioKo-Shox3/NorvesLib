#pragma once

#include "GameModeContext.h"

namespace NorvesLib::Core::GameMode
{

    /**
     * @brief サブルーチン（GameMode 併走サブユニット）インターフェース
     *
     * アクティブな GameMode を Suspend させずに「並走」する小さな実行ユニット。
     * GameMode のスタック 1 段（StackEntry）に push/pop で積み下ろしし、
     * その段がアクティブ（スタック最上段）である間だけ Mode の Tick 後に
     * 登録順で Tick される。
     *
     * 寿命: サブルーチンは所属する StackEntry が生きている間だけ存在し、
     * その段が破棄される（Change/Pop/Reset/Shutdown）際に、Mode が Leave
     * される前に逆順で Leave される。push 時に Enter、pop 時に Leave が呼ばれる。
     *
     * 並走範囲: Suspend 中（スタック最上段でない）の段に積まれたサブルーチンは
     * Tick されない。Mode のような Suspend/Resume フックは設けない（並走は
     * アクティブ段のみという単純な不変条件を保つため）。
     *
     * 依存方針: このインターフェースは Core の一般的な sub-unit 抽象であり、
     * 特定の UI / オーバーレイ実装には一切依存しない。具体的なサブルーチン
     * （描画オーバーレイ等）は上位レイヤ側でこれを実装する。
     */
    class ISubRoutine
    {
    public:
        // 仮想デストラクタ
        virtual ~ISubRoutine() = default;

        /**
         * @brief サブルーチンが段に積まれたときに呼ばれる
         * @param ctx 実行コンテキスト（所属段のスコープを参照）
         */
        virtual void Enter(GameModeContext& ctx) { (void)ctx; }

        /**
         * @brief 所属段がアクティブな間、毎フレーム Mode の Tick 後に呼ばれる
         * @param ctx       実行コンテキスト
         * @param deltaTime フレーム間隔（秒）
         */
        virtual void Tick(GameModeContext& ctx, float deltaTime) { (void)ctx; (void)deltaTime; }

        /**
         * @brief サブルーチンが段から取り除かれる、または段が破棄されるときに呼ばれる
         * @param ctx 実行コンテキスト
         */
        virtual void Leave(GameModeContext& ctx) { (void)ctx; }

        /**
         * @brief デバッグ用の名前を返す
         * @return サブルーチンを識別する文字列リテラル
         */
        virtual const char* DebugName() const { return "SubRoutine"; }
    };

} // namespace NorvesLib::Core::GameMode

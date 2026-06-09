#pragma once

#include "Core/Public/GameMode/IStateMachine.h"
#include "Rendering3DTestData.h"

namespace Game::GameModes
{

    /**
     * @brief 3Dレンダリングテストのロジッククラス
     *
     * 球体と地面のメッシュを生成し、Object/MeshComponent/MeshProxy経由で
     * ディファードレンダリングパイプラインに自動描画させます。
     */
    class Rendering3DTestRoutine
    {
    public:
        /**
         * @brief ステート開始時の処理
         * @param proc ステートマシン
         * @param data ゲームモードデータ
         *
         * ProceduralMeshGeneratorで球とPlaneを生成、
         * RenderResourcesに登録、
         * WorldにObjectとMeshComponentを追加。
         */
        void Enter(NorvesLib::Core::GameMode::IStateMachine *proc, Rendering3DTestData &data);

        /**
         * @brief ステート実行中の処理
         * @param proc ステートマシン
         * @param data ゲームモードデータ
         * @param deltaTime フレーム間隔（秒）
         *
         * 球体を回転させてWorldTransformを更新。
         */
        void Do(NorvesLib::Core::GameMode::IStateMachine *proc, Rendering3DTestData &data, float deltaTime);

        /**
         * @brief ステート終了時の処理
         * @param proc ステートマシン
         * @param data ゲームモードデータ
         */
        void Leave(NorvesLib::Core::GameMode::IStateMachine *proc, Rendering3DTestData &data);
    };

} // namespace Game::GameModes

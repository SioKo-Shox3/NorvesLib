#pragma once

#include "Core/Public/GameMode/IGameMode.h"
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
         * @param ctx  実行コンテキスト
         * @param data ゲームモードデータ
         * @return 入場結果
         *
         * ProceduralMeshGeneratorで球とPlaneを生成、
         * RenderResourcesに登録、
         * WorldにObjectとMeshComponentを追加。
         */
        NorvesLib::Core::GameMode::GameModeEnterResult
        Enter(NorvesLib::Core::GameMode::GameModeContext& ctx, Rendering3DTestData& data);

        /**
         * @brief ステート実行中の処理
         * @param ctx  実行コンテキスト
         * @param data ゲームモードデータ
         * @param deltaTime フレーム間隔（秒）
         *
         * 球体を回転させてWorldTransformを更新。
         */
        void Tick(NorvesLib::Core::GameMode::GameModeContext& ctx, Rendering3DTestData& data, float deltaTime);

        /**
         * @brief ステート終了時の処理
         * @param ctx    実行コンテキスト
         * @param data   ゲームモードデータ
         * @param reason 退場理由
         */
        void Leave(NorvesLib::Core::GameMode::GameModeContext& ctx, Rendering3DTestData& data,
                   NorvesLib::Core::GameMode::GameModeExitReason reason);

        static constexpr const char* DebugName = "Rendering3DTest";
    };

} // namespace Game::GameModes

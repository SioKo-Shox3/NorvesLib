// filepath: c:\Users\KINGkawamura\Documents\NorvesLib\Library\Core\Public\Engine\IEngine.h
#pragma once

#include <memory>

namespace NorvesLib::Core
{

/**
 * @brief ゲームエンジンのインターフェース
 * 
 * アプリケーション内のゲーム機能を纏めるインターフェースです。
 * シーン管理、リソース管理、入力処理などのゲームシステムの中心的な役割を果たします。
 */
class IEngine
{
public:
    /**
     * @brief デストラクタ
     */
    virtual ~IEngine() = default;

    /**
     * @brief エンジンの初期化
     * 
     * エンジンの初期化処理を行います。
     * 
     * @return 初期化に成功した場合はtrue、失敗した場合はfalse
     */
    virtual bool Initialize() = 0;

    /**
     * @brief エンジンの更新
     * 
     * ゲームループの一部として呼び出され、エンジンの状態を更新します。
     * 
     * @param deltaTime 前フレームからの経過時間（秒）
     */
    virtual void Update(float deltaTime) = 0;

    /**
     * @brief エンジンのシャットダウン
     * 
     * エンジンのリソース解放とクリーンアップを行います。
     */
    virtual void Shutdown() = 0;

    /**
     * @brief エンジンが実行中かどうか
     * 
     * @return 実行中の場合はtrue、そうでない場合はfalse
     */
    virtual bool IsRunning() const = 0;

    /**
     * @brief エンジンの実行を停止する
     */
    virtual void Stop() = 0;
};

} // namespace NorvesLib::Core
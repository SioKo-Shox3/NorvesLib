#pragma once

#include <functional>
#include <memory>
#include <cstdint>

#ifdef _WIN32
#include <Windows.h>
#else
#include <ucontext.h>
#endif

#include "Core/Public/Container/Containers.h"

namespace NorvesLib::Thread
{

/**
 * @brief ファイバー（軽量スレッド）クラス
 * 
 * 協調的マルチタスクを実現するための軽量スレッド実装です。
 * ファイバーはスレッドよりも軽量で、明示的な切り替えによる
 * 協調的なスケジューリングを行います。
 */
class Fiber
{
public:
    /// ファイバー関数の型定義
    using FiberFunction = std::function<void()>;
    
    /// ファイバーの状態
    enum class State
    {
        CREATED,    ///< 作成済み
        RUNNING,    ///< 実行中
        SUSPENDED,  ///< 一時停止中
        FINISHED    ///< 終了
    };
    
    /**
     * @brief 現在のスレッドをファイバーとして初期化
     * @return 成功した場合true
     */
    static bool InitializeCurrentThread();
    
    /**
     * @brief 現在のスレッドのファイバー化を解除
     */
    static void UninitializeCurrentThread();
    
    /**
     * @brief メインファイバーに切り替える
     * @return 成功した場合true
     */
    static bool SwitchToMain();
    
    /**
     * @brief 現在実行中のファイバーを取得
     * @return 現在のファイバーへのポインタ
     */
    static Fiber* GetCurrentFiber();
    
    /**
     * @brief 新しいファイバーを作成
     * @param function 実行する関数
     * @param stackSize スタックサイズ（バイト単位、0の場合はデフォルト値）
     * @return 新しいファイバーのポインタ
     */
    static std::unique_ptr<Fiber> Create(FiberFunction function, size_t stackSize = 0);
    
public:
    /**
     * @brief デフォルトコンストラクタ（削除）
     */
    Fiber() = delete;
    
    /**
     * @brief 関数とスタックサイズを指定するコンストラクタ
     * 
     * @param function 実行する関数
     * @param stackSize スタックサイズ（バイト単位、0の場合はデフォルト値）
     */
    Fiber(FiberFunction function, size_t stackSize);
    
    /**
     * @brief コピーコンストラクタ（削除）
     */
    Fiber(const Fiber&) = delete;
    
    /**
     * @brief ムーブコンストラクタ
     */
    Fiber(Fiber&& other) noexcept;
    
    /**
     * @brief コピー代入演算子（削除）
     */
    Fiber& operator=(const Fiber&) = delete;
    
    /**
     * @brief ムーブ代入演算子
     */
    Fiber& operator=(Fiber&& other) noexcept;
    
    /**
     * @brief デストラクタ
     */
    ~Fiber();
    
    /**
     * @brief このファイバーに切り替える
     * @return 成功した場合true
     */
    bool SwitchTo();
    
    /**
     * @brief 現在のファイバーの実行を一時停止し、元のファイバーに戻る
     * @return 成功した場合true
     */
    bool Suspend();
    
    /**
     * @brief ファイバーの状態を取得
     * @return ファイバーの状態
     */
    State GetState() const;
    
    /**
     * @brief ファイバーが完了したかどうかを確認
     * @return 完了した場合true
     */
    bool IsFinished() const;

private:
    // ファイバーのエントリーポイント関数
    static void FiberEntryPoint(void* arg);
    
    // メインファイバーのインスタンス
    static thread_local Fiber* s_mainFiber;
    
    // 現在実行中のファイバー
    static thread_local Fiber* s_currentFiber;
    
    // ファイバー関数
    FiberFunction m_function;
    
    // 状態
    State m_state;
    
    // プラットフォーム固有のファイバーハンドル
#ifdef _WIN32
    // Windows実装
    void* m_fiberHandle;
    bool m_isMainFiber;
#else
    // POSIX実装
    ucontext_t m_context;
    char* m_stack;
    size_t m_stackSize;
    bool m_isMainFiber;
#endif
};

} // namespace NorvesLib::Thread
#pragma once

#include <thread>
#include <functional>
#include <atomic>
#include <cstdint>
#include "Core/Public/Container/Containers.h"

namespace NorvesLib::Thread
{

/**
 * @brief アフィニティマスク型（CPUコア指定用）
 */
using AffinityMask = uint64_t;

/**
 * @brief スレッドラッパークラス
 * 
 * std::threadをラップし、NorvesLib環境に最適化されたスレッド管理を提供します
 */
class Thread 
{
public:
    using ThreadId = std::thread::id;
    using ThreadFunction = std::function<void()>;
    
    /**
     * @brief デフォルトコンストラクタ
     * スレッドを作成しますが、開始はしません
     */
    Thread();
    
    /**
     * @brief 関数を指定して、スレッドを生成・開始するコンストラクタ
     * @param function 実行する関数
     */
    explicit Thread(ThreadFunction function);
    
    /**
     * @brief ムーブコンストラクタ
     */
    Thread(Thread&& other) noexcept;
    
    /**
     * @brief コピーコンストラクタ（削除：スレッドはコピー不可）
     */
    Thread(const Thread&) = delete;
    
    /**
     * @brief ムーブ代入演算子
     */
    Thread& operator=(Thread&& other) noexcept;
    
    /**
     * @brief コピー代入演算子（削除：スレッドはコピー不可）
     */
    Thread& operator=(const Thread&) = delete;
    
    /**
     * @brief デストラクタ
     * スレッドが実行中であればjoin()を呼び出します
     */
    ~Thread();
    
    /**
     * @brief スレッドを関数と共に開始する
     * @param function 実行する関数
     */
    void Start(ThreadFunction function);
    
    /**
     * @brief スレッドの終了を待機する
     */
    void Join();
    
    /**
     * @brief スレッドをデタッチする（バックグラウンドで実行）
     */
    void Detach();
    
    /**
     * @brief スレッドが実行可能か確認する
     * @return スレッドが実行可能であれば true
     */
    bool Joinable() const;

    /**
     * @brief スレッドIDを取得する
     * @return スレッドID
     */
    ThreadId GetId() const;
    
    /**
     * @brief 現在のスレッドのIDを取得する
     * @return 現在のスレッドID
     */
    static ThreadId GetCurrentThreadId();
    
    /**
     * @brief スレッドの優先度を設定する
     * @param priority 優先度（0-100）、高いほど優先度が高い
     * @return 成功すればtrue
     */
    bool SetPriority(int priority);
    
    /**
     * @brief スレッドの名前を設定する
     * @param name スレッド名
     */
    void SetName(const Core::Container::String& name);
    
    /**
     * @brief スレッドのCPUアフィニティを設定する
     * 特定のCPUコア上でスレッドを実行するように制限します
     * 
     * @param affinityMask CPUアフィニティマスク（各ビットが1つのコアを表す）
     * @return 設定に成功した場合はtrue、そうでない場合はfalse
     */
    bool SetAffinity(AffinityMask affinityMask);
    
    /**
     * @brief 現在のスレッドのCPUアフィニティを設定する（静的バージョン）
     * 
     * @param affinityMask CPUアフィニティマスク（各ビットが1つのコアを表す）
     * @return 設定に成功した場合はtrue、そうでない場合はfalse
     */
    static bool SetCurrentThreadAffinity(AffinityMask affinityMask);
    
    /**
     * @brief ハードウェアのコンカレントスレッド数を取得する
     * @return スレッド数
     */
    static unsigned int GetHardwareConcurrency();

private:
    std::thread m_thread;
    std::atomic<bool> m_isRunning;
    Core::Container::String m_name;
};

} // namespace NorvesLib::Thread

#include "EngineGlobals/GlobalMemoryAllocator.h"
#include <cassert>

namespace NorvesLib::Core
{
    // スタティックメンバ変数の初期化
    Memory::TLSFAllocator *GlobalMemoryAllocator::s_instance = nullptr;

    // シングルトンインスタンスの取得
    Memory::TLSFAllocator &GlobalMemoryAllocator::Get()
    {
        // インスタンスが初期化されていない場合は自動的に初期化
        if (!s_instance)
        {
            Initialize();
        }
        return *s_instance;
    }

    // アロケーターの初期化
    void GlobalMemoryAllocator::Initialize()
    {
        // 既に初期化されている場合は何もしない
        if (s_instance)
        {
            return;
        }

        // TLSFアロケーターのインスタンスを2GBのメモリで作成（デフォルトの最小ブロックサイズを使用）
        s_instance = new Memory::TLSFAllocator(GLOBAL_MEMORY_SIZE, Memory::TLSFAllocator::DEFAULT_ALIGNMENT);
    }

    // アロケーターの終了処理
    void GlobalMemoryAllocator::Shutdown()
    {
        // インスタンスが初期化されていない場合は何もしない
        if (!s_instance)
        {
            return;
        }

        // インスタンスを解放
        delete s_instance;
        s_instance = nullptr;
    }
}
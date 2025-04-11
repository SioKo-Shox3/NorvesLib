#include "EngineGlobals/GlobalMemoryAllocator.h"
#include <cassert>

namespace NorvesLib::Core
{
    // スタティックメンバ変数の初期化
    Memory::TLSFAllocator* GlobalMemoryAllocator::s_instance = nullptr;

    // シングルトンインスタンスの取得
    Memory::TLSFAllocator& GlobalMemoryAllocator::Get()
    {
        // インスタンスが初期化されていない場合はエラー
        assert(s_instance && "GlobalMemoryAllocator has not been initialized");
        return *s_instance;
    }

    // アロケーターの初期化
    void GlobalMemoryAllocator::Initialize()
    {
        // 多重初期化のチェック
        assert(!s_instance && "GlobalMemoryAllocator has already been initialized");
        
        // TLSFアロケーターのインスタンスを2GBのメモリで作成
        s_instance = new Memory::TLSFAllocator(GLOBAL_MEMORY_SIZE);
    }

    // アロケーターの終了処理
    void GlobalMemoryAllocator::Shutdown()
    {
        // インスタンスが初期化されていない場合はエラー
        assert(s_instance && "GlobalMemoryAllocator has not been initialized");
        
        // インスタンスを解放
        delete s_instance;
        s_instance = nullptr;
    }
}
#pragma once

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Memory
{
    /**
     * @brief プラットフォーム定義
     *
     * サポートするプラットフォームの列挙型
     */
    enum class Platform : uint8_t
    {
        Windows,
        Linux,
        MacOS,
        PlayStation,
        Xbox,
        Nintendo,
        Unknown
    };

    /**
     * @brief 現在のプラットフォームを取得
     */
    constexpr Platform GetCurrentPlatform()
    {
#if defined(_WIN32) || defined(_WIN64)
        return Platform::Windows;
#elif defined(__linux__)
        return Platform::Linux;
#elif defined(__APPLE__)
        return Platform::MacOS;
#elif defined(__ORBIS__) || defined(__PROSPERO__)
        return Platform::PlayStation;
#elif defined(_XBOX_ONE) || defined(_GAMING_XBOX)
        return Platform::Xbox;
#elif defined(__NINTENDO__)
        return Platform::Nintendo;
#else
        return Platform::Unknown;
#endif
    }

    /**
     * @brief プラットフォーム依存のメモリ設定
     */
    namespace Config
    {
        // ===========================================
        // 基本アライメント設定
        // ===========================================

#if defined(_WIN32) || defined(_WIN64)
        // Windows: x64では16バイトアライメントが標準
        static constexpr size_t DefaultAlignment = 16;
        static constexpr size_t CacheLineSize = 64;
        static constexpr size_t PageSize = 4096;
        static constexpr size_t LargePageSize = 2 * 1024 * 1024; // 2MB

#elif defined(__linux__)
        // Linux: 同様に16バイトアライメント
        static constexpr size_t DefaultAlignment = 16;
        static constexpr size_t CacheLineSize = 64;
        static constexpr size_t PageSize = 4096;
        static constexpr size_t LargePageSize = 2 * 1024 * 1024; // 2MB

#elif defined(__APPLE__)
        // macOS/iOS: ARMでは特にアライメント要件が厳しい
#if defined(__arm64__)
        static constexpr size_t DefaultAlignment = 16;
        static constexpr size_t CacheLineSize = 128; // Apple Silicon
#else
        static constexpr size_t DefaultAlignment = 16;
        static constexpr size_t CacheLineSize = 64;
#endif
        static constexpr size_t PageSize = 16384;                 // 16KB (Apple Silicon)
        static constexpr size_t LargePageSize = 16 * 1024 * 1024; // 16MB

#elif defined(__ORBIS__) || defined(__PROSPERO__)
        // PlayStation: 独自のメモリ要件
        static constexpr size_t DefaultAlignment = 16;
        static constexpr size_t CacheLineSize = 64;
        static constexpr size_t PageSize = 16384;                // 16KB
        static constexpr size_t LargePageSize = 2 * 1024 * 1024; // 2MB

#elif defined(_XBOX_ONE) || defined(_GAMING_XBOX)
        // Xbox: DirectXに合わせた設定
        static constexpr size_t DefaultAlignment = 16;
        static constexpr size_t CacheLineSize = 64;
        static constexpr size_t PageSize = 4096;
        static constexpr size_t LargePageSize = 2 * 1024 * 1024; // 2MB

#elif defined(__NINTENDO__)
        // Nintendo: プラットフォーム固有の設定
        static constexpr size_t DefaultAlignment = 16;
        static constexpr size_t CacheLineSize = 64;
        static constexpr size_t PageSize = 4096;
        static constexpr size_t LargePageSize = 2 * 1024 * 1024; // 2MB

#else
        // デフォルト設定
        static constexpr size_t DefaultAlignment = 16;
        static constexpr size_t CacheLineSize = 64;
        static constexpr size_t PageSize = 4096;
        static constexpr size_t LargePageSize = 2 * 1024 * 1024; // 2MB
#endif

        // ===========================================
        // メモリ確保サイズ制限
        // ===========================================

#if defined(_WIN32) || defined(_WIN64)
#if defined(_WIN64)
        // 64-bit Windows: 理論上128TBまで
        static constexpr size_t MaxAllocationSize = 8ULL * 1024 * 1024 * 1024; // 8GB（実用的な上限）
        static constexpr size_t MaxTotalMemory = 64ULL * 1024 * 1024 * 1024;   // 64GB
#else
        // 32-bit Windows: 2GB制限
        static constexpr size_t MaxAllocationSize = 512 * 1024 * 1024;      // 512MB
        static constexpr size_t MaxTotalMemory = 2ULL * 1024 * 1024 * 1024; // 2GB
#endif

#elif defined(__linux__)
        static constexpr size_t MaxAllocationSize = 8ULL * 1024 * 1024 * 1024; // 8GB
        static constexpr size_t MaxTotalMemory = 64ULL * 1024 * 1024 * 1024;   // 64GB

#elif defined(__APPLE__)
#if defined(__arm64__)
        // Apple Silicon Mac/iOS
        static constexpr size_t MaxAllocationSize = 8ULL * 1024 * 1024 * 1024; // 8GB
        static constexpr size_t MaxTotalMemory = 32ULL * 1024 * 1024 * 1024;   // 32GB
#else
        static constexpr size_t MaxAllocationSize = 8ULL * 1024 * 1024 * 1024; // 8GB
        static constexpr size_t MaxTotalMemory = 64ULL * 1024 * 1024 * 1024;   // 64GB
#endif

#elif defined(__ORBIS__)
        // PlayStation 4: 約5.5GBが利用可能
        static constexpr size_t MaxAllocationSize = 512 * 1024 * 1024;      // 512MB
        static constexpr size_t MaxTotalMemory = 5ULL * 1024 * 1024 * 1024; // 5GB

#elif defined(__PROSPERO__)
        // PlayStation 5: 約12GBが利用可能
        static constexpr size_t MaxAllocationSize = 2ULL * 1024 * 1024 * 1024; // 2GB
        static constexpr size_t MaxTotalMemory = 12ULL * 1024 * 1024 * 1024;   // 12GB

#elif defined(_XBOX_ONE)
        // Xbox One: 約5GBが利用可能
        static constexpr size_t MaxAllocationSize = 512 * 1024 * 1024;      // 512MB
        static constexpr size_t MaxTotalMemory = 5ULL * 1024 * 1024 * 1024; // 5GB

#elif defined(_GAMING_XBOX)
        // Xbox Series X|S: 約13.5GBが利用可能
        static constexpr size_t MaxAllocationSize = 2ULL * 1024 * 1024 * 1024; // 2GB
        static constexpr size_t MaxTotalMemory = 13ULL * 1024 * 1024 * 1024;   // 13GB

#elif defined(__NINTENDO__)
        // Nintendo Switch: 約3GBが利用可能
        static constexpr size_t MaxAllocationSize = 256 * 1024 * 1024;      // 256MB
        static constexpr size_t MaxTotalMemory = 3ULL * 1024 * 1024 * 1024; // 3GB

#else
        // デフォルト（保守的な設定）
        static constexpr size_t MaxAllocationSize = 1ULL * 1024 * 1024 * 1024; // 1GB
        static constexpr size_t MaxTotalMemory = 4ULL * 1024 * 1024 * 1024;    // 4GB
#endif

        // ===========================================
        // サイズクラス定義
        // ===========================================

        // 小さなオブジェクト用サイズクラス（バイト単位）
        static constexpr size_t SizeClasses[] = {
            16, 32, 48, 64, 80, 96, 112, 128, // 16バイト刻み: 8クラス
            160, 192, 224, 256,               // 32バイト刻み: 4クラス
            320, 384, 448, 512,               // 64バイト刻み: 4クラス
            640, 768, 896, 1024,              // 128バイト刻み: 4クラス
            1280, 1536, 1792, 2048,           // 256バイト刻み: 4クラス
            2560, 3072, 3584, 4096,           // 512バイト刻み: 4クラス
            5120, 6144, 7168, 8192,           // 1024バイト刻み: 4クラス
            10240, 12288, 14336, 16384,       // 2048バイト刻み: 4クラス
            20480, 24576, 28672, 32768,       // 4096バイト刻み: 4クラス
            40960, 49152, 57344, 65536,       // 8192バイト刻み: 4クラス
            81920, 98304, 114688, 131072,     // 16384バイト刻み: 4クラス (128KB)
            163840, 196608, 229376, 262144    // 32768バイト刻み: 4クラス (256KB)
        };

        static constexpr size_t NumSizeClasses = sizeof(SizeClasses) / sizeof(SizeClasses[0]);

        // 大きなオブジェクトの閾値（これ以上は直接OSから確保）
        static constexpr size_t LargeObjectThreshold = 262144; // 256KB

        // ===========================================
        // スレッドローカルキャッシュ設定
        // ===========================================

        // スレッドローカルキャッシュの各サイズクラスあたりの最大オブジェクト数
        static constexpr size_t ThreadCacheMaxObjects = 64;

        // スレッドローカルキャッシュからセントラルへのバッチ転送サイズ
        static constexpr size_t ThreadCacheBatchSize = 16;

        // スレッドローカルキャッシュの最大合計サイズ
        static constexpr size_t ThreadCacheMaxTotalSize = 2 * 1024 * 1024; // 2MB

        // ===========================================
        // フレームアロケータ設定
        // ===========================================

        // デフォルトのフレームアロケータサイズ
        static constexpr size_t DefaultFrameAllocatorSize = 16 * 1024 * 1024; // 16MB

        // ===========================================
        // スタックアロケータ設定
        // ===========================================

        // デフォルトのスタックアロケータサイズ
        static constexpr size_t DefaultStackAllocatorSize = 4 * 1024 * 1024; // 4MB

        // ===========================================
        // プールアロケータ設定
        // ===========================================

        // デフォルトのチャンクあたりのブロック数
        static constexpr size_t DefaultBlocksPerChunk = 64;

        // ===========================================
        // グローバルアロケータ設定
        // ===========================================

        // 初期ヒープサイズ
        static constexpr size_t InitialHeapSize = 64 * 1024 * 1024; // 64MB

        // ヒープ拡張サイズ
        static constexpr size_t HeapGrowSize = 32 * 1024 * 1024; // 32MB
    }

    // ===========================================
    // ユーティリティ関数
    // ===========================================

    /**
     * @brief サイズをアライメントに合わせて切り上げる
     * @param size 元のサイズ
     * @param alignment アライメント要件
     * @return アライメントされたサイズ
     */
    constexpr size_t AlignUp(size_t size, size_t alignment)
    {
        return (size + alignment - 1) & ~(alignment - 1);
    }

    /**
     * @brief サイズをアライメントに合わせて切り下げる
     * @param size 元のサイズ
     * @param alignment アライメント要件
     * @return アライメントされたサイズ
     */
    constexpr size_t AlignDown(size_t size, size_t alignment)
    {
        return size & ~(alignment - 1);
    }

    /**
     * @brief ポインタがアライメントされているか確認
     * @param ptr 確認するポインタ
     * @param alignment アライメント要件
     * @return アライメントされている場合true
     */
    inline bool IsAligned(const void *ptr, size_t alignment)
    {
        return (reinterpret_cast<uintptr_t>(ptr) & (alignment - 1)) == 0;
    }

    /**
     * @brief ポインタをアライメントに合わせて調整
     * @param ptr 元のポインタ
     * @param alignment アライメント要件
     * @return アライメントされたポインタ
     */
    inline void *AlignPointer(void *ptr, size_t alignment)
    {
        uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        uintptr_t aligned = AlignUp(addr, alignment);
        return reinterpret_cast<void *>(aligned);
    }

    /**
     * @brief 要求サイズに適したサイズクラスを取得
     * @param size 要求サイズ
     * @return サイズクラスのインデックス（見つからない場合は-1）
     */
    constexpr int GetSizeClassIndex(size_t size)
    {
        for (size_t i = 0; i < Config::NumSizeClasses; ++i)
        {
            if (size <= Config::SizeClasses[i])
            {
                return static_cast<int>(i);
            }
        }
        return -1; // 大きなオブジェクト
    }

    /**
     * @brief サイズクラスインデックスからサイズを取得
     * @param index サイズクラスインデックス
     * @return サイズクラスのサイズ
     */
    constexpr size_t GetSizeClassSize(int index)
    {
        if (index < 0 || index >= static_cast<int>(Config::NumSizeClasses))
        {
            return 0;
        }
        return Config::SizeClasses[index];
    }

} // namespace NorvesLib::Memory

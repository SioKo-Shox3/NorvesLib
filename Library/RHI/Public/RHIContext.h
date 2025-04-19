// filepath: c:\Users\KINGkawamura\Documents\NorvesLib\Library\RHI\Public\RHIContext.h
#pragma once

#include "RHITypes.h"
#include "IDevice.h"
#include <type_traits>
#include <memory>
#include <cassert>

namespace NorvesLib::RHI 
{

/**
 * @brief RHI実装を切り替えるためのコンテキストクラス
 * 
 * @tparam TDevice RHIデバイスの具体的な実装クラス
 */
template<typename TDevice>
class RHIContext 
{
public:
    /**
     * @brief RHIコンテキストの初期化
     * 
     * @param initParams 初期化パラメータ (実装ごとに異なる形式)
     * @return 初期化が成功したかどうか
     */
    template<typename... Args>
    static bool Initialize(Args&&... args) 
    {
        if (s_initialized) 
        {
            return true;
        }

        try {
            s_device = TDevice::Create(std::forward<Args>(args)...);
            s_initialized = true;
            return true;
        }
        catch (const std::exception& e) 
        {
            // 初期化失敗
            return false;
        }
    }

    /**
     * @brief RHIコンテキストの終了処理
     */
    static void Shutdown() 
    {
        if (!s_initialized) 
        {
            return;
        }

        s_device = nullptr;
        s_initialized = false;
    }

    /**
     * @brief RHIデバイスの取得
     * 
     * @return RHIデバイスのポインタ
     */
    static DevicePtr GetDevice() 
    {
        assert(s_initialized && "RHIContextが初期化されていません");
        return s_device;
    }

    /**
     * @brief 現在使用中のRHI APIの種類を取得
     * 
     * @return API種類
     */
    static API GetAPI() 
    {
        if (!s_initialized) 
        {
            return API::None;
        }
        return s_device->GetAPI();
    }
    
    /**
     * @brief 初期化済みかどうかを確認
     * 
     * @return 初期化状態
     */
    static bool IsInitialized() 
    {
        return s_initialized;
    }

private:
    static DevicePtr s_device;
    static bool s_initialized;
};

// 静的メンバ変数の定義
template<typename TDevice>
DevicePtr RHIContext<TDevice>::s_device = nullptr;

template<typename TDevice>
bool RHIContext<TDevice>::s_initialized = false;

} // namespace NorvesLib::RHI
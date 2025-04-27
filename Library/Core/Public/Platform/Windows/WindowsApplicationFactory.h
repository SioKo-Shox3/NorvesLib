#pragma once

#include "Application/ApplicationFactory.h"
#include <memory>

namespace NorvesLib {
namespace Core {
namespace Platform {

/**
 * @brief Windows向けのアプリケーションファクトリークラス
 * 
 * Windows向けの実装を提供するアプリケーションファクトリーです。
 */
class WindowsApplicationFactory
{
public:
    /**
     * @brief Windows向けのアプリケーションインスタンスを作成
     * @return 作成されたWindowsアプリケーションインスタンス
     */
    static std::unique_ptr<IApplication> CreateWindowsApplication();
    
    /**
     * @brief Windows向けのウィンドウインスタンスを作成
     * @return 作成されたWindowsウィンドウインスタンス
     */
    static std::shared_ptr<IWindow> CreateWindowsWindow();
};

} // namespace Platform
} // namespace Core
} // namespace NorvesLib
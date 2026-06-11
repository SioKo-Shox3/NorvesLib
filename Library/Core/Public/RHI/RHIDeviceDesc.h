#pragma once

#include "Debug/DebugConfig.h"

namespace NorvesLib::RHI
{

    /**
     * @brief グラフィックスAPIの選択を表す列挙型
     *
     * 本列挙はデバイス生成時のバックエンド選択用です。
     * IDevice::GetAPI() が返す実行時バックエンド報告用の RHI::API とは別物です。
     *
     * Default は実行時に Platform::GetDefaultGraphicsAPI() で解決されます。
     * 現時点では Windows 上の Default は Vulkan になります。
     */
    enum class GraphicsAPI
    {
        Default,  ///< プラットフォームデフォルトAPIを使用
        Vulkan,   ///< Vulkan（対応済み）
        D3D12,    ///< Direct3D 12（未実装）
        Null,     ///< ヌルバックエンド（テスト・ヘッドレス用、未実装）
    };

    /**
     * @brief RHIデバイス生成記述子
     *
     * RHIDeviceFactory::CreateRHIDevice() に渡してデバイスを生成します。
     * フィールドは実際に Vulkan バックエンドで使用可能なものだけを含みます。
     */
    struct RHIDeviceDesc
    {
        /**
         * @brief 使用するグラフィックスAPI
         * Default の場合は Platform::GetDefaultGraphicsAPI() で自動選択されます。
         */
        GraphicsAPI Api = GraphicsAPI::Default;

        /**
         * @brief RHI バリデーションレイヤーを有効にするか
         *
         * デフォルトは NORVES_BUILD_DEBUG に連動（Debug ビルドで true）。
         * リリースビルドでデバッグ検証が不要な場合は false に設定してください。
         * TODO: ビルド種別マクロ連動は将来的に BootConfig 経由で自動反映予定。
         */
#if NORVES_BUILD_DEBUG
        bool bEnableValidation = true;
#else
        bool bEnableValidation = false;
#endif
    };

} // namespace NorvesLib::RHI

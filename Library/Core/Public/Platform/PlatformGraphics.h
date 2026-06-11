#pragma once

#include "RHI/RHIDeviceDesc.h"

namespace NorvesLib::Core::Platform
{

    /**
     * @brief プラットフォームの既定グラフィックスAPIを返す
     *
     * フェーズ4方式に準拠した静的 Platform 関数です。
     * RHI::GraphicsAPI::Default が指定されたとき、このAPI解決関数を経由して
     * 実際のバックエンドに振り分けます。
     *
     * - Windows: RHI::GraphicsAPI::Vulkan を返します。
     *
     * @return プラットフォームデフォルトの GraphicsAPI 値
     */
    RHI::GraphicsAPI GetDefaultGraphicsAPI();

    /**
     * @brief 指定したグラフィックスAPIがサポートされているか確認する
     *
     * @param api 確認するAPI
     * @return 対応している場合 true
     * @note GraphicsAPI::Default は解決後の API を渡すこと（Default 自体は false を返す）
     */
    bool IsGraphicsAPISupported(RHI::GraphicsAPI api);

} // namespace NorvesLib::Core::Platform

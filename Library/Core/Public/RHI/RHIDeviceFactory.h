#pragma once

#include "RHITypes.h"
#include "RHIDeviceDesc.h"

namespace NorvesLib::RHI
{

    /**
     * @brief RHIデバイスファクトリ
     *
     * バックエンド固有ヘッダを include せず、抽象 DevicePtr を返します。
     * 呼び出し側は RHI::IDevice インターフェースのみを知れば十分です。
     *
     * @note 現時点で対応しているバックエンドは Vulkan のみです。
     *       D3D12 / Null バックエンドは今後のフェーズで追加予定。
     */

    /**
     * @brief RHIデバイスを生成して返す
     *
     * desc.Api が GraphicsAPI::Default の場合は
     * Platform::GetDefaultGraphicsAPI() でプラットフォーム既定APIに解決されます。
     * 対応していないAPIが要求された場合はエラーログを出力して nullptr を返します。
     *
     * @param desc デバイス生成記述子
     * @return 生成されたデバイスの共有ポインタ。失敗時は nullptr
     */
    DevicePtr CreateRHIDevice(const RHIDeviceDesc& desc);

} // namespace NorvesLib::RHI

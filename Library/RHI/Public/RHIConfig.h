// filepath: c:\Users\KINGkawamura\Documents\NorvesLib\Library\RHI\Public\RHIConfig.h
#pragma once

#include "RHITypes.h"
#include "RHIContext.h"
#include <cassert>
#include <type_traits>

// 利用可能なRHI実装定義
#define RHI_API_NONE      0
#define RHI_API_VULKAN    1
#define RHI_API_DIRECTX11 2
#define RHI_API_DIRECTX12 3
#define RHI_API_OPENGL    4

// デフォルトのRHI APIとしてVulkanを設定
// プロジェクト設定で上書き可能
#ifndef RHI_API
#define RHI_API RHI_API_VULKAN
#endif

// 指定されたRHI APIに基づいて型定義を行うマクロ
#if RHI_API == RHI_API_VULKAN
    #define RHI_IMPLEMENTATION_NAMESPACE NorvesLib::RHI::Vulkan
    #define RHI_DEVICE_TYPE VulkanDevice
    #define RHI_IMPLEMENTATION_INCLUDE "RHI/Private/Vulkan/VulkanRHI.h"
#elif RHI_API == RHI_API_DIRECTX11
    static_assert(false, "DirectX11 APIはまだ実装されていません");
#elif RHI_API == RHI_API_DIRECTX12
    static_assert(false, "DirectX12 APIはまだ実装されていません");
#elif RHI_API == RHI_API_OPENGL
    static_assert(false, "OpenGL APIはまだ実装されていません");
#else
    #error "未サポートのRHI APIが指定されています"
#endif

// 現在のRHIコンテキスト型の簡易アクセス定義
namespace NorvesLib::RHI 
{
    using CurrentRHIContext = NorvesLib::RHI::RHIContext<RHI_IMPLEMENTATION_NAMESPACE::RHI_DEVICE_TYPE>;
}

// RHI APIを簡単に切り替えるためのユーティリティマクロ
#define RHI_INITIALIZE(...) NorvesLib::RHI::CurrentRHIContext::Initialize(__VA_ARGS__)
#define RHI_SHUTDOWN() NorvesLib::RHI::CurrentRHIContext::Shutdown()
#define RHI_GET_DEVICE() NorvesLib::RHI::CurrentRHIContext::GetDevice()
#define RHI_GET_API() NorvesLib::RHI::CurrentRHIContext::GetAPI()
#define RHI_IS_INITIALIZED() NorvesLib::RHI::CurrentRHIContext::IsInitialized()
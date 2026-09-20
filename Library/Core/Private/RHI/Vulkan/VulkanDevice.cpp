#include "VulkanDevice.h"
#include "VulkanBuffer.h"
#include "VulkanAccelerationStructure.h"
#include "VulkanTexture.h"
#include "VulkanSampler.h"
#include "VulkanShader.h"
#include "VulkanShaderCompiler.h"
#include "VulkanSlangCompiler.h"
#include "VulkanPipeline.h"
#include "VulkanRenderPass.h"
#include "VulkanFramebuffer.h"
#include "VulkanDescriptorSet.h"
#include "VulkanCommandList.h"
#include "VulkanSwapChain.h"
#include "VulkanGPUResourceAllocator.h"
#include "Logging/LogMacros.h"
#include "Math/MatrixUtils.h"
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <limits>
#include "Container/Containers.h"

// Dynamic dispatcherの定義
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace NorvesLib::RHI::Vulkan
{
    // 明示的なusing宣言（グローバル名前空間から参照）
    using ::NorvesLib::Core::Container::DynamicPointerCast;
    using ::NorvesLib::Core::Container::FixedArray;
    using ::NorvesLib::Core::Container::MakeUnique;
    using ::NorvesLib::Core::Container::MakeShared;
    using ::NorvesLib::Core::Container::String;
    using ::NorvesLib::Core::Container::StringBuilder;
    using ::NorvesLib::Core::Container::StaticPointerCast;
    using ::NorvesLib::Core::Container::TSharedPtr;
    using ::NorvesLib::Core::Container::TWeakPtr;
    using ::NorvesLib::Core::Container::UnorderedMap;
    using ::NorvesLib::Core::Container::VariableArray;

    namespace
    {
        VkResult WaitIdleWithoutResultCheck(vk::Device device, const char* context) noexcept
        {
            if (!device)
            {
                return VK_SUCCESS;
            }

            const VkResult result = VULKAN_HPP_DEFAULT_DISPATCHER.vkDeviceWaitIdle(static_cast<VkDevice>(device));
            if (result != VK_SUCCESS)
            {
                NORVES_LOG_ERROR("Vulkan", "vkDeviceWaitIdle failed context=%s result=%d", context, static_cast<int32_t>(result));
            }
            return result;
        }

        vk::BuildAccelerationStructureFlagsKHR ToVkAccelerationStructureBuildFlags(
            const AccelerationStructureDesc& desc)
        {
            vk::BuildAccelerationStructureFlagsKHR flags =
                vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
            if (desc.allowUpdate)
            {
                flags |= vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate;
            }
            if (desc.allowCompaction)
            {
                flags |= vk::BuildAccelerationStructureFlagBitsKHR::eAllowCompaction;
            }
            return flags;
        }

        vk::AccelerationStructureGeometryKHR MakeAccelerationStructureCapacityGeometry(
            const AccelerationStructureGeometryCapacityDesc& capacity)
        {
            vk::AccelerationStructureGeometryKHR geometry{};
            if (capacity.type == AccelerationStructureGeometryType::Triangles)
            {
                vk::AccelerationStructureGeometryTrianglesDataKHR triangles{};
                triangles.vertexFormat = vk::Format::eR32G32B32Sfloat;
                triangles.vertexStride = 12;
                const uint64_t maxVertex = static_cast<uint64_t>(capacity.maxPrimitiveCount) * 3u - 1u;
                triangles.maxVertex = static_cast<uint32_t>(
                    std::min<uint64_t>(maxVertex, std::numeric_limits<uint32_t>::max()));
                triangles.indexType = vk::IndexType::eUint32;
                geometry.geometryType = vk::GeometryTypeKHR::eTriangles;
                geometry.geometry.triangles = triangles;
            }
            else
            {
                vk::AccelerationStructureGeometryAabbsDataKHR aabbs{};
                aabbs.stride = 24;
                geometry.geometryType = vk::GeometryTypeKHR::eAabbs;
                geometry.geometry.aabbs = aabbs;
            }

            if (capacity.opaque)
            {
                geometry.flags = vk::GeometryFlagBitsKHR::eOpaque;
            }
            return geometry;
        }
    }

    VulkanAccelerationStructure::VulkanAccelerationStructure(
        TSharedPtr<VulkanDevice> device,
        const AccelerationStructureDesc& desc)
        : m_device(device), m_desc(desc)
    {
        if (!m_device || !m_device->GetCapabilities().RayTracing.bAccelerationStructure ||
            !m_device->GetCapabilities().bBufferDeviceAddress || !IsValidAccelerationStructureDesc(m_desc))
        {
            throw std::invalid_argument("Vulkan加速構造の記述子またはデバイス能力が無効です");
        }

        VariableArray<vk::AccelerationStructureGeometryKHR> capacityGeometries;
        VariableArray<uint32_t> maxPrimitiveCounts;
        if (m_desc.type == AccelerationStructureType::BottomLevel)
        {
            for (const AccelerationStructureGeometryCapacityDesc& capacity : m_desc.geometryCapacities)
            {
                capacityGeometries.push_back(MakeAccelerationStructureCapacityGeometry(capacity));
                maxPrimitiveCounts.push_back(capacity.maxPrimitiveCount);
            }
        }
        else
        {
            vk::AccelerationStructureGeometryKHR instancesGeometry{};
            vk::AccelerationStructureGeometryInstancesDataKHR instancesData{};
            instancesData.arrayOfPointers = VK_FALSE;
            instancesGeometry.geometryType = vk::GeometryTypeKHR::eInstances;
            instancesGeometry.geometry.instances = instancesData;
            capacityGeometries.push_back(instancesGeometry);
            maxPrimitiveCounts.push_back(m_desc.maxInstanceCount);
        }

        vk::AccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.type = m_desc.type == AccelerationStructureType::BottomLevel
            ? vk::AccelerationStructureTypeKHR::eBottomLevel
            : vk::AccelerationStructureTypeKHR::eTopLevel;
        buildInfo.flags = ToVkAccelerationStructureBuildFlags(m_desc);
        buildInfo.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
        buildInfo.geometryCount = static_cast<uint32_t>(capacityGeometries.size());
        buildInfo.pGeometries = capacityGeometries.data();

        vk::AccelerationStructureBuildSizesInfoKHR buildSizes{};
        m_device->GetVkDevice().getAccelerationStructureBuildSizesKHR(
            vk::AccelerationStructureBuildTypeKHR::eDevice,
            &buildInfo,
            maxPrimitiveCounts.data(),
            &buildSizes);
        if (buildSizes.accelerationStructureSize == 0 || buildSizes.buildScratchSize == 0)
        {
            throw std::runtime_error("Vulkan加速構造の容量を計算できませんでした");
        }

        m_size = buildSizes.accelerationStructureSize;
        m_buildScratchSize = buildSizes.buildScratchSize;
        BufferDesc storageDesc;
        storageDesc.Size = m_size;
        storageDesc.Usage = ResourceUsage::StorageBuffer | ResourceUsage::BufferDeviceAddress;
        storageDesc.DebugName = m_desc.type == AccelerationStructureType::BottomLevel
            ? "VulkanAccelerationStructure.BLAS.Storage"
            : "VulkanAccelerationStructure.TLAS.Storage";
        m_storageBuffer = MakeShared<VulkanBuffer>(
            m_device,
            storageDesc,
            vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR);

        vk::AccelerationStructureCreateInfoKHR createInfo{};
        createInfo.buffer = m_storageBuffer->GetVkBuffer();
        createInfo.size = m_size;
        createInfo.type = buildInfo.type;
        auto createResult = m_device->GetVkDevice().createAccelerationStructureKHR(createInfo);
        if (createResult.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("Vulkan加速構造リソースを作成できませんでした");
        }
        m_accelerationStructure = createResult.value;

        vk::AccelerationStructureDeviceAddressInfoKHR addressInfo{};
        addressInfo.accelerationStructure = m_accelerationStructure;
        m_deviceAddress = m_device->GetVkDevice().getAccelerationStructureAddressKHR(addressInfo);
        if (m_deviceAddress == 0)
        {
            m_device->GetVkDevice().destroyAccelerationStructureKHR(m_accelerationStructure);
            m_accelerationStructure = nullptr;
            throw std::runtime_error("Vulkan加速構造のdevice addressを取得できませんでした");
        }
    }

    VulkanAccelerationStructure::~VulkanAccelerationStructure()
    {
        if (m_device && m_accelerationStructure)
        {
            m_device->GetVkDevice().destroyAccelerationStructureKHR(m_accelerationStructure);
        }
    }

    bool VulkanAccelerationStructure::Build(const AccelerationStructureBuildDesc& desc)
    {
        if (desc.destination.get() != this || desc.mode != AccelerationStructureBuildMode::Build ||
            !IsValidAccelerationStructureBuildDesc(desc) || desc.type != m_desc.type)
        {
            return false;
        }

        try
        {
            VariableArray<vk::AccelerationStructureGeometryKHR> geometries;
            VariableArray<vk::AccelerationStructureBuildRangeInfoKHR> buildRanges;
            TSharedPtr<VulkanBuffer> instanceBuffer;
            uint32_t builtInstanceCount = 0;
            if (desc.type == AccelerationStructureType::BottomLevel)
            {
                for (const AccelerationStructureGeometryDesc& geometryDesc : desc.geometries)
                {
                    vk::AccelerationStructureGeometryKHR geometry{};
                    if (geometryDesc.opaque)
                    {
                        geometry.flags = vk::GeometryFlagBitsKHR::eOpaque;
                    }
                    vk::AccelerationStructureBuildRangeInfoKHR buildRange{};
                    if (geometryDesc.type == AccelerationStructureGeometryType::Triangles)
                    {
                        const AccelerationStructureTriangleGeometryDesc& triangles = geometryDesc.triangles;
                        TSharedPtr<VulkanBuffer> vertexBuffer = DynamicPointerCast<VulkanBuffer>(triangles.vertexBuffer);
                        TSharedPtr<VulkanBuffer> indexBuffer = DynamicPointerCast<VulkanBuffer>(triangles.indexBuffer);
                        if (!vertexBuffer || vertexBuffer->m_device.get() != m_device.get() ||
                            (triangles.indexBuffer &&
                             (!indexBuffer || indexBuffer->m_device.get() != m_device.get())))
                        {
                            return false;
                        }

                        vk::AccelerationStructureGeometryTrianglesDataKHR triangleData{};
                        triangleData.vertexFormat = triangles.vertexFormat == Format::R32G32B32_FLOAT
                            ? vk::Format::eR32G32B32Sfloat
                            : vk::Format::eR32G32B32A32Sfloat;
                        triangleData.vertexData.deviceAddress =
                            vertexBuffer->GetDeviceAddress() + triangles.vertexOffset;
                        triangleData.vertexStride = triangles.vertexStride;
                        triangleData.maxVertex = triangles.vertexCount - 1u;
                        triangleData.indexType = vk::IndexType::eNoneKHR;
                        if (indexBuffer)
                        {
                            triangleData.indexType = triangles.indexFormat == IndexType::Uint16
                                ? vk::IndexType::eUint16
                                : vk::IndexType::eUint32;
                            triangleData.indexData.deviceAddress =
                                indexBuffer->GetDeviceAddress() + triangles.indexOffset;
                        }
                        geometry.geometryType = vk::GeometryTypeKHR::eTriangles;
                        geometry.geometry.triangles = triangleData;
                        buildRange.primitiveCount = indexBuffer
                            ? triangles.indexCount / 3u
                            : triangles.vertexCount / 3u;
                    }
                    else
                    {
                        const AccelerationStructureAabbGeometryDesc& aabbs = geometryDesc.aabbs;
                        TSharedPtr<VulkanBuffer> inputBuffer = DynamicPointerCast<VulkanBuffer>(aabbs.buffer);
                        if (!inputBuffer || inputBuffer->m_device.get() != m_device.get())
                        {
                            return false;
                        }
                        vk::AccelerationStructureGeometryAabbsDataKHR aabbData{};
                        aabbData.data.deviceAddress = inputBuffer->GetDeviceAddress() + aabbs.offset;
                        aabbData.stride = aabbs.stride;
                        geometry.geometryType = vk::GeometryTypeKHR::eAabbs;
                        geometry.geometry.aabbs = aabbData;
                        buildRange.primitiveCount = aabbs.primitiveCount;
                    }
                    geometries.push_back(geometry);
                    buildRanges.push_back(buildRange);
                }
            }
            else
            {
                VariableArray<vk::AccelerationStructureInstanceKHR> instances;
                for (const AccelerationStructureInstanceDesc& instanceDesc : desc.instances)
                {
                    TSharedPtr<VulkanAccelerationStructure> bottomLevel =
                        DynamicPointerCast<VulkanAccelerationStructure>(instanceDesc.bottomLevel);
                    if (!bottomLevel || bottomLevel->m_device.get() != m_device.get() ||
                        bottomLevel->GetDesc().type != AccelerationStructureType::BottomLevel)
                    {
                        return false;
                    }

                    vk::AccelerationStructureInstanceKHR instance{};
                    for (uint32_t row = 0; row < 3; ++row)
                    {
                        for (uint32_t column = 0; column < 4; ++column)
                        {
                            instance.transform.matrix[row][column] = instanceDesc.transform[row * 4u + column];
                        }
                    }
                    instance.instanceCustomIndex = instanceDesc.customIndex;
                    instance.mask = instanceDesc.mask;
                    instance.instanceShaderBindingTableRecordOffset = instanceDesc.shaderBindingTableRecordOffset;
                    if (instanceDesc.disableTriangleFacingCull)
                    {
                        instance.flags = static_cast<VkGeometryInstanceFlagsKHR>(
                            vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable);
                    }
                    instance.accelerationStructureReference = bottomLevel->GetDeviceAddress();
                    instances.push_back(instance);
                }

                BufferDesc instanceBufferDesc;
                instanceBufferDesc.Size = static_cast<uint64_t>(instances.size()) * sizeof(vk::AccelerationStructureInstanceKHR);
                instanceBufferDesc.Usage = ResourceUsage::StorageBuffer | ResourceUsage::BufferDeviceAddress;
                instanceBufferDesc.CPUAccessible = true;
                instanceBufferDesc.DebugName = "VulkanAccelerationStructure.Build.Instances";
                instanceBuffer = MakeShared<VulkanBuffer>(
                    m_device,
                    instanceBufferDesc,
                    vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR);
                instanceBuffer->Update(instances.data(), instanceBufferDesc.Size);

                vk::AccelerationStructureGeometryInstancesDataKHR instancesData{};
                instancesData.arrayOfPointers = VK_FALSE;
                instancesData.data.deviceAddress = instanceBuffer->GetDeviceAddress();
                vk::AccelerationStructureGeometryKHR instancesGeometry{};
                instancesGeometry.geometryType = vk::GeometryTypeKHR::eInstances;
                instancesGeometry.geometry.instances = instancesData;
                geometries.push_back(instancesGeometry);

                vk::AccelerationStructureBuildRangeInfoKHR buildRange{};
                buildRange.primitiveCount = static_cast<uint32_t>(instances.size());
                builtInstanceCount = buildRange.primitiveCount;
                buildRanges.push_back(buildRange);
            }

            vk::PhysicalDeviceAccelerationStructurePropertiesKHR accelerationStructureProperties{};
            vk::PhysicalDeviceProperties2 properties{};
            properties.pNext = &accelerationStructureProperties;
            m_device->GetVkPhysicalDevice().getProperties2(&properties);
            const uint64_t scratchAlignment = std::max<uint64_t>(
                accelerationStructureProperties.minAccelerationStructureScratchOffsetAlignment,
                1u);

            BufferDesc scratchDesc;
            scratchDesc.Size = m_buildScratchSize + scratchAlignment - 1u;
            scratchDesc.Usage = ResourceUsage::StorageBuffer | ResourceUsage::BufferDeviceAddress;
            scratchDesc.DebugName = "VulkanAccelerationStructure.Build.Scratch";
            TSharedPtr<VulkanBuffer> scratchBuffer = MakeShared<VulkanBuffer>(m_device, scratchDesc);
            const uint64_t scratchAddress = scratchBuffer->GetDeviceAddress();
            const uint64_t scratchRemainder = scratchAddress % scratchAlignment;
            const uint64_t alignedScratchAddress = scratchAddress +
                (scratchRemainder == 0 ? 0 : scratchAlignment - scratchRemainder);

            vk::AccelerationStructureBuildGeometryInfoKHR buildInfo{};
            buildInfo.type = desc.type == AccelerationStructureType::BottomLevel
                ? vk::AccelerationStructureTypeKHR::eBottomLevel
                : vk::AccelerationStructureTypeKHR::eTopLevel;
            buildInfo.flags = ToVkAccelerationStructureBuildFlags(m_desc);
            buildInfo.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
            buildInfo.dstAccelerationStructure = m_accelerationStructure;
            buildInfo.geometryCount = static_cast<uint32_t>(geometries.size());
            buildInfo.pGeometries = geometries.data();
            buildInfo.scratchData.deviceAddress = alignedScratchAddress;

            const vk::AccelerationStructureBuildRangeInfoKHR* buildRangeInfos = buildRanges.data();
            const vk::AccelerationStructureBuildRangeInfoKHR* buildRangeInfoArrays[] = {buildRangeInfos};
            vk::CommandBuffer commandBuffer = m_device->BeginSingleTimeCommands();
            commandBuffer.buildAccelerationStructuresKHR(1, &buildInfo, buildRangeInfoArrays);

            vk::MemoryBarrier accelerationStructureBarrier{};
            accelerationStructureBarrier.srcAccessMask = vk::AccessFlagBits::eAccelerationStructureWriteKHR;
            accelerationStructureBarrier.dstAccessMask = vk::AccessFlagBits::eAccelerationStructureReadKHR;
            commandBuffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
                vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR | vk::PipelineStageFlagBits::eComputeShader,
                {},
                1,
                &accelerationStructureBarrier,
                0,
                nullptr,
                0,
                nullptr);
            m_device->EndSingleTimeCommands(commandBuffer);
            if (desc.type == AccelerationStructureType::TopLevel)
            {
                m_lastBuiltInstanceCount = builtInstanceCount;
            }
            return true;
        }
        catch (const std::exception& error)
        {
            NORVES_LOG_ERROR("VulkanAccelerationStructure", "加速構造の構築に失敗しました: %s", error.what());
            return false;
        }
    }

    // バリデーションレイヤー名
    const VariableArray<const char *> validationLayers = {
        "VK_LAYER_KHRONOS_validation"};

    // 必要なデバイス拡張機能（基本セット）
    const VariableArray<const char *> baseDeviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    // ファクトリメソッド
    DevicePtr VulkanDevice::Create(const VulkanInitParams &params)
    {
        return MakeShared<VulkanDevice>(params.bEnableValidation);
    }

    // コンストラクタ
    VulkanDevice::VulkanDevice(bool bEnableValidation)
        : m_bValidationEnabled(bEnableValidation)
    {
        // Dynamic dispatcherの初期化（Vulkan SDKからvkGetInstanceProcAddrを直接使用）
        VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

        CreateInstance();

        if (m_bValidationEnabled)
        {
            SetupDebugMessenger();
        }
        PickPhysicalDevice();
        CreateLogicalDevice();
#if defined(VK_EXT_device_address_binding_report)
        if (m_bValidationEnabled &&
            m_addressBindingReportFeatures.reportAddressBinding == VK_TRUE)
        {
            SetupAddressBindingDebugMessenger();
            if (m_addressBindingDebugMessenger)
            {
                InitializeAddressBindingDiagnostics();
            }
        }
#endif
        m_ResourceAllocator = MakeUnique<VulkanGPUResourceAllocator>(this);
        CreateCommandPool();
        InitFormatMaps();
        DetectCapabilities();
    }

    // デストラクタ
    VulkanDevice::~VulkanDevice()
    {
        if (m_device)
        {
            WaitIdle();
        }

        m_ResourceAllocator.reset();

        // コマンドプールを破棄
        if (m_commandPool)
        {
            m_device.destroyCommandPool(m_commandPool);
        }

        // デバイスを破棄
        if (m_device)
        {
            m_device.destroy();
        }

#if defined(VK_EXT_device_address_binding_report)
        ShutdownAddressBindingDiagnostics();
        if (m_addressBindingDebugMessenger)
        {
            m_instance.destroyDebugUtilsMessengerEXT(m_addressBindingDebugMessenger);
        }
#endif

        // デバッグメッセンジャーを破棄
        if (m_debugMessenger)
        {
            m_instance.destroyDebugUtilsMessengerEXT(m_debugMessenger);
        }

        // インスタンスを破棄
        if (m_instance)
        {
            m_instance.destroy();
        }
    }

    // Vulkanインスタンス作成
    void VulkanDevice::CreateInstance()
    {
        // バリデーションレイヤーのチェック
        if (m_bValidationEnabled && !CheckValidationLayerSupport())
        {
            throw std::runtime_error("バリデーションレイヤーが利用できません");
        }

        // アプリケーション情報
        vk::ApplicationInfo appInfo{};
        appInfo.pApplicationName = "NorvesLib Application";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "NorvesLib Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_2;

        // 各サブシステムへ供給するため、インスタンスの apiVersion を保持する
        // （appInfo.apiVersion と単一ソースに揃える）。
        m_instanceApiVersion = appInfo.apiVersion;

        // インスタンス作成情報
        auto extensions = GetRequiredExtensions();

        vk::InstanceCreateInfo createInfo{};
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        // デバッグメッセンジャー情報
        vk::DebugUtilsMessengerCreateInfoEXT debugCreateInfo{};

        if (m_bValidationEnabled)
        {
            createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
            createInfo.ppEnabledLayerNames = validationLayers.data();

            // デバッグメッセンジャー情報
            debugCreateInfo.messageSeverity =
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
            debugCreateInfo.messageType =
                vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
            debugCreateInfo.pfnUserCallback = reinterpret_cast<vk::PFN_DebugUtilsMessengerCallbackEXT>(DebugCallback);

            createInfo.pNext = &debugCreateInfo;
        }
        else
        {
            createInfo.enabledLayerCount = 0;
            createInfo.pNext = nullptr;
        }

        // インスタンス作成
        auto result = vk::createInstance(createInfo);
        if (result.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("Vulkanインスタンスの作成に失敗しました");
        }
        m_instance = result.value;

        // インスタンスレベルの関数ポインタを初期化
        VULKAN_HPP_DEFAULT_DISPATCHER.init(m_instance);
    }

    // デバッグメッセンジャーのセットアップ
    void VulkanDevice::SetupDebugMessenger()
    {
        vk::DebugUtilsMessengerCreateInfoEXT createInfo{};
        createInfo.messageSeverity =
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
        createInfo.messageType =
            vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
            vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
            vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
        createInfo.pfnUserCallback = reinterpret_cast<vk::PFN_DebugUtilsMessengerCallbackEXT>(DebugCallback);
        createInfo.pUserData = this;

        auto result = m_instance.createDebugUtilsMessengerEXT(createInfo);
        if (result.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("デバッグメッセンジャーの設定に失敗しました");
        }
        m_debugMessenger = result.value;
    }

#if defined(VK_EXT_device_address_binding_report)
    void VulkanDevice::SetupAddressBindingDebugMessenger()
    {
        vk::DebugUtilsMessengerCreateInfoEXT createInfo{};
        createInfo.messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo;
        createInfo.messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eDeviceAddressBinding;
        createInfo.pfnUserCallback = reinterpret_cast<vk::PFN_DebugUtilsMessengerCallbackEXT>(DebugCallback);
        createInfo.pUserData = this;

        auto result = m_instance.createDebugUtilsMessengerEXT(createInfo);
        if (result.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("アドレスバインディングデバッグメッセンジャーの設定に失敗しました");
        }
        m_addressBindingDebugMessenger = result.value;
    }
#endif

    // 物理デバイスの選択
    void VulkanDevice::PickPhysicalDevice()
    {
        // デバイス一覧取得
        auto devicesResult = m_instance.enumeratePhysicalDevices();
        if (devicesResult.result != vk::Result::eSuccess || devicesResult.value.empty())
        {
            throw std::runtime_error("Vulkanをサポートするデバイスが見つかりません");
        }

        auto devices = devicesResult.value;

        // 適切なデバイスを探す
        for (const auto &device : devices)
        {
            if (IsDeviceSuitable(device))
            {
                m_physicalDevice = device;

                // デバイス情報を取得
                m_deviceProperties = m_physicalDevice.getProperties();
                m_deviceFeatures = m_physicalDevice.getFeatures();
                m_memoryProperties = m_physicalDevice.getMemoryProperties();

                // キューファミリーを取得
                FindQueueFamilies(m_physicalDevice);
                break;
            }
        }

        if (!m_physicalDevice)
        {
            throw std::runtime_error("適切なGPUデバイスが見つかりません");
        }
    }

    // 論理デバイスの作成
    void VulkanDevice::CreateLogicalDevice()
    {
        // 重複のないキューファミリインデックスのセット
        Set<uint32_t> uniqueQueueFamilies;
        uniqueQueueFamilies.insert(m_graphicsQueueFamilyIndex);
        uniqueQueueFamilies.insert(m_presentQueueFamilyIndex);
        uniqueQueueFamilies.insert(m_computeQueueFamilyIndex);

        // 転送キューを追加
        if (m_transferQueueFamilyIndex != UINT32_MAX)
        {
            uniqueQueueFamilies.insert(m_transferQueueFamilyIndex);
        }

        // キュー作成情報
        float queuePriority = 1.0f;
        VariableArray<vk::DeviceQueueCreateInfo> queueCreateInfos;

        for (uint32_t queueFamily : uniqueQueueFamilies)
        {
            vk::DeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.queueFamilyIndex = queueFamily;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueCreateInfo);
        }

        // デバイス機能の設定（Features2チェーンを使用）
        const vk::PhysicalDeviceFeatures physicalFeatures = m_physicalDevice.getFeatures();
        vk::PhysicalDeviceFeatures2 features2{};
        features2.features.samplerAnisotropy = VK_TRUE;
        features2.features.fillModeNonSolid = VK_TRUE; // ワイヤーフレームなどのサポート
        features2.features.drawIndirectFirstInstance =
            physicalFeatures.drawIndirectFirstInstance == VK_TRUE ? VK_TRUE : VK_FALSE;
        m_enabledDeviceFeatures = features2.features;

        // Vulkan 1.2 機能: 対応している場合のみ drawIndirectCount を有効化
        vk::PhysicalDeviceVulkan12Features vulkan12Query{};
        vk::PhysicalDeviceFeatures2 features2Query{};
        features2Query.pNext = &vulkan12Query;
        m_physicalDevice.getFeatures2(&features2Query);

        m_vulkan12Features = vk::PhysicalDeviceVulkan12Features{};
        m_vulkan12Features.drawIndirectCount =
            vulkan12Query.drawIndirectCount == VK_TRUE ? VK_TRUE : VK_FALSE;
        m_vulkan12Features.bufferDeviceAddress =
            vulkan12Query.bufferDeviceAddress == VK_TRUE ? VK_TRUE : VK_FALSE;
        features2.pNext = &m_vulkan12Features;

        // 任意のデバイス拡張は機能照会より先に選定する。
        auto extensions = GetDeviceExtensions();

        vk::PhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeaturesQuery{};
        vk::PhysicalDeviceRayQueryFeaturesKHR rayQueryFeaturesQuery{};
        vk::PhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeaturesQuery{};
        const auto availableDeviceExtensionsResult = m_physicalDevice.enumerateDeviceExtensionProperties();
        // Vulkan 1.2のBDAとdeferred host operationsが揃う場合だけRT拡張を照会する。
        bool bAccelerationStructureExtensionAvailable = false;
        bool bDeferredHostOperationsExtensionAvailable = false;
        bool bRayQueryExtensionAvailable = false;
        bool bRayTracingPipelineExtensionAvailable = false;
        if (availableDeviceExtensionsResult.result == vk::Result::eSuccess &&
            m_deviceProperties.apiVersion >= VK_API_VERSION_1_2)
        {
            const auto& availableDeviceExtensions = availableDeviceExtensionsResult.value;
            auto hasDeviceExtension = [&availableDeviceExtensions](const char* name) -> bool
            {
                for (const auto& extension : availableDeviceExtensions)
                {
                    if (String(extension.extensionName.data()) == String(name))
                    {
                        return true;
                    }
                }
                return false;
            };

            bAccelerationStructureExtensionAvailable =
                hasDeviceExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
            bDeferredHostOperationsExtensionAvailable =
                hasDeviceExtension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
            const bool bAccelerationStructureDependencyAvailable =
                bAccelerationStructureExtensionAvailable &&
                bDeferredHostOperationsExtensionAvailable;

            if (bAccelerationStructureDependencyAvailable)
            {
                vk::PhysicalDeviceFeatures2 accelerationStructureFeatures2Query{};
                accelerationStructureFeatures2Query.pNext = &accelerationStructureFeaturesQuery;
                m_physicalDevice.getFeatures2(&accelerationStructureFeatures2Query);
            }

            bRayQueryExtensionAvailable =
                bAccelerationStructureDependencyAvailable &&
                hasDeviceExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME);
            if (bRayQueryExtensionAvailable)
            {
                vk::PhysicalDeviceFeatures2 rayQueryFeatures2Query{};
                rayQueryFeatures2Query.pNext = &rayQueryFeaturesQuery;
                m_physicalDevice.getFeatures2(&rayQueryFeatures2Query);
            }

            bRayTracingPipelineExtensionAvailable =
                bAccelerationStructureDependencyAvailable &&
                hasDeviceExtension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
            if (bRayTracingPipelineExtensionAvailable)
            {
                vk::PhysicalDeviceFeatures2 rayTracingPipelineFeatures2Query{};
                rayTracingPipelineFeatures2Query.pNext = &rayTracingPipelineFeaturesQuery;
                m_physicalDevice.getFeatures2(&rayTracingPipelineFeatures2Query);
            }
        }

        RayTracingFeatureAvailability rayTracingAvailability;
        rayTracingAvailability.bAccelerationStructureExtension =
            bAccelerationStructureExtensionAvailable;
        rayTracingAvailability.bDeferredHostOperationsExtension =
            bDeferredHostOperationsExtensionAvailable;
        rayTracingAvailability.bBufferDeviceAddress =
            m_vulkan12Features.bufferDeviceAddress == VK_TRUE;
        rayTracingAvailability.bAccelerationStructureFeature =
            accelerationStructureFeaturesQuery.accelerationStructure == VK_TRUE;
        rayTracingAvailability.bRayQueryExtension = bRayQueryExtensionAvailable;
        rayTracingAvailability.bRayQueryFeature = rayQueryFeaturesQuery.rayQuery == VK_TRUE;
        rayTracingAvailability.bRayTracingPipelineExtension =
            bRayTracingPipelineExtensionAvailable;
        rayTracingAvailability.bRayTracingPipelineFeature =
            rayTracingPipelineFeaturesQuery.rayTracingPipeline == VK_TRUE;

        const RayTracingCapabilities rayTracingCapabilities =
            ResolveRayTracingCapabilities(rayTracingAvailability);
        m_accelerationStructureFeatures = vk::PhysicalDeviceAccelerationStructureFeaturesKHR{};
        m_accelerationStructureFeatures.accelerationStructure =
            rayTracingCapabilities.bAccelerationStructure ? VK_TRUE : VK_FALSE;
        m_rayQueryFeatures = vk::PhysicalDeviceRayQueryFeaturesKHR{};
        m_rayQueryFeatures.rayQuery = rayTracingCapabilities.bRayQuery ? VK_TRUE : VK_FALSE;
        m_rayTracingPipelineFeatures = vk::PhysicalDeviceRayTracingPipelineFeaturesKHR{};
        m_rayTracingPipelineFeatures.rayTracingPipeline =
            rayTracingCapabilities.bRayTracingPipeline ? VK_TRUE : VK_FALSE;

        if (rayTracingCapabilities.bAccelerationStructure)
        {
            extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
            extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        }
        if (rayTracingCapabilities.bRayQuery)
        {
            extensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
        }
        if (rayTracingCapabilities.bRayTracingPipeline)
        {
            extensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
        }

        bool bDeviceFaultRequested = false;
        bool bCooperativeVectorRequested = false;
#if defined(VK_EXT_device_address_binding_report)
        bool bAddressBindingReportRequested = false;
#endif
        for (const auto *ext : extensions)
        {
            if (String(ext) == String(VK_EXT_DEVICE_FAULT_EXTENSION_NAME))
            {
                bDeviceFaultRequested = true;
            }

            if (String(ext) == String(VK_NV_COOPERATIVE_VECTOR_EXTENSION_NAME))
            {
                bCooperativeVectorRequested = true;
            }

#if defined(VK_EXT_device_address_binding_report)
            if (String(ext) == String(VK_EXT_DEVICE_ADDRESS_BINDING_REPORT_EXTENSION_NAME))
            {
                bAddressBindingReportRequested = true;
            }
#endif
        }

        if (bDeviceFaultRequested)
        {
            VkPhysicalDeviceFaultFeaturesEXT deviceFaultFeaturesQuery{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT,
                nullptr,
                VK_FALSE,
                VK_FALSE};
            vk::PhysicalDeviceFeatures2 deviceFaultFeatures2Query{};
            deviceFaultFeatures2Query.pNext = &deviceFaultFeaturesQuery;
            m_physicalDevice.getFeatures2(&deviceFaultFeatures2Query);

            m_deviceFaultFeatures.deviceFault =
                deviceFaultFeaturesQuery.deviceFault == VK_TRUE ? VK_TRUE : VK_FALSE;
            m_deviceFaultFeatures.deviceFaultVendorBinary =
                m_deviceFaultFeatures.deviceFault == VK_TRUE &&
                deviceFaultFeaturesQuery.deviceFaultVendorBinary == VK_TRUE ? VK_TRUE : VK_FALSE;
        }

#if defined(VK_EXT_device_address_binding_report)
        m_addressBindingReportFeatures = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ADDRESS_BINDING_REPORT_FEATURES_EXT,
            nullptr,
            VK_FALSE};
        if (bAddressBindingReportRequested)
        {
            VkPhysicalDeviceAddressBindingReportFeaturesEXT addressBindingReportFeaturesQuery{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ADDRESS_BINDING_REPORT_FEATURES_EXT,
                nullptr,
                VK_FALSE};
            vk::PhysicalDeviceFeatures2 addressBindingReportFeatures2Query{};
            addressBindingReportFeatures2Query.pNext = &addressBindingReportFeaturesQuery;
            m_physicalDevice.getFeatures2(&addressBindingReportFeatures2Query);

            m_addressBindingReportFeatures.reportAddressBinding =
                addressBindingReportFeaturesQuery.reportAddressBinding == VK_TRUE ? VK_TRUE : VK_FALSE;
        }
#endif

        void **featuresTail = &m_vulkan12Features.pNext;
        if (m_deviceFaultFeatures.deviceFault == VK_TRUE)
        {
            *featuresTail = &m_deviceFaultFeatures;
            featuresTail = &m_deviceFaultFeatures.pNext;
        }

        if (bCooperativeVectorRequested)
        {
            m_cooperativeVectorFeatures = vk::PhysicalDeviceCooperativeVectorFeaturesNV{};
            m_cooperativeVectorFeatures.cooperativeVector = VK_TRUE;
            m_cooperativeVectorFeatures.cooperativeVectorTraining = VK_FALSE;

            *featuresTail = &m_cooperativeVectorFeatures;
            featuresTail = &m_cooperativeVectorFeatures.pNext;
        }

        if (rayTracingCapabilities.bAccelerationStructure)
        {
            *featuresTail = &m_accelerationStructureFeatures;
            featuresTail = &m_accelerationStructureFeatures.pNext;
        }

        if (rayTracingCapabilities.bRayQuery)
        {
            *featuresTail = &m_rayQueryFeatures;
            featuresTail = &m_rayQueryFeatures.pNext;
        }

        if (rayTracingCapabilities.bRayTracingPipeline)
        {
            *featuresTail = &m_rayTracingPipelineFeatures;
            featuresTail = &m_rayTracingPipelineFeatures.pNext;
        }

#if defined(VK_EXT_device_address_binding_report)
        if (m_addressBindingReportFeatures.reportAddressBinding == VK_TRUE)
        {
            *featuresTail = &m_addressBindingReportFeatures;
            featuresTail = &m_addressBindingReportFeatures.pNext;
        }
#endif
        *featuresTail = nullptr;

        // デバイス作成情報
        vk::DeviceCreateInfo createInfo{};
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.pEnabledFeatures = nullptr; // Features2使用時はnullptr
        createInfo.pNext = &features2;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        // バリデーションレイヤー
        if (m_bValidationEnabled)
        {
            createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
            createInfo.ppEnabledLayerNames = validationLayers.data();
        }
        else
        {
            createInfo.enabledLayerCount = 0;
        }

        // デバイス作成
        auto result = m_physicalDevice.createDevice(createInfo);
        if (result.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("Vulkan論理デバイスの作成に失敗しました");
        }
        m_device = result.value;

        // デバイスレベルの関数ポインタを初期化
        VULKAN_HPP_DEFAULT_DISPATCHER.init(m_device);

        // キューハンドルを取得
        m_graphicsQueue = m_device.getQueue(m_graphicsQueueFamilyIndex, 0);
        m_presentQueue = m_device.getQueue(m_presentQueueFamilyIndex, 0);
        m_computeQueue = m_device.getQueue(m_computeQueueFamilyIndex, 0);

        if (m_transferQueueFamilyIndex != UINT32_MAX)
        {
            m_transferQueue = m_device.getQueue(m_transferQueueFamilyIndex, 0);
        }
        else
        {
            m_transferQueue = m_graphicsQueue;
        }
    }

    // コマンドプールの作成
    void VulkanDevice::CreateCommandPool()
    {
        vk::CommandPoolCreateInfo poolInfo{};
        poolInfo.queueFamilyIndex = m_graphicsQueueFamilyIndex;
        poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;

        auto result = m_device.createCommandPool(poolInfo);
        if (result.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("コマンドプールの作成に失敗しました");
        }
        m_commandPool = result.value;
    }

    // フォーマット変換マップの初期化
    void VulkanDevice::InitFormatMaps()
    {
        // RHI Format → vk::Format
        m_formatMap[Format::R8_UNORM] = vk::Format::eR8Unorm;
        m_formatMap[Format::R8G8_UNORM] = vk::Format::eR8G8Unorm;
        m_formatMap[Format::R8G8B8A8_UNORM] = vk::Format::eR8G8B8A8Unorm;
        m_formatMap[Format::R8G8B8A8_SRGB] = vk::Format::eR8G8B8A8Srgb;
        m_formatMap[Format::B8G8R8A8_UNORM] = vk::Format::eB8G8R8A8Unorm;
        m_formatMap[Format::B8G8R8A8_SRGB] = vk::Format::eB8G8R8A8Srgb;
        m_formatMap[Format::R16_FLOAT] = vk::Format::eR16Sfloat;
        m_formatMap[Format::R16G16_FLOAT] = vk::Format::eR16G16Sfloat;
        m_formatMap[Format::R16G16B16A16_FLOAT] = vk::Format::eR16G16B16A16Sfloat;
        m_formatMap[Format::R32_FLOAT] = vk::Format::eR32Sfloat;
        m_formatMap[Format::R32G32_FLOAT] = vk::Format::eR32G32Sfloat;
        m_formatMap[Format::R32G32B32_FLOAT] = vk::Format::eR32G32B32Sfloat;
        m_formatMap[Format::R32G32B32A32_FLOAT] = vk::Format::eR32G32B32A32Sfloat;
        m_formatMap[Format::D16_UNORM] = vk::Format::eD16Unorm;
        m_formatMap[Format::D24_UNORM_S8_UINT] = vk::Format::eD24UnormS8Uint;
        m_formatMap[Format::D32_FLOAT] = vk::Format::eD32Sfloat;

        // vk::Format → RHI Format (逆変換マップも作成)
        for (const auto &[rhiFormat, vkFormat] : m_formatMap)
        {
            m_reverseFormatMap[vkFormat] = rhiFormat;
        }
    }

    // バリデーションレイヤーのサポート確認
    bool VulkanDevice::CheckValidationLayerSupport()
    {
        // レイヤー一覧の取得
        auto layersResult = vk::enumerateInstanceLayerProperties();
        if (layersResult.result != vk::Result::eSuccess)
        {
            return false;
        }

        auto availableLayers = layersResult.value;

        // 必要なレイヤーが全て存在するか確認
        for (const char *layerName : validationLayers)
        {
            bool bLayerFound = false;

            for (const auto &layerProperties : availableLayers)
            {
                if (strcmp(layerName, layerProperties.layerName) == 0)
                {
                    bLayerFound = true;
                    break;
                }
            }

            if (!bLayerFound)
            {
                return false;
            }
        }

        return true;
    }

    // デバイスが適切かどうかの判定
    bool VulkanDevice::IsDeviceSuitable(vk::PhysicalDevice device)
    {
        // 物理デバイスのプロパティと機能
        auto deviceProperties = device.getProperties();
        auto deviceFeatures = device.getFeatures();

        // キューファミリーのサポートチェック
        FindQueueFamilies(device);
        bool bHasRequiredQueueFamilies =
            m_graphicsQueueFamilyIndex != UINT32_MAX &&
            m_computeQueueFamilyIndex != UINT32_MAX;

        // 拡張機能のサポートチェック
        auto extensionsResult = device.enumerateDeviceExtensionProperties();
        if (extensionsResult.result != vk::Result::eSuccess)
        {
            return false;
        }

        auto availableExtensions = extensionsResult.value;

        Set<String> requiredExtensions;
        for (const auto &ext : baseDeviceExtensions)
        {
            requiredExtensions.insert(String(ext));
        }

        for (const auto &extension : availableExtensions)
        {
            requiredExtensions.erase(String(extension.extensionName.data()));
        }

        bool bExtensionsSupported = requiredExtensions.empty();

        // 物理デバイスの選定
        bool bIsDiscrete = deviceProperties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu;
        bool bHasAnisotropySupport = deviceFeatures.samplerAnisotropy;

        return bHasRequiredQueueFamilies &&
               bExtensionsSupported &&
               bHasAnisotropySupport &&
               bIsDiscrete; // 離散GPUを優先
    }

    // 必要なインスタンス拡張機能を取得
    VariableArray<const char *> VulkanDevice::GetRequiredExtensions()
    {
        VariableArray<const char *> extensions;

        // ウィンドウシステム連携のための拡張機能
        extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);

        // プラットフォーム固有の拡張機能
#ifdef _WIN32
        extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(__linux__)
        extensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
#endif

        // バリデーション関連の拡張機能
        if (m_bValidationEnabled)
        {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        return extensions;
    }

    // 必要なデバイス拡張機能を取得
    VariableArray<const char *> VulkanDevice::GetDeviceExtensions()
    {
        VariableArray<const char *> extensions = baseDeviceExtensions;

        if (!m_physicalDevice)
        {
            return extensions;
        }

        // 利用可能な拡張を列挙
        auto extensionsResult = m_physicalDevice.enumerateDeviceExtensionProperties();
        if (extensionsResult.result != vk::Result::eSuccess)
        {
            return extensions;
        }

        auto available = extensionsResult.value;
        auto hasExtension = [&available](const char *name) -> bool
        {
            for (const auto &ext : available)
            {
                if (String(ext.extensionName.data()) == String(name))
                {
                    return true;
                }
            }
            return false;
        };

        // VK_NV_cooperative_vector（Neural Shaders）
        if (hasExtension(VK_NV_COOPERATIVE_VECTOR_EXTENSION_NAME))
        {
            extensions.push_back(VK_NV_COOPERATIVE_VECTOR_EXTENSION_NAME);
            NORVES_LOG_INFO("VulkanDevice", "Optional extension enabled: VK_NV_cooperative_vector");
        }

        if (hasExtension(VK_EXT_DEVICE_FAULT_EXTENSION_NAME))
        {
            extensions.push_back(VK_EXT_DEVICE_FAULT_EXTENSION_NAME);
            NORVES_LOG_INFO("VulkanDevice", "Optional extension enabled: VK_EXT_device_fault");
        }

#if defined(VK_EXT_device_address_binding_report)
        if (m_bValidationEnabled && hasExtension(VK_EXT_DEVICE_ADDRESS_BINDING_REPORT_EXTENSION_NAME))
        {
            extensions.push_back(VK_EXT_DEVICE_ADDRESS_BINDING_REPORT_EXTENSION_NAME);
            NORVES_LOG_INFO("VulkanDevice", "Optional extension enabled: VK_EXT_device_address_binding_report");
        }
#endif

        return extensions;
    }

    // キューファミリーのインデックス取得
    void VulkanDevice::FindQueueFamilies(vk::PhysicalDevice device)
    {
        // キューファミリーのプロパティ取得
        auto queueFamilies = device.getQueueFamilyProperties();

        // グラフィックスキューファミリーを探す
        for (uint32_t i = 0; i < queueFamilies.size(); i++)
        {
            const auto &queueFamily = queueFamilies[i];

            // グラフィックスキュー
            if (queueFamily.queueFlags & vk::QueueFlagBits::eGraphics)
            {
                m_graphicsQueueFamilyIndex = i;
                m_presentQueueFamilyIndex = i; // 通常はグラフィックスキューでプレゼントも可能
            }

            // コンピュートキュー（可能ならグラフィックスとは別のキューを使用）
            if (queueFamily.queueFlags & vk::QueueFlagBits::eCompute)
            {
                if (m_computeQueueFamilyIndex == UINT32_MAX ||
                    !(queueFamily.queueFlags & vk::QueueFlagBits::eGraphics))
                {
                    m_computeQueueFamilyIndex = i;
                }
            }

            // 転送専用キュー（可能ならグラフィックスとは別のキューを使用）
            if (queueFamily.queueFlags & vk::QueueFlagBits::eTransfer)
            {
                if (m_transferQueueFamilyIndex == UINT32_MAX ||
                    !(queueFamily.queueFlags & vk::QueueFlagBits::eGraphics))
                {
                    m_transferQueueFamilyIndex = i;
                }
            }
        }

        // コンピュートキューがない場合はグラフィックスキューで代用
        if (m_computeQueueFamilyIndex == UINT32_MAX)
        {
            m_computeQueueFamilyIndex = m_graphicsQueueFamilyIndex;
        }
    }

    // メモリタイプのインデックス検索
    uint32_t VulkanDevice::FindMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const
    {
        for (uint32_t i = 0; i < m_memoryProperties.memoryTypeCount; i++)
        {
            if ((typeFilter & (1 << i)) &&
                (m_memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
            {
                return i;
            }
        }

        throw std::runtime_error("適切なメモリタイプが見つかりません");
    }

    // 単発コマンドバッファ開始
    vk::CommandBuffer VulkanDevice::BeginSingleTimeCommands()
    {
        vk::CommandBufferAllocateInfo allocInfo{};
        allocInfo.level = vk::CommandBufferLevel::ePrimary;
        allocInfo.commandPool = m_commandPool;
        allocInfo.commandBufferCount = 1;

        auto allocResult = m_device.allocateCommandBuffers(allocInfo);
        if (allocResult.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("単発コマンドバッファの割り当てに失敗しました");
        }

        vk::CommandBuffer commandBuffer = allocResult.value[0];

        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

        auto beginResult = commandBuffer.begin(beginInfo);
        if (beginResult != vk::Result::eSuccess)
        {
            throw std::runtime_error("コマンドバッファの開始に失敗しました");
        }

        return commandBuffer;
    }

    // 単発コマンドバッファ終了
    void VulkanDevice::EndSingleTimeCommands(vk::CommandBuffer commandBuffer)
    {
        commandBuffer.end();

        vk::SubmitInfo submitInfo{};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        auto submitResult = m_graphicsQueue.submit(1, &submitInfo, nullptr);
        if (submitResult != vk::Result::eSuccess)
        {
            throw std::runtime_error("キューへの送信に失敗しました");
        }

        m_graphicsQueue.waitIdle();

        m_device.freeCommandBuffers(m_commandPool, 1, &commandBuffer);
    }

    // サポートするフォーマットを検索
    vk::Format VulkanDevice::FindSupportedFormat(
        const VariableArray<vk::Format> &candidates,
        vk::ImageTiling tiling,
        vk::FormatFeatureFlags features) const
    {
        for (vk::Format format : candidates)
        {
            auto props = m_physicalDevice.getFormatProperties(format);

            if (tiling == vk::ImageTiling::eLinear && (props.linearTilingFeatures & features) == features)
            {
                return format;
            }
            else if (tiling == vk::ImageTiling::eOptimal && (props.optimalTilingFeatures & features) == features)
            {
                return format;
            }
        }

        throw std::runtime_error("サポートされているフォーマットが見つかりません");
    }

    // RHI Format → vk::Format変換
    vk::Format VulkanDevice::ToVkFormat(Format format) const
    {
        auto it = m_formatMap.find(format);
        if (it != m_formatMap.end())
        {
            return it->second;
        }
        return vk::Format::eUndefined;
    }

    // vk::Format → RHI Format変換
    Format VulkanDevice::FromVkFormat(vk::Format format) const
    {
        auto it = m_reverseFormatMap.find(format);
        if (it != m_reverseFormatMap.end())
        {
            return it->second;
        }
        return Format::UNKNOWN;
    }

#if defined(VK_EXT_device_address_binding_report)
    namespace
    {
        const char *GetAddressBindingTypeName(VkDeviceAddressBindingTypeEXT bindingType)
        {
            switch (bindingType)
            {
            case VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT:
                return "bind";
            case VK_DEVICE_ADDRESS_BINDING_TYPE_UNBIND_EXT:
                return "unbind";
            default:
                return "unknown";
            }
        }

        const char *GetDebugObjectTypeName(VkObjectType objectType)
        {
            switch (objectType)
            {
            case VK_OBJECT_TYPE_BUFFER:
                return "VK_OBJECT_TYPE_BUFFER";
            case VK_OBJECT_TYPE_IMAGE:
                return "VK_OBJECT_TYPE_IMAGE";
            case VK_OBJECT_TYPE_DEVICE_MEMORY:
                return "VK_OBJECT_TYPE_DEVICE_MEMORY";
            case VK_OBJECT_TYPE_IMAGE_VIEW:
                return "VK_OBJECT_TYPE_IMAGE_VIEW";
            case VK_OBJECT_TYPE_SAMPLER:
                return "VK_OBJECT_TYPE_SAMPLER";
            case VK_OBJECT_TYPE_PIPELINE:
                return "VK_OBJECT_TYPE_PIPELINE";
            case VK_OBJECT_TYPE_DESCRIPTOR_SET:
                return "VK_OBJECT_TYPE_DESCRIPTOR_SET";
            default:
                return "<unknown>";
            }
        }

        String EscapeAddressBindingText(const char *pText)
        {
            if (pText == nullptr || pText[0] == '\0')
            {
                return "<unnamed>";
            }

            StringBuilder builder;
            for (const unsigned char *pCharacter = reinterpret_cast<const unsigned char *>(pText);
                 *pCharacter != '\0';
                 ++pCharacter)
            {
                switch (*pCharacter)
                {
                case '\\':
                    builder.Append("\\\\");
                    break;
                case '\"':
                    builder.Append("\\\"");
                    break;
                case '\r':
                    builder.Append("\\r");
                    break;
                case '\n':
                    builder.Append("\\n");
                    break;
                default:
                    if (*pCharacter < 0x20 || *pCharacter == 0x7F)
                    {
                        builder.AppendFormat("\\x%02X", static_cast<uint32_t>(*pCharacter));
                    }
                    else
                    {
                        builder.Append(static_cast<char>(*pCharacter));
                    }
                    break;
                }
            }
            return builder.ToString();
        }
    }

    void VulkanDevice::InitializeAddressBindingDiagnostics()
    {
#if defined(NORVES_ASSET_DIR)
        String assetDirectory = NORVES_ASSET_DIR;
        constexpr const char *assetsName = "Assets";
        const size_t assetsNameLength = 6;
        if (assetDirectory.size() <= assetsNameLength ||
            assetDirectory.substr(assetDirectory.size() - assetsNameLength) != assetsName ||
            (assetDirectory[assetDirectory.size() - assetsNameLength - 1] != '/' &&
             assetDirectory[assetDirectory.size() - assetsNameLength - 1] != '\\'))
        {
            NORVES_LOG_WARNING("VulkanDevice", "Address binding diagnostics disabled: NORVES_ASSET_DIR must end in Assets");
            return;
        }

        const String repositoryRoot = assetDirectory.substr(0, assetDirectory.size() - assetsNameLength - 1);
        const String diagnosticsDirectory = repositoryRoot + "/build/Diagnostics";
        std::error_code errorCode;
        std::filesystem::create_directories(diagnosticsDirectory.c_str(), errorCode);
        if (errorCode)
        {
            NORVES_LOG_WARNING("VulkanDevice", "Address binding diagnostics disabled: cannot create diagnostics directory");
            return;
        }

        const String diagnosticsPath = diagnosticsDirectory + "/VulkanAddressBindings.log";
        m_addressBindingDiagnosticsSink = FileStream::FileStream::Create(
            diagnosticsPath,
            FileStream::FileMode::Append,
            FileStream::FileAccess::Write);
        if (!m_addressBindingDiagnosticsSink || !m_addressBindingDiagnosticsSink->IsOpen())
        {
            m_addressBindingDiagnosticsSink.reset();
            NORVES_LOG_WARNING("VulkanDevice", "Address binding diagnostics disabled: cannot open diagnostics file");
            return;
        }

        m_addressBindingDiagnosticsSink->WriteString("schema=vulkan_device_address_binding_v1\n");
        m_addressBindingDiagnosticsSink->Flush();
#else
        NORVES_LOG_WARNING("VulkanDevice", "Address binding diagnostics disabled: NORVES_ASSET_DIR is unavailable");
#endif
    }

    void VulkanDevice::ShutdownAddressBindingDiagnostics()
    {
        NorvesLib::Thread::ScopedLock lock(m_addressBindingDiagnosticsMutex);
        if (m_addressBindingDiagnosticsSink && m_addressBindingDiagnosticsSink->IsOpen())
        {
            m_addressBindingDiagnosticsSink->Flush();
            m_addressBindingDiagnosticsSink->Close();
        }
        m_addressBindingDiagnosticsSink.reset();
    }

    const VkDeviceAddressBindingCallbackDataEXT *VulkanDevice::FindAddressBindingCallbackData(
        const VkDebugUtilsMessengerCallbackDataEXT &callbackData)
    {
        const VkBaseInStructure *pNext = reinterpret_cast<const VkBaseInStructure *>(callbackData.pNext);
        while (pNext != nullptr)
        {
            if (pNext->sType == VK_STRUCTURE_TYPE_DEVICE_ADDRESS_BINDING_CALLBACK_DATA_EXT)
            {
                return reinterpret_cast<const VkDeviceAddressBindingCallbackDataEXT *>(pNext);
            }
            pNext = pNext->pNext;
        }
        return nullptr;
    }

    void VulkanDevice::RecordAddressBindingDiagnostic(
        const VkDebugUtilsMessengerCallbackDataEXT &callbackData,
        const VkDeviceAddressBindingCallbackDataEXT &addressBindingData)
    {
        NorvesLib::Thread::ScopedLock lock(m_addressBindingDiagnosticsMutex);
        if (!m_addressBindingDiagnosticsSink || !m_addressBindingDiagnosticsSink->IsOpen())
        {
            return;
        }

        const uint64_t baseAddress = static_cast<uint64_t>(addressBindingData.baseAddress);
        const uint64_t size = static_cast<uint64_t>(addressBindingData.size);
        const uint64_t endAddress = size > std::numeric_limits<uint64_t>::max() - baseAddress
                                        ? std::numeric_limits<uint64_t>::max()
                                        : baseAddress + size;
        StringBuilder line;
        line.AppendFormat(
            "event_id=%llu binding=%s base=0x%llX size=0x%llX end=0x%llX flags=0x%X internal_object=%u object_count=%u",
            static_cast<unsigned long long>(m_addressBindingDiagnosticsSequence++),
            GetAddressBindingTypeName(addressBindingData.bindingType),
            static_cast<unsigned long long>(baseAddress),
            static_cast<unsigned long long>(size),
            static_cast<unsigned long long>(endAddress),
            static_cast<uint32_t>(addressBindingData.flags),
            (addressBindingData.flags & VK_DEVICE_ADDRESS_BINDING_INTERNAL_OBJECT_BIT_EXT) != 0 ? 1U : 0U,
            callbackData.objectCount);

        if (callbackData.objectCount == 0)
        {
            line.Append(" object0_type=0 object0_type_name=\"<none>\" object0_handle=0x0 object0_name=\"<unnamed>\"");
        }
        else if (callbackData.pObjects == nullptr)
        {
            line.Append(" object0_type=0 object0_type_name=\"<unavailable>\" object0_handle=0x0 object0_name=\"<unnamed>\"");
        }
        else
        {
            for (uint32_t objectIndex = 0; objectIndex < callbackData.objectCount; ++objectIndex)
            {
                const VkDebugUtilsObjectNameInfoEXT &object = callbackData.pObjects[objectIndex];
                line.AppendFormat(
                    " object%u_type=%u object%u_type_name=\"%s\" object%u_handle=0x%llX object%u_name=\"%s\"",
                    objectIndex,
                    static_cast<uint32_t>(object.objectType),
                    objectIndex,
                    GetDebugObjectTypeName(object.objectType),
                    objectIndex,
                    static_cast<unsigned long long>(object.objectHandle),
                    objectIndex,
                    EscapeAddressBindingText(object.pObjectName).c_str());
            }
        }

        line.Append("\n");
        m_addressBindingDiagnosticsSink->WriteString(line.ToString());
        m_addressBindingDiagnosticsSink->Flush();
    }

#endif

    void VulkanDevice::SetDebugObjectName(vk::ObjectType objectType, uint64_t objectHandle, const char *pName)
    {
        if (!m_device || objectHandle == 0 || pName == nullptr || pName[0] == '\0')
        {
            return;
        }

        const PFN_vkSetDebugUtilsObjectNameEXT setDebugUtilsObjectName =
            reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
                VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr(
                    static_cast<VkDevice>(m_device),
                    "vkSetDebugUtilsObjectNameEXT"));
        if (setDebugUtilsObjectName == nullptr)
        {
            return;
        }

        const VkDebugUtilsObjectNameInfoEXT nameInfo{
            VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
            nullptr,
            static_cast<VkObjectType>(objectType),
            objectHandle,
            pName};
        (void)setDebugUtilsObjectName(static_cast<VkDevice>(m_device), &nameInfo);
    }

    // デバッグコールバック
    VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDevice::DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
        void *pUserData)
    {
        try
        {
            if (pCallbackData != nullptr &&
                messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            {
                std::cerr << "Vulkanバリデーション: " << pCallbackData->pMessage << std::endl;
            }

#if defined(VK_EXT_device_address_binding_report)
            if (pCallbackData != nullptr && pUserData != nullptr &&
                (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT) != 0)
            {
                const VkDeviceAddressBindingCallbackDataEXT *addressBindingData =
                    FindAddressBindingCallbackData(*pCallbackData);
                if (addressBindingData != nullptr)
                {
                    static_cast<VulkanDevice *>(pUserData)->RecordAddressBindingDiagnostic(
                        *pCallbackData,
                        *addressBindingData);
                }
            }
#else
            (void)messageType;
            (void)pUserData;
#endif
        }
        catch (...)
        {
        }

        return VK_FALSE;
    }

#include "VulkanBuffer.h"
#include "VulkanCommandList.h"
#include "VulkanTexture.h"
#include "VulkanSampler.h"
#include "VulkanShader.h"
#include "VulkanDescriptorSet.h"
#include "VulkanSwapChain.h"
#include "VulkanRenderPass.h"
#include "VulkanFramebuffer.h"
#include "VulkanPipeline.h"

    // IDeviceインターフェース実装
    BufferPtr VulkanDevice::CreateBuffer(const BufferDesc &desc)
    {
        auto buffer = MakeShared<VulkanBuffer>(
            TSharedPtr<VulkanDevice>(this, [](VulkanDevice *) {}), desc);
        return StaticPointerCast<IBuffer>(buffer);
    }

    AccelerationStructurePtr VulkanDevice::CreateAccelerationStructure(const AccelerationStructureDesc& desc)
    {
        if (!m_Capabilities.RayTracing.bAccelerationStructure ||
            !m_Capabilities.bBufferDeviceAddress || !IsValidAccelerationStructureDesc(desc))
        {
            return {};
        }

        auto accelerationStructure = MakeShared<VulkanAccelerationStructure>(
            TSharedPtr<VulkanDevice>(this, [](VulkanDevice *) {}),
            desc);
        return StaticPointerCast<IAccelerationStructure>(accelerationStructure);
    }

    TexturePtr VulkanDevice::CreateTexture(const TextureDesc &desc)
    {
        auto texture = MakeShared<VulkanTexture>(
            TSharedPtr<VulkanDevice>(this, [](VulkanDevice *) {}), desc);
        return StaticPointerCast<ITexture>(texture);
    }

    SamplerPtr VulkanDevice::CreateSampler(const SamplerDesc &desc)
    {
        auto sampler = MakeShared<VulkanSampler>(
            TSharedPtr<VulkanDevice>(this, [](VulkanDevice *) {}), desc);
        return StaticPointerCast<ISampler>(sampler);
    }

    ShaderPtr VulkanDevice::CreateShader(const ShaderDesc &desc)
    {
        auto shader = MakeShared<VulkanShader>(
            TSharedPtr<VulkanDevice>(this, [](VulkanDevice *) {}), desc);
        return StaticPointerCast<IShader>(shader);
    }

    CommandListPtr VulkanDevice::CreateCommandList()
    {
        auto commandList = MakeShared<VulkanCommandList>(
            TSharedPtr<VulkanDevice>(this, [](VulkanDevice *) {}));
        return StaticPointerCast<ICommandList>(commandList);
    }

    SwapChainPtr VulkanDevice::CreateSwapChain(const SwapChainDesc &desc)
    {
        auto swapChain = MakeShared<VulkanSwapChain>(
            TSharedPtr<VulkanDevice>(this, [](VulkanDevice *) {}), desc);
        return StaticPointerCast<ISwapChain>(swapChain);
    }

    RenderPassPtr VulkanDevice::CreateRenderPass(const RenderPassDesc &desc)
    {
        auto renderPass = MakeShared<VulkanRenderPass>(
            TSharedPtr<VulkanDevice>(this, [](VulkanDevice *) {}), desc);
        return StaticPointerCast<IRenderPass>(renderPass);
    }

    FramebufferPtr VulkanDevice::CreateFramebuffer(const FramebufferDesc &desc)
    {
        auto framebuffer = MakeShared<VulkanFramebuffer>(
            TSharedPtr<VulkanDevice>(this, [](VulkanDevice *) {}), desc);
        return StaticPointerCast<IFramebuffer>(framebuffer);
    }

    PipelinePtr VulkanDevice::CreateGraphicsPipeline(const GraphicsPipelineDesc &desc)
    {
        auto pipeline = MakeShared<VulkanGraphicsPipeline>(
            TSharedPtr<VulkanDevice>(this, [](VulkanDevice *) {}), desc);
        return StaticPointerCast<IPipeline>(pipeline);
    }

    PipelinePtr VulkanDevice::CreateComputePipeline(const ComputePipelineDesc &desc)
    {
        auto pipeline = MakeShared<VulkanComputePipeline>(
            TSharedPtr<VulkanDevice>(this, [](VulkanDevice *) {}), desc);
        return StaticPointerCast<IPipeline>(pipeline);
    }

    // ResourceBindTypeからDescriptorTypeへの変換ヘルパー
    DescriptorType VulkanDevice::ConvertResourceBindType(ResourceBindType type)
    {
        switch (type)
        {
        case ResourceBindType::ConstantBuffer:
            return DescriptorType::UniformBuffer;
        case ResourceBindType::Texture:
            return DescriptorType::SampledImage;
        case ResourceBindType::Sampler:
            return DescriptorType::Sampler;
        case ResourceBindType::CombinedImageSampler:
            return DescriptorType::CombinedImageSampler;
        case ResourceBindType::RWTexture:
            return DescriptorType::StorageImage;
        case ResourceBindType::RWBuffer:
            return DescriptorType::StorageBuffer;
        case ResourceBindType::StructuredBuffer:
            return DescriptorType::StorageBuffer;
        default:
            return DescriptorType::UniformBuffer;
        }
    }

    DescriptorSetPtr VulkanDevice::CreateDescriptorSet(const DescriptorSetDesc &desc)
    {
        // DescriptorBindingをDescriptorBindingDescに変換
        VariableArray<DescriptorBindingDesc> bindingDescs;
        bindingDescs.reserve(desc.bindings.size());
        for (const auto &binding : desc.bindings)
        {
            DescriptorBindingDesc bindingDesc;
            bindingDesc.binding = binding.binding;
            bindingDesc.type = ConvertResourceBindType(binding.type);
            bindingDesc.stages = binding.stages;
            bindingDesc.count = 1;
            bindingDescs.push_back(bindingDesc);
        }

        // ディスクリプタセットレイアウトの作成
        auto layout = MakeShared<VulkanDescriptorSetLayout>(
            TSharedPtr<VulkanDevice>(this, [](VulkanDevice *) {}),
            bindingDescs);

        // ディスクリプタプールの作成
        auto pool = MakeShared<VulkanDescriptorPool>(
            TSharedPtr<VulkanDevice>(this, [](VulkanDevice *) {}),
            10); // 10セット分のプールを作成

        // VulkanDescriptorSetの作成
        auto descriptorSet = MakeShared<VulkanDescriptorSet>(
            TSharedPtr<VulkanDevice>(this, [](VulkanDevice *) {}),
            desc,
            layout,
            pool);
        return StaticPointerCast<IDescriptorSet>(descriptorSet);
    }

    void VulkanDevice::WaitIdle()
    {
        const VkResult result = WaitIdleWithoutResultCheck(m_device, "VulkanDevice::WaitIdle");
        if (result == VK_ERROR_DEVICE_LOST)
        {
            ReportDeviceFaultOnce();
        }
    }

    void VulkanDevice::ReportDeviceFaultOnce()
    {
        if (m_bDeviceFaultReported.Exchange(true))
        {
            return;
        }

        if (m_deviceFaultFeatures.deviceFault != VK_TRUE)
        {
            NORVES_LOG_ERROR("Vulkan", "vkGetDeviceFaultInfoEXT unavailable feature_device_fault=0");
            return;
        }

        const PFN_vkGetDeviceFaultInfoEXT getDeviceFaultInfo =
            VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceFaultInfoEXT;
        if (getDeviceFaultInfo == nullptr)
        {
            NORVES_LOG_ERROR("Vulkan", "vkGetDeviceFaultInfoEXT unavailable function_pointer=0");
            return;
        }

        VkDeviceFaultCountsEXT totalCounts{
            VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT,
            nullptr,
            0,
            0,
            0};
        const VkResult totalResult = getDeviceFaultInfo(static_cast<VkDevice>(m_device), &totalCounts, nullptr);
        if (totalResult != VK_SUCCESS && totalResult != VK_INCOMPLETE)
        {
            NORVES_LOG_ERROR("Vulkan", "vkGetDeviceFaultInfoEXT failed step=counts result=%d", static_cast<int32_t>(totalResult));
            return;
        }

        const uint32_t totalAddressInfoCount = totalCounts.addressInfoCount;
        const uint32_t totalVendorInfoCount = totalCounts.vendorInfoCount;
        const VkDeviceSize totalVendorBinarySize = totalCounts.vendorBinarySize;
        constexpr uint32_t maxCapturedFaultInfos = 16;
        const uint32_t requestedAddressInfoCount = std::min(totalAddressInfoCount, maxCapturedFaultInfos);
        const uint32_t requestedVendorInfoCount = std::min(totalVendorInfoCount, maxCapturedFaultInfos);

        FixedArray<VkDeviceFaultAddressInfoEXT, 16> addressInfos{};
        FixedArray<VkDeviceFaultVendorInfoEXT, 16> vendorInfos{};
        VkDeviceFaultCountsEXT writtenCounts{
            VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT,
            nullptr,
            requestedAddressInfoCount,
            requestedVendorInfoCount,
            0};
        writtenCounts.vendorBinarySize = 0;
        VkDeviceFaultInfoEXT faultInfo{
            VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT,
            nullptr,
            {},
            addressInfos.data(),
            vendorInfos.data(),
            nullptr};
        faultInfo.pVendorBinaryData = nullptr;
        const VkResult detailsResult = getDeviceFaultInfo(static_cast<VkDevice>(m_device), &writtenCounts, &faultInfo);
        if (detailsResult != VK_SUCCESS && detailsResult != VK_INCOMPLETE)
        {
            NORVES_LOG_ERROR("Vulkan", "vkGetDeviceFaultInfoEXT failed step=details result=%d", static_cast<int32_t>(detailsResult));
            return;
        }

        const uint32_t writtenAddressInfoCount = std::min(writtenCounts.addressInfoCount, maxCapturedFaultInfos);
        const uint32_t writtenVendorInfoCount = std::min(writtenCounts.vendorInfoCount, maxCapturedFaultInfos);
        NORVES_LOG_ERROR(
            "Vulkan",
            "vkGetDeviceFaultInfoEXT captured result=%d total_addresses=%u written_addresses=%u total_vendors=%u written_vendors=%u total_vendor_binary_size=%llu written_vendor_binary_size=%llu description=%s",
            static_cast<int32_t>(detailsResult),
            totalAddressInfoCount,
            writtenAddressInfoCount,
            totalVendorInfoCount,
            writtenVendorInfoCount,
            static_cast<unsigned long long>(totalVendorBinarySize),
            static_cast<unsigned long long>(writtenCounts.vendorBinarySize),
            faultInfo.description);

        for (uint32_t index = 0; index < writtenAddressInfoCount; ++index)
        {
            const VkDeviceFaultAddressInfoEXT &addressInfo = addressInfos[index];
            NORVES_LOG_ERROR(
                "Vulkan",
                "vkGetDeviceFaultInfoEXT address index=%u type=%d reported_address=%llu precision=%llu",
                index,
                static_cast<int32_t>(addressInfo.addressType),
                static_cast<unsigned long long>(addressInfo.reportedAddress),
                static_cast<unsigned long long>(addressInfo.addressPrecision));
        }

        for (uint32_t index = 0; index < writtenVendorInfoCount; ++index)
        {
            const VkDeviceFaultVendorInfoEXT &vendorInfo = vendorInfos[index];
            NORVES_LOG_ERROR(
                "Vulkan",
                "vkGetDeviceFaultInfoEXT vendor index=%u description=%s code=%llu data=%llu",
                index,
                vendorInfo.description,
                static_cast<unsigned long long>(vendorInfo.vendorFaultCode),
                static_cast<unsigned long long>(vendorInfo.vendorFaultData));
        }
    }

    IGPUResourceAllocator* VulkanDevice::GetResourceAllocator()
    {
        return m_ResourceAllocator.get();
    }

    ShaderCompilerPtr VulkanDevice::CreateShaderCompiler()
    {
        auto compiler = MakeShared<VulkanShaderCompiler>();
        return StaticPointerCast<IShaderCompiler>(compiler);
    }

    ShaderCompilerPtr VulkanDevice::CreateSlangShaderCompiler()
    {
        auto compiler = MakeShared<VulkanSlangCompiler>();
        return StaticPointerCast<IShaderCompiler>(compiler);
    }

    Math::Matrix4x4 VulkanDevice::AdjustProjectionForClipSpace(
        const Math::Matrix4x4 &projection, bool bApplyYFlip) const
    {
        // Vulkanクリップ空間補正行列を構築
        // 右手系座標のZ軸反転（視線方向が-Zのため）+ オプションのY軸反転
        const Math::Matrix4x4 correction =
            Math::MatrixUtils::CreateScale(Math::Vector3(1.0f, bApplyYFlip ? -1.0f : 1.0f, -1.0f));
        return projection * correction;
    }

    // デバイス能力の検出
    void VulkanDevice::DetectCapabilities()
    {
        // 基本情報
        auto props = m_physicalDevice.getProperties();
        std::memcpy(m_Capabilities.DeviceName, props.deviceName.data(),
                    std::min(sizeof(m_Capabilities.DeviceName) - 1, sizeof(props.deviceName)));
        m_Capabilities.bIsNvidia = (props.vendorID == 0x10DE);
        m_Capabilities.bIsDiscreteGPU = (props.deviceType == vk::PhysicalDeviceType::eDiscreteGpu);

        // 利用可能な拡張を列挙
        auto extensionsResult = m_physicalDevice.enumerateDeviceExtensionProperties();
        Set<String> availableExtensions;
        if (extensionsResult.result == vk::Result::eSuccess)
        {
            for (const auto &ext : extensionsResult.value)
            {
                availableExtensions.insert(String(ext.extensionName.data()));
            }
        }

        // ========================================
        // Neural Shaders (Cooperative Vector)
        // ========================================
        if (availableExtensions.contains(String(VK_NV_COOPERATIVE_VECTOR_EXTENSION_NAME)))
        {
            // Features2チェーンで実際のサポート状況を問い合わせ
            vk::PhysicalDeviceCooperativeVectorFeaturesNV coopFeatures{};
            vk::PhysicalDeviceFeatures2 features2Query{};
            features2Query.pNext = &coopFeatures;
            m_physicalDevice.getFeatures2(&features2Query);

            m_Capabilities.NeuralShaders.bSupported = (coopFeatures.cooperativeVector == VK_TRUE);
            m_Capabilities.NeuralShaders.bCooperativeVectorTraining = (coopFeatures.cooperativeVectorTraining == VK_TRUE);
            m_Capabilities.NeuralShaders.bCooperativeVectorShaderConvert = m_Capabilities.NeuralShaders.bSupported;

            // プロパティの取得
            vk::PhysicalDeviceCooperativeVectorPropertiesNV coopProps{};
            vk::PhysicalDeviceProperties2 props2{};
            props2.pNext = &coopProps;
            m_physicalDevice.getProperties2(&props2);

            m_Capabilities.NeuralShaders.MaxCooperativeVectorComponents =
                coopProps.maxCooperativeVectorComponents;

            if (m_Capabilities.NeuralShaders.bSupported)
            {
                NORVES_LOG_INFO("VulkanDevice", "Cooperative Vector (Neural Shaders) supported: maxComponents=%u",
                                m_Capabilities.NeuralShaders.MaxCooperativeVectorComponents);
            }
        }

        // ========================================
        // Mega Geometry (Cluster Acceleration Structure) - 検出のみ
        // ========================================
        m_Capabilities.MegaGeometry.bAccelerationStructureSupported =
            availableExtensions.contains(String("VK_KHR_acceleration_structure"));
        m_Capabilities.MegaGeometry.bRayTracingPipelineSupported =
            availableExtensions.contains(String("VK_KHR_ray_tracing_pipeline"));
        m_Capabilities.MegaGeometry.bSupported =
            availableExtensions.contains(String("VK_NV_cluster_acceleration_structure")) &&
            m_Capabilities.MegaGeometry.bAccelerationStructureSupported;

        m_Capabilities.RayTracing.bAccelerationStructure =
            m_accelerationStructureFeatures.accelerationStructure == VK_TRUE;
        m_Capabilities.RayTracing.bRayQuery = m_rayQueryFeatures.rayQuery == VK_TRUE;
        m_Capabilities.RayTracing.bRayTracingPipeline =
            m_rayTracingPipelineFeatures.rayTracingPipeline == VK_TRUE;

        // ========================================
        // Draw Indirect (論理デバイスで有効化済みのコア機能)
        // ========================================
        {
            m_Capabilities.bDrawIndirectCount = (m_vulkan12Features.drawIndirectCount == VK_TRUE);
            m_Capabilities.bBufferDeviceAddress =
                m_vulkan12Features.bufferDeviceAddress == VK_TRUE;
            m_Capabilities.bDrawIndirectFirstInstance =
                (m_enabledDeviceFeatures.drawIndirectFirstInstance == VK_TRUE);

            if (m_Capabilities.bDrawIndirectCount)
            {
                NORVES_LOG_INFO("VulkanDevice", "DrawIndirectCount enabled (Vulkan 1.2 core)");
            }

            if (m_Capabilities.bDrawIndirectFirstInstance)
            {
                NORVES_LOG_INFO("VulkanDevice", "DrawIndirectFirstInstance enabled (Vulkan core)");
            }
        }

        // サマリーログ
        const char *deviceName = m_Capabilities.DeviceName;
        NORVES_LOG_INFO("VulkanDevice", "Device Capabilities: GPU=%s, NVIDIA=%s, "
                                        "NeuralShaders=%s, MegaGeometry=%s, AccelerationStructure=%s, "
                                        "RayQuery=%s, RayTracingPipeline=%s, DrawIndirectCount=%s, DrawIndirectFirstInstance=%s",
                        deviceName,
                        m_Capabilities.bIsNvidia ? "Yes" : "No",
                        m_Capabilities.NeuralShaders.bSupported ? "Yes" : "No",
                        m_Capabilities.MegaGeometry.bSupported ? "Yes" : "No",
                        m_Capabilities.RayTracing.bAccelerationStructure ? "Yes" : "No",
                        m_Capabilities.RayTracing.bRayQuery ? "Yes" : "No",
                        m_Capabilities.RayTracing.bRayTracingPipeline ? "Yes" : "No",
                        m_Capabilities.bDrawIndirectCount ? "Yes" : "No",
                        m_Capabilities.bDrawIndirectFirstInstance ? "Yes" : "No");
    }

} // namespace NorvesLib::RHI::Vulkan

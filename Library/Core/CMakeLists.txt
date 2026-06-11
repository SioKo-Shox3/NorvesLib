cmake_minimum_required(VERSION 3.28)

# モジュール名の設定
set(MODULE_NAME Core)

# Vulkan SDKを検索
find_package(Vulkan REQUIRED)

# ソースファイルの定義
set(PRIVATE_SOURCES
    Private/CoreModule.cpp
    Private/Asset/AssetBlob.cpp
    Private/Asset/CookedTextureLoader.cpp
    Private/Asset/AssetFileReader.cpp
    Private/Asset/AssetManifest.cpp
    Private/Asset/AssetPath.cpp
    Private/Asset/AssetSystem.cpp
    Private/Object/Object.cpp
    Private/Object/UnknownImpl.cpp
    Private/Object/ObjectPropertyOps.cpp
    Private/Object/ObjectHeap.cpp
    Private/Object/ObjectResolver.cpp
    Private/Object/Invocation.cpp
    Private/Object/SchemaProjection.cpp
    Private/Object/ClassRegistry.cpp
    Private/Object/Resource.cpp
    Private/Object/WorldObject.cpp
    Private/Object/ResourceRegistry.cpp
    Private/Object/World.cpp
    Private/Text/IdentityPool.cpp
    Private/Text/JsonDocument.cpp
    Private/Logging/Logger.cpp
    Private/Logging/LogTypes.cpp
    Private/Debug/DebugOutput.cpp
    Private/Debug/Stats.cpp
    Private/EngineGlobals/MemoryOverrides.cpp
    Private/Platform/Windows/WindowsApplication.cpp
    Private/Platform/Windows/WindowsApplicationFactory.cpp
    Private/Platform/Windows/WindowsWindow.cpp
    Private/Platform/Windows/WindowsEntryPoint.cpp
    Private/Platform/Windows/WindowsPlatformProcess.cpp
    Private/Platform/Windows/WindowsPlatformConsole.cpp
    Private/Boot/AppLauncher.cpp
    Private/Application/ApplicationFactory.cpp
    Private/Engine/Engine.cpp
    Private/Engine/NorvesEngine.cpp
    Private/Engine/ApplicationProcessor.cpp
    # Rendering
    Private/Rendering/Screen.cpp
    Private/Rendering/View.cpp
    Private/Rendering/Viewport.cpp
    Private/Rendering/CameraViewConstants.cpp
    Private/Rendering/SceneView.cpp
    Private/Rendering/DrawCommand.cpp
    Private/Rendering/RenderingCoordinator.cpp
    Private/Rendering/RenderThread.cpp
    Private/Rendering/RenderFrameExecutor.cpp
    Private/Rendering/PresentationComposer.cpp
    Private/Rendering/PersistentResourceCache.cpp
    Private/Rendering/SceneRenderer.cpp
    Private/Rendering/RenderWorld.cpp
    Private/Rendering/SharedResourceRegistry.cpp
    Private/Rendering/RenderResources.cpp
    Private/Rendering/TextureAssetRuntime.cpp
    Private/Rendering/MegaGeometryResourceStore.cpp
    Private/Rendering/ProceduralMeshGpuStore.cpp
    Private/Rendering/RenderMaterialStore.cpp
    Private/Rendering/GpuResourceStore.cpp
    Private/Rendering/TextureAsyncLoadQueue.cpp
    Private/Rendering/TextureAssetLoader.cpp
    Private/Rendering/TextureHandleCache.cpp
    Private/Rendering/TextureAssetResolver.cpp
    Private/Rendering/TextureUploadProfile.cpp
    Private/Rendering/CookedTextureUpload.cpp
    Private/Rendering/DynamicUniformAllocator.cpp
    Private/Rendering/PostProcessStack.cpp
    Private/Rendering/GBufferPass.cpp
    Private/Rendering/ShadowMapPass.cpp
    Private/Rendering/LightingPass.cpp
    Private/Rendering/ToneMappingPass.cpp
    Private/Rendering/BloomPass.cpp
    Private/Rendering/SSAOPass.cpp
    Private/Rendering/FXAAPass.cpp
    Private/Rendering/UpscalePass.cpp
    Private/Rendering/SSRPass.cpp
    Private/Rendering/ForwardPass.cpp
    Private/Rendering/ShaderManager.cpp
    Private/Rendering/NeuralMaterialResource.cpp
    Private/Rendering/NeuralMaterialDecoder.cpp
    Private/Rendering/NeuralMaterialDecodePass.cpp
    # MegaGeometry
    Private/Rendering/MegaGeometry/MeshClusterizer.cpp
    Private/Rendering/MegaGeometry/MeshSimplifier.cpp
    Private/Rendering/MegaGeometry/ClusterGroupBuilder.cpp
    Private/Rendering/MegaGeometry/LODHierarchyBuilder.cpp
    Private/Rendering/MegaGeometryPass.cpp
    # Engine
    Private/Engine/AssetRegistry.cpp
    # Resource
    Private/Resource/MeshResource.cpp
    Private/Resource/MaterialResource.cpp
    Private/Resource/GLTFAnalyzer.cpp
    # Component
    Private/Component/Component.cpp
    Private/Component/MeshComponent.cpp
    Private/Component/MegaGeometryComponent.cpp
    Private/Component/LightComponent.cpp
    Private/Component/PointLightComponent.cpp
    # Thread
    Private/Thread/ConditionVariable.cpp
    Private/Thread/Fiber.cpp
    Private/Thread/JobSystem.cpp
    Private/Thread/Mutex.cpp
    Private/Thread/ReadWriteLock.cpp
    Private/Thread/Task.cpp
    Private/Thread/Thread.cpp
    Private/Thread/WorkStealingQueue.cpp
    # RHI Vulkan
    Private/RHI/Vulkan/VulkanBuffer.cpp
    Private/RHI/Vulkan/VulkanCommandList.cpp
    Private/RHI/Vulkan/VulkanDescriptorSet.cpp
    Private/RHI/Vulkan/VulkanDevice.cpp
    Private/RHI/Vulkan/VulkanFramebuffer.cpp
    Private/RHI/Vulkan/VulkanPipeline.cpp
    Private/RHI/Vulkan/VulkanRenderPass.cpp
    Private/RHI/Vulkan/VulkanSampler.cpp
    Private/RHI/Vulkan/VulkanShader.cpp
    Private/RHI/Vulkan/VulkanShaderCompiler.cpp
    Private/RHI/Vulkan/VulkanSlangCompiler.cpp
    Private/RHI/Vulkan/VulkanSwapChain.cpp
    Private/RHI/Vulkan/VulkanTexture.cpp
    Private/RHI/TransientResourcePool.cpp
    # Memory
    Private/Memory/FrameAllocator.cpp
    Private/Memory/GlobalAllocator.cpp
    Private/Memory/MemorySystem.cpp
    Private/Memory/StackAllocator.cpp
    Private/Memory/ThreadLocalCache.cpp
    # Input
    Private/Input/InputState.cpp
    Private/Input/InputSystem.cpp
    Private/Input/MayaCameraController.cpp
    Private/Input/LightController.cpp
    # Random
    Private/Random/Random.cpp
    Private/Random/RandomDistributions.cpp
    Private/Random/RandomUtility.cpp
    # FileStream
    Private/FileStream/FileStream.cpp
    Private/FileStream/AsyncFileStream.cpp
    Private/FileStream/Package.cpp
    # ThirdParty
    ${CMAKE_SOURCE_DIR}/Library/ThirdParty/stb/stb_image_impl.cpp
)

set(PUBLIC_HEADERS
    Public/CoreModule.h
    Public/Asset/AssetBlob.h
    Public/Asset/CookedTextureFormat.h
    Public/Asset/AssetFileReader.h
    Public/Asset/AssetManifest.h
    Public/Asset/AssetPackageFormat.h
    Public/Asset/AssetReadRequest.h
    Public/Asset/AssetResolveResult.h
    Public/Asset/AssetSystem.h
    Public/Asset/AssetPath.h
    Public/Object/Object.h
    Public/Text/IdentityPool.h
    Public/Text/JsonDocument.h
    Public/Container/Containers.h
    Public/Container/PointerTypes.h
    Public/Container/VariableArray.h
    Public/Container/FixedArray.h
    Public/Container/List.h
    Public/Container/Map.h
    Public/Container/Set.h
    Public/Container/UnorderedMap.h
    Public/Container/UnorderedSet.h
    Public/Container/String.h
    Public/Container/StringView.h
    Public/Container/Span.h
    Public/Container/Deque.h
    Public/Container/Queue.h
    Public/Logging/LoggingModule.h
    Public/Logging/Logger.h
    Public/Logging/LogMacros.h
    Public/Debug/DebugConfig.h
    Public/Debug/DebugOutput.h
    Public/Debug/Stats.h
    Public/EngineGlobals/MemoryOverrides.h
    Public/Platform/NativeWindowHandle.h
    Public/Platform/PlatformProcess.h
    Public/Platform/PlatformConsole.h
    Public/Platform/Windows/WindowsApplication.h
    Public/Platform/Windows/WindowsApplicationFactory.h
    Public/Platform/Windows/WindowsWindow.h
    Public/Boot/AppLauncher.h
    Public/Boot/BootConfig.h
    Public/Boot/EntryPoint.h
    Public/Application/ApplicationFactory.h
    Public/Application/IApplicationHandler.h
    Public/Application/ApplicationHandlerBase.h
    Public/Engine/Engine.h
    Public/Engine/ApplicationProcessor.h
    # GameMode
    Public/GameMode/IStateMachine.h
    Public/GameMode/TStateMachine.h
    Public/GameMode/IGameMode.h
    Public/GameMode/TGameMode.h
    Public/GameMode/GameModeFactory.h
    Public/GameMode/GameModeTypes.h
    # Rendering
    Public/Rendering/RenderTypes.h
    Public/Rendering/GpuResourceTypes.h
    Public/Rendering/TextureAssetTypes.h
    Public/Rendering/TextureAsyncTypes.h
    Public/Rendering/VertexLayout.h
    Public/Rendering/MeshTypes.h
    Public/Rendering/MaterialTypes.h
    Public/Rendering/ITextureHandleRegistrar.h
    Public/Rendering/ProceduralMeshGPUData.h
    Public/Rendering/SceneProxy.h
    Public/Rendering/FramePacket.h
    Public/Rendering/RenderResourcesFwd.h
    Public/Rendering/RenderResourceContexts.h
    Public/Rendering/RenderResources.h
    Public/Rendering/RenderThread.h
    Public/Rendering/RenderWorld.h
    Public/Rendering/Screen.h
    Public/Rendering/View.h
    Public/Rendering/Viewport.h
    Public/Rendering/CameraViewConstants.h
    Public/Rendering/ViewRenderPlan.h
    Public/Rendering/ViewportSnapshot.h
    Public/Rendering/SceneView.h
    Public/Rendering/DrawCommand.h
    Public/Rendering/RenderingCoordinator.h
    Public/Rendering/RenderFrameExecutor.h
    Public/Rendering/PresentationComposer.h
    Public/Rendering/PersistentResourceCache.h
    Public/Rendering/SceneRenderer.h
    Public/Rendering/ViewRenderContext.h
    Public/Rendering/IViewPass.h
    Public/Rendering/SharedResourceRegistry.h
    Public/Rendering/PostProcessStack.h
    Public/Rendering/GBufferPass.h
    Public/Rendering/ShadowMapPass.h
    Public/Rendering/LightingPass.h
    Public/Rendering/ToneMappingPass.h
    Public/Rendering/BloomPass.h
    Public/Rendering/SSAOPass.h
    Public/Rendering/FXAAPass.h
    Public/Rendering/UpscalePass.h
    Public/Rendering/SSRPass.h
    Public/Rendering/ForwardPass.h
    Public/Rendering/ShaderManager.h
    Public/Rendering/NeuralMaterialResource.h
    Public/Rendering/NeuralMaterialDecoder.h
    Public/Rendering/NeuralMaterialDecodePass.h
    Public/Rendering/MegaGeometryPass.h
    # MegaGeometry types
    Public/Rendering/MegaGeometry/MegaGeometryTypes.h
    Public/Rendering/MegaGeometry/MeshClusterizer.h
    Public/Rendering/MegaGeometry/MeshSimplifier.h
    Public/Rendering/MegaGeometry/ClusterGroupBuilder.h
    Public/Rendering/MegaGeometry/LODHierarchyBuilder.h
    # Object
    Public/Object/Invocation.h
    Public/Object/ObjectCast.h
    Public/Object/ObjectHandle.h
    Public/Object/ObjectHeap.h
    Public/Object/ObjectPropertyOps.h
    Public/Object/ObjectReference.h
    Public/Object/ObjectResolver.h
    Public/Object/PropertyBag.h
    Public/Object/ReferenceCollector.h
    Public/Object/Resource.h
    Public/Object/ResourceRef.h
    Public/Object/RuntimeSchema.h
    Public/Object/SchemaProjection.h
    Public/Object/WorldObject.h
    Public/Object/ResourceRegistry.h
    Public/Object/World.h
    # Engine
    Public/Engine/NorvesEngine.h
    Public/Engine/IEngine.h
    Public/Engine/AssetRegistry.h
    # Resource
    Public/Resource/MeshResource.h
    Public/Resource/MaterialResource.h
    Public/Resource/GLTFAnalyzer.h
    # Component
    Public/Component/Component.h
    Public/Component/MeshComponent.h
    Public/Component/MegaGeometryComponent.h
    Public/Component/LightComponent.h
    Public/Component/PointLightComponent.h
    # Math
    Public/Math/MathForward.h
    Public/Math/MathTypes.h
    Public/Math/Matrix4x4.h
    Public/Math/MatrixUtils.h
    Public/Math/Quaternion.h
    Public/Math/QuaternionUtils.h
    Public/Math/Transform.h
    Public/Math/Vector2.h
    Public/Math/Vector3.h
    Public/Math/Vector4.h
    Public/Math/VectorUtils.h
    # Thread
    Public/Thread/Atomic.h
    Public/Thread/ConditionVariable.h
    Public/Thread/Fiber.h
    Public/Thread/JobSystem.h
    Public/Thread/Mutex.h
    Public/Thread/ReadWriteLock.h
    Public/Thread/RingBuffer.h
    Public/Thread/Task.h
    Public/Thread/Thread.h
    Public/Thread/ThreadLocalStorage.h
    Public/Thread/WorkStealingQueue.h
    # RHI
    Public/RHI/RHITypes.h
    Public/RHI/RHIConfig.h
    Public/RHI/RHIContext.h
    Public/RHI/IBuffer.h
    Public/RHI/ITexture.h
    Public/RHI/ISampler.h
    Public/RHI/IShader.h
    Public/RHI/ICommandList.h
    Public/RHI/ISwapChain.h
    Public/RHI/IRenderPass.h
    Public/RHI/IFramebuffer.h
    Public/RHI/IPipeline.h
    Public/RHI/IDescriptorSet.h
    Public/RHI/IDevice.h
    Public/RHI/DeviceCapabilities.h
    Public/RHI/IShaderCompiler.h
    Public/RHI/IGPUResourceAllocator.h
    Public/RHI/TransientResourcePool.h
    # Memory
    Public/Memory/FrameAllocator.h
    Public/Memory/GlobalAllocator.h
    Public/Memory/IAllocator.h
    Public/Memory/MemoryConfig.h
    Public/Memory/MemorySystem.h
    Public/Memory/PoolAllocator.h
    Public/Memory/SizeClassAllocator.h
    Public/Memory/StackAllocator.h
    Public/Memory/ThreadLocalCache.h
    # Random
    Public/Random/Random.h
    Public/Random/RandomDistributions.h
    Public/Random/RandomUtility.h
    # FileStream
    Public/FileStream/IFileStream.h
    Public/FileStream/FileStream.h
    Public/FileStream/AsyncFileStream.h
    Public/FileStream/FileStreamForward.h
    Public/FileStream/FileStreamModule.h
    Public/FileStream/Package.h
    # Input
    Public/Input/InputTypes.h
    Public/Input/InputState.h
    Public/Input/InputSystem.h
    Public/Input/IInputDevice.h
    Public/Input/MayaCameraController.h
    Public/Input/LightController.h
    Public/Platform/Windows/WindowsKeyMap.h
)

# ライブラリの作成
add_library(${MODULE_NAME} STATIC ${PRIVATE_SOURCES} ${PUBLIC_HEADERS})

# インクルードディレクトリの設定
target_include_directories(${MODULE_NAME}
    PUBLIC 
        ${CMAKE_SOURCE_DIR}/Library  # Core/Public/... のパスでアクセス可能にする
        ${CMAKE_SOURCE_DIR}/Library/Core/Public  # Container/String.h のような相対インクルード用
    PRIVATE 
        Private
        ${CMAKE_SOURCE_DIR}/Library/ThirdParty/stb  # stb_image.h
)

target_compile_definitions(${MODULE_NAME}
    PUBLIC
        NORVES_BUILD_DEBUG=$<IF:$<CONFIG:Debug>,1,0>
        NORVES_BUILD_DEVELOPMENT=$<IF:$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>,1,0>
        NORVES_BUILD_RELEASE=$<IF:$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>,1,0>
        NORVES_ENABLE_LOGGING=$<IF:$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>,1,0>
        NORVES_ENABLE_DEBUG_OUTPUT=$<IF:$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>,1,0>
        NORVES_ENABLE_STATS=$<IF:$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>,1,0>
        NORVES_ENABLE_PROFILING=$<IF:$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>,1,0>
)

# shaderc（Vulkan SDK同梱のランタイムシェーダーコンパイラ）
find_library(SHADERC_LIB_RELEASE shaderc_combined PATHS $ENV{VULKAN_SDK}/Lib)
find_library(SHADERC_LIB_DEBUG shaderc_combinedd PATHS $ENV{VULKAN_SDK}/Lib)
if(NOT SHADERC_LIB_RELEASE OR NOT SHADERC_LIB_DEBUG)
    message(FATAL_ERROR "shaderc libraries not found in Vulkan SDK")
endif()

# Slang SDK（オプション - Neural Shaders用）
set(SLANG_SDK_DIR "" CACHE PATH "Path to Slang SDK root directory")
if(SLANG_SDK_DIR AND EXISTS "${SLANG_SDK_DIR}/include/slang.h")
    set(NORVES_HAS_SLANG TRUE)
    find_library(SLANG_LIB slang PATHS "${SLANG_SDK_DIR}/lib" "${SLANG_SDK_DIR}/bin")
    message(STATUS "Slang SDK found: ${SLANG_SDK_DIR}")
else()
    set(NORVES_HAS_SLANG FALSE)
    message(STATUS "Slang SDK not found (SLANG_SDK_DIR not set or invalid). Neural shader compilation disabled.")
endif()

# 依存関係の設定
target_link_libraries(${MODULE_NAME}
    PUBLIC
        Vulkan::Vulkan
    PRIVATE
        $<$<CONFIG:Debug>:${SHADERC_LIB_DEBUG}>
        $<$<NOT:$<CONFIG:Debug>>:${SHADERC_LIB_RELEASE}>
)

# Slang SDKリンク（オプション）
if(NORVES_HAS_SLANG)
    target_include_directories(${MODULE_NAME} PRIVATE "${SLANG_SDK_DIR}/include")
    target_link_libraries(${MODULE_NAME} PRIVATE ${SLANG_LIB})
    target_compile_definitions(${MODULE_NAME} PRIVATE NORVES_HAS_SLANG)
endif()

# シェーダーアセットディレクトリ（ソースからランタイムコンパイル用）
set(NORVES_SHADER_DIR "${CMAKE_SOURCE_DIR}/Assets/Shaders")
string(REPLACE "\\" "/" NORVES_SHADER_DIR "${NORVES_SHADER_DIR}")

# アセットルートディレクトリ（テクスチャ、モデル等のリソース用）
set(NORVES_ASSET_DIR "${CMAKE_SOURCE_DIR}/Assets")
string(REPLACE "\\" "/" NORVES_ASSET_DIR "${NORVES_ASSET_DIR}")

# Vulkan設定
target_compile_definitions(${MODULE_NAME}
    PRIVATE
        VULKAN_HPP_NO_EXCEPTIONS
        NORVES_SHADER_DIR="${NORVES_SHADER_DIR}"
        NORVES_ASSET_DIR="${NORVES_ASSET_DIR}"
)

# C++23の設定
target_compile_features(${MODULE_NAME} PUBLIC cxx_std_23)

# Windows固有の設定
if(WIN32)
    target_compile_definitions(${MODULE_NAME} PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

# IDEでのファイル整理（Visual Studio等）
source_group("Public Headers" FILES ${PUBLIC_HEADERS})
source_group("Private Sources" FILES ${PRIVATE_SOURCES})
source_group("RHI/Private" FILES 
    Private/RHI/Vulkan/VulkanBuffer.cpp
    Private/RHI/Vulkan/VulkanCommandList.cpp
    Private/RHI/Vulkan/VulkanDescriptorSet.cpp
    Private/RHI/Vulkan/VulkanDevice.cpp
    Private/RHI/Vulkan/VulkanFramebuffer.cpp
    Private/RHI/Vulkan/VulkanPipeline.cpp
    Private/RHI/Vulkan/VulkanRenderPass.cpp
    Private/RHI/Vulkan/VulkanSampler.cpp
    Private/RHI/Vulkan/VulkanShader.cpp
    Private/RHI/Vulkan/VulkanShaderCompiler.cpp
    Private/RHI/Vulkan/VulkanSlangCompiler.cpp
    Private/RHI/Vulkan/VulkanSwapChain.cpp
    Private/RHI/Vulkan/VulkanTexture.cpp
)

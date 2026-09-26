add_executable(FramePacketManagerTest
    FramePacketManagerTest.cpp
)
target_link_libraries(FramePacketManagerTest PRIVATE Core)
target_include_directories(FramePacketManagerTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(FramePacketManagerTest PROPERTIES FOLDER "Test/Core/Rendering")

include(CTest)
add_test(NAME FramePacketManagerTest COMMAND FramePacketManagerTest)

add_executable(RenderGraphGPUTimestampScopeTest
    RenderGraphGPUTimestampScopeTest.cpp
)
target_link_libraries(RenderGraphGPUTimestampScopeTest PRIVATE Core)
target_include_directories(RenderGraphGPUTimestampScopeTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
set_target_properties(RenderGraphGPUTimestampScopeTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RenderGraphGPUTimestampScopeTest COMMAND RenderGraphGPUTimestampScopeTest)

add_executable(RenderingCoordinatorGPUTimestampPublicationTest
    RenderingCoordinatorGPUTimestampPublicationTest.cpp
)
target_link_libraries(RenderingCoordinatorGPUTimestampPublicationTest PRIVATE Core)
target_include_directories(RenderingCoordinatorGPUTimestampPublicationTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
set_target_properties(RenderingCoordinatorGPUTimestampPublicationTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RenderingCoordinatorGPUTimestampPublicationTest COMMAND RenderingCoordinatorGPUTimestampPublicationTest)

add_executable(RHIGPUTimestampVulkanTest
    RHIGPUTimestampVulkanTest.cpp
)
target_link_libraries(RHIGPUTimestampVulkanTest PRIVATE
    RenderingValidationFixtureSupport
    RenderingGpuTestSupport
)
target_include_directories(RHIGPUTimestampVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
target_compile_definitions(RHIGPUTimestampVulkanTest PRIVATE
    NORVES_BINARY_ROOT="${CMAKE_BINARY_DIR}"
)
set_target_properties(RHIGPUTimestampVulkanTest PROPERTIES FOLDER "Test/Core/Rendering")
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/RenderingValidation/TimestampRuns)
add_test(NAME RHIGPUTimestampVulkanTest COMMAND RHIGPUTimestampVulkanTest)
add_test(NAME RHIGPUTimestampVulkanSkipContractTest COMMAND RHIGPUTimestampVulkanTest)
set_tests_properties(
    RHIGPUTimestampVulkanTest
    RHIGPUTimestampVulkanSkipContractTest
    PROPERTIES
        LABELS "GPU;RenderingValidation"
        RESOURCE_LOCK NorvesLibGPU
        SKIP_RETURN_CODE 125
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/RenderingValidation/TimestampRuns
)
set_tests_properties(RHIGPUTimestampVulkanSkipContractTest PROPERTIES
    ENVIRONMENT "NORVESLIB_FORCE_GPU_TEST_SKIP=1")

add_library(RenderingGpuTestSupport STATIC
    RenderingValidation/GpuTestEnvironment.cpp
)
target_link_libraries(RenderingGpuTestSupport PUBLIC Core)
target_include_directories(RenderingGpuTestSupport PUBLIC
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
set_target_properties(RenderingGpuTestSupport PROPERTIES FOLDER "Test/Core/Rendering")

add_library(RenderingValidationFixtureSupport STATIC
    RenderingValidation/RenderingValidationScene.cpp
    RenderingValidation/RenderingValidationApplication.cpp
)
target_link_libraries(RenderingValidationFixtureSupport PUBLIC Core)
target_include_directories(RenderingValidationFixtureSupport PUBLIC
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
set_target_properties(RenderingValidationFixtureSupport PROPERTIES FOLDER "Test/Core/Rendering")

function(register_rendering_gpu_test test_name target_name)
    add_test(NAME ${test_name} COMMAND ${target_name} ${ARGN})
    set_tests_properties(${test_name} PROPERTIES
        LABELS "GPU;RenderingValidation"
        RESOURCE_LOCK NorvesLibGPU
        SKIP_RETURN_CODE 125
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endfunction()

add_executable(RenderThreadFrameCompletionTest
    RenderThreadFrameCompletionTest.cpp
)
target_link_libraries(RenderThreadFrameCompletionTest PRIVATE Core)
target_include_directories(RenderThreadFrameCompletionTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(RenderThreadFrameCompletionTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RenderThreadFrameCompletionTest COMMAND RenderThreadFrameCompletionTest)

add_executable(RHIBufferDeviceAddressVulkanTest
    RHIBufferDeviceAddressVulkanTest.cpp
)
target_link_libraries(RHIBufferDeviceAddressVulkanTest PRIVATE RenderingGpuTestSupport)
target_include_directories(RHIBufferDeviceAddressVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
set_target_properties(RHIBufferDeviceAddressVulkanTest PROPERTIES FOLDER "Test/Core/Rendering")
register_rendering_gpu_test(
    RHIBufferDeviceAddressVulkanTest
    RHIBufferDeviceAddressVulkanTest
)
add_executable(RHIAccelerationStructureVulkanTest
    RHIAccelerationStructureVulkanTest.cpp
)
target_link_libraries(RHIAccelerationStructureVulkanTest PRIVATE RenderingGpuTestSupport)
target_include_directories(RHIAccelerationStructureVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
set_target_properties(RHIAccelerationStructureVulkanTest PROPERTIES FOLDER "Test/Core/Rendering")
register_rendering_gpu_test(
    RHIAccelerationStructureVulkanTest
    RHIAccelerationStructureVulkanTest
)
add_executable(RayTracingSceneSnapshotTest
    RayTracingSceneSnapshotTest.cpp
)
target_link_libraries(RayTracingSceneSnapshotTest PRIVATE RenderingGpuTestSupport)
target_include_directories(RayTracingSceneSnapshotTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
set_target_properties(RayTracingSceneSnapshotTest PROPERTIES FOLDER "Test/Core/Rendering")
register_rendering_gpu_test(
    RayTracingSceneSnapshotTest
    RayTracingSceneSnapshotTest
)
add_executable(DDGIProbeRayQueryVulkanTest
    DDGIProbeRayQueryVulkanTest.cpp
)
target_link_libraries(DDGIProbeRayQueryVulkanTest PRIVATE RenderingGpuTestSupport)
target_include_directories(DDGIProbeRayQueryVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
target_compile_definitions(DDGIProbeRayQueryVulkanTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
set_target_properties(DDGIProbeRayQueryVulkanTest PROPERTIES FOLDER "Test/Core/Rendering")
register_rendering_gpu_test(
    DDGIProbeRayQueryVulkanTest
    DDGIProbeRayQueryVulkanTest
)
add_executable(RTGIDiffuseIndirectVulkanTest
    RTGIDiffuseIndirectVulkanTest.cpp
)
target_link_libraries(RTGIDiffuseIndirectVulkanTest PRIVATE RenderingGpuTestSupport)
target_include_directories(RTGIDiffuseIndirectVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
target_compile_definitions(RTGIDiffuseIndirectVulkanTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
set_target_properties(RTGIDiffuseIndirectVulkanTest PROPERTIES FOLDER "Test/Core/Rendering")
register_rendering_gpu_test(
    RTGIDiffuseIndirectVulkanTest
    RTGIDiffuseIndirectVulkanTest
)
add_executable(R8AcesLutToneMappingVulkanTest
    R8AcesLutToneMappingVulkanTest.cpp
)
target_link_libraries(R8AcesLutToneMappingVulkanTest PRIVATE RenderingGpuTestSupport)
target_include_directories(R8AcesLutToneMappingVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
target_compile_definitions(R8AcesLutToneMappingVulkanTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
set_target_properties(R8AcesLutToneMappingVulkanTest PROPERTIES FOLDER "Test/Core/Rendering")
register_rendering_gpu_test(
    R8AcesLutToneMappingVulkanTest
    R8AcesLutToneMappingVulkanTest
)
# R8: フィルムグレイン（既定オフ、強さ0で従来と一致、統計・フレームごとの模様・再実行の一致）を実GPUで確かめる。
add_executable(R8FilmGrainVulkanTest R8FilmGrainVulkanTest.cpp)
target_link_libraries(R8FilmGrainVulkanTest PRIVATE RenderingGpuTestSupport)
target_include_directories(R8FilmGrainVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
target_compile_definitions(R8FilmGrainVulkanTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
set_target_properties(R8FilmGrainVulkanTest PROPERTIES FOLDER "Test/Core/Rendering")
register_rendering_gpu_test(R8FilmGrainVulkanTest R8FilmGrainVulkanTest)
add_executable(R6RTGIAcceptanceVulkanTest
    R6RTGIAcceptanceVulkanTest.cpp
)
target_link_libraries(R6RTGIAcceptanceVulkanTest PRIVATE
    RenderingValidationFixtureSupport
    RenderingGpuTestSupport
    RenderingFloatImageSupport
)
target_include_directories(R6RTGIAcceptanceVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
target_compile_definitions(R6RTGIAcceptanceVulkanTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
set_target_properties(R6RTGIAcceptanceVulkanTest PROPERTIES FOLDER "Test/Core/Rendering")
register_rendering_gpu_test(
    R6RTGIAcceptanceVulkanTest
    R6RTGIAcceptanceVulkanTest
    --scene=indoor
)
add_executable(DDGIProbeRadianceVulkanTest
    DDGIProbeRadianceVulkanTest.cpp
)
target_link_libraries(DDGIProbeRadianceVulkanTest PRIVATE RenderingGpuTestSupport)
target_include_directories(DDGIProbeRadianceVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
target_compile_definitions(DDGIProbeRadianceVulkanTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
set_target_properties(DDGIProbeRadianceVulkanTest PROPERTIES FOLDER "Test/Core/Rendering")
register_rendering_gpu_test(
    DDGIProbeRadianceVulkanTest
    DDGIProbeRadianceVulkanTest
)
add_executable(DDGIProbeUpdateVulkanTest
    DDGIProbeUpdateVulkanTest.cpp
)
target_link_libraries(DDGIProbeUpdateVulkanTest PRIVATE RenderingGpuTestSupport)
target_include_directories(DDGIProbeUpdateVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
target_compile_definitions(DDGIProbeUpdateVulkanTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
set_target_properties(DDGIProbeUpdateVulkanTest PROPERTIES FOLDER "Test/Core/Rendering")
register_rendering_gpu_test(
    DDGIProbeUpdateVulkanTest
    DDGIProbeUpdateVulkanTest
)
add_executable(RenderingDDGIVulkanTest
    RenderingDDGIVulkanTest.cpp
)
target_link_libraries(RenderingDDGIVulkanTest PRIVATE
    RenderingValidationFixtureSupport
    RenderingGpuTestSupport
)
target_include_directories(RenderingDDGIVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
target_compile_definitions(RenderingDDGIVulkanTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
set_target_properties(RenderingDDGIVulkanTest PROPERTIES FOLDER "Test/Core/Rendering")
register_rendering_gpu_test(
    RenderingDDGIVulkanTest
    RenderingDDGIVulkanTest
    --capture-source=scene-color
    --scenario=cornell-reference
)
register_rendering_gpu_test(
    RenderingDDGIVulkanDynamicTest
    RenderingDDGIVulkanTest
    --capture-source=scene-color
    --scenario=dynamic-update
)
add_executable(RHIRayTracingPipelineVulkanTest
    RHIRayTracingPipelineVulkanTest.cpp
)
target_link_libraries(RHIRayTracingPipelineVulkanTest PRIVATE RenderingGpuTestSupport)
target_include_directories(RHIRayTracingPipelineVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
target_compile_definitions(RHIRayTracingPipelineVulkanTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
set_target_properties(RHIRayTracingPipelineVulkanTest PROPERTIES FOLDER "Test/Core/Rendering")
register_rendering_gpu_test(
    RHIRayTracingPipelineVulkanTest
    RHIRayTracingPipelineVulkanTest
)
add_executable(RHIDescriptorArrayVulkanTest
    RHIDescriptorArrayVulkanTest.cpp
)
target_link_libraries(RHIDescriptorArrayVulkanTest PRIVATE RenderingGpuTestSupport)
target_include_directories(RHIDescriptorArrayVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
set_target_properties(RHIDescriptorArrayVulkanTest PROPERTIES FOLDER "Test/Core/Rendering")
register_rendering_gpu_test(
    RHIDescriptorArrayVulkanTest
    RHIDescriptorArrayVulkanTest
)
add_executable(RayTracingCapabilityContractTest
    RayTracingCapabilityContractTest.cpp
)
target_link_libraries(RayTracingCapabilityContractTest PRIVATE RenderingGpuTestSupport)
target_include_directories(RayTracingCapabilityContractTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
set_target_properties(RayTracingCapabilityContractTest PROPERTIES FOLDER "Test/Core/Rendering")
register_rendering_gpu_test(
    RayTracingCapabilityContractTest
    RayTracingCapabilityContractTest
)

add_executable(RHIRayTracingApiContractTest
    RHIRayTracingApiContractTest.cpp
)
target_link_libraries(RHIRayTracingApiContractTest PRIVATE Core)
target_include_directories(RHIRayTracingApiContractTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(RHIRayTracingApiContractTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RHIRayTracingApiContractTest COMMAND RHIRayTracingApiContractTest)

add_executable(RHIRayTracingShaderStageCompileTest
    RHIRayTracingShaderStageCompileTest.cpp
)
target_link_libraries(RHIRayTracingShaderStageCompileTest PRIVATE Core)
target_include_directories(RHIRayTracingShaderStageCompileTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
target_compile_definitions(RHIRayTracingShaderStageCompileTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
set_target_properties(RHIRayTracingShaderStageCompileTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RHIRayTracingShaderStageCompileTest COMMAND RHIRayTracingShaderStageCompileTest)

add_executable(RHITextureToBufferReadbackVulkanTest
    RHITextureToBufferReadbackVulkanTest.cpp
)
target_link_libraries(RHITextureToBufferReadbackVulkanTest PRIVATE Core)
target_include_directories(RHITextureToBufferReadbackVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(RHITextureToBufferReadbackVulkanTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RHITextureToBufferReadbackVulkanTest COMMAND RHITextureToBufferReadbackVulkanTest)
set_tests_properties(RHITextureToBufferReadbackVulkanTest PROPERTIES
    LABELS "GPU;RenderingValidation"
    RESOURCE_LOCK NorvesLibGPU
    SKIP_RETURN_CODE 125
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_executable(RHITextureUpdateVulkanTest
    RHITextureUpdateVulkanTest.cpp
)
target_link_libraries(RHITextureUpdateVulkanTest PRIVATE Core)
target_include_directories(RHITextureUpdateVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(RHITextureUpdateVulkanTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RHITextureUpdateVulkanTest COMMAND RHITextureUpdateVulkanTest)
set_tests_properties(RHITextureUpdateVulkanTest PROPERTIES
    LABELS "GPU;RenderingValidation"
    RESOURCE_LOCK NorvesLibGPU
    SKIP_RETURN_CODE 125
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_executable(RenderingVelocityVulkanTest
    RenderingVelocityVulkanTest.cpp
)
target_link_libraries(RenderingVelocityVulkanTest PRIVATE
    RenderingValidationFixtureSupport
    RenderingGpuTestSupport
)
target_include_directories(RenderingVelocityVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
set_target_properties(RenderingVelocityVulkanTest PROPERTIES FOLDER "Test/Core/Rendering")
register_rendering_gpu_test(
    RenderingVelocityStaticVulkanTest
    RenderingVelocityVulkanTest
    --scene=indoor
    --capture-source=gbuffer-velocity
    --scenario=static)
register_rendering_gpu_test(
    RenderingVelocityMotionVulkanTest
    RenderingVelocityVulkanTest
    --scene=outdoor
    --capture-source=gbuffer-velocity
    --scenario=camera-object-motion)
register_rendering_gpu_test(
    RenderingVelocityCameraVulkanTest
    RenderingVelocityVulkanTest
    --scene=outdoor
    --capture-source=gbuffer-velocity
    --scenario=camera-motion)
register_rendering_gpu_test(
    RenderingVelocityObjectVulkanTest
    RenderingVelocityVulkanTest
    --scene=outdoor
    --capture-source=gbuffer-velocity
    --scenario=object-motion)
register_rendering_gpu_test(
    RenderingVelocityFirstFrameVulkanTest
    RenderingVelocityVulkanTest
    --scene=indoor
    --capture-source=gbuffer-velocity
    --scenario=first-frame-invalid-history)
register_rendering_gpu_test(
    RenderingVelocityMoveThenStopVulkanTest
    RenderingVelocityVulkanTest
    --scene=outdoor
    --capture-source=gbuffer-velocity
    --scenario=move-then-stop)

add_executable(RHIImageLayoutVulkanValidationTest
    RHIImageLayoutVulkanValidationTest.cpp
)
target_link_libraries(RHIImageLayoutVulkanValidationTest PRIVATE
    RenderingValidationFixtureSupport
    RenderingGpuTestSupport
)
target_include_directories(RHIImageLayoutVulkanValidationTest PRIVATE
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
target_compile_definitions(RHIImageLayoutVulkanValidationTest PRIVATE
    NORVES_BINARY_ROOT="${CMAKE_BINARY_DIR}"
)
set_target_properties(RHIImageLayoutVulkanValidationTest PROPERTIES FOLDER "Test/Core/Rendering")
register_rendering_gpu_test(
    RHIImageLayoutVulkanValidationTest
    RHIImageLayoutVulkanValidationTest
    --scene=indoor
    --image-layout-case=micro)
register_rendering_gpu_test(
    RHIImageLayoutVulkanNoEligibleLightSceneTest
    RHIImageLayoutVulkanValidationTest
    --scene=indoor
    --image-layout-case=no-eligible-light)
register_rendering_gpu_test(
    RHIImageLayoutVulkanNoCasterSceneTest
    RHIImageLayoutVulkanValidationTest
    --scene=outdoor
    --image-layout-case=no-caster)
register_rendering_gpu_test(
    RHIImageLayoutVulkanDrawSceneTest
    RHIImageLayoutVulkanValidationTest
    --scene=outdoor
    --image-layout-case=draw)
register_rendering_gpu_test(
    RHIImageLayoutVulkanDrawThenNoCasterSceneTest
    RHIImageLayoutVulkanValidationTest
    --scene=outdoor
    --image-layout-case=draw-then-no-caster)

add_executable(FrameCaptureReadbackHelperTest
    FrameCaptureReadbackHelperTest.cpp
)
target_link_libraries(FrameCaptureReadbackHelperTest PRIVATE Core)
target_include_directories(FrameCaptureReadbackHelperTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
set_target_properties(FrameCaptureReadbackHelperTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME FrameCaptureReadbackHelperTest COMMAND FrameCaptureReadbackHelperTest)

add_library(RenderingFloatImageSupport STATIC
    RenderingValidation/RenderingFloatImage.cpp
)
target_link_libraries(RenderingFloatImageSupport PUBLIC Core)
target_include_directories(RenderingFloatImageSupport PUBLIC
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
set_target_properties(RenderingFloatImageSupport PROPERTIES FOLDER "Test/Core/Rendering")

add_executable(RenderingFloatImageTest
    RenderingFloatImageTest.cpp
)
target_link_libraries(RenderingFloatImageTest PRIVATE RenderingFloatImageSupport)
target_include_directories(RenderingFloatImageTest PRIVATE
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
set_target_properties(RenderingFloatImageTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RenderingFloatImageTest COMMAND RenderingFloatImageTest)

add_executable(FrameCaptureFloatReadbackVulkanTest
    FrameCaptureFloatReadbackVulkanTest.cpp
)
target_link_libraries(FrameCaptureFloatReadbackVulkanTest PRIVATE
    RenderingFloatImageSupport
    RenderingGpuTestSupport
)
target_include_directories(FrameCaptureFloatReadbackVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
set_target_properties(FrameCaptureFloatReadbackVulkanTest PROPERTIES FOLDER "Test/Core/Rendering")
register_rendering_gpu_test(
    FrameCaptureFloatReadbackVulkanTest
    FrameCaptureFloatReadbackVulkanTest)

add_executable(RenderingHdrSceneCaptureTest
    RenderingHdrSceneCaptureTest.cpp
)
target_link_libraries(RenderingHdrSceneCaptureTest PRIVATE
    RenderingValidationFixtureSupport
    RenderingGpuTestSupport
    RenderingFloatImageSupport
    NorvesModule_ImGui
)
target_include_directories(RenderingHdrSceneCaptureTest PRIVATE
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
    ${CMAKE_SOURCE_DIR}/Library/ThirdParty/imgui
)
set_target_properties(RenderingHdrSceneCaptureTest PROPERTIES FOLDER "Test/Core/Rendering")
register_rendering_gpu_test(
    RenderingHdrIndoorSceneVulkanTest
    RenderingHdrSceneCaptureTest
    --scene=indoor
    --capture-source=scene-color)
register_rendering_gpu_test(
    RenderingHdrOutdoorSceneVulkanTest
    RenderingHdrSceneCaptureTest
    --scene=outdoor
    --capture-source=scene-color)

add_executable(RenderingRayTracingShadowVulkanTest
    RenderingRayTracingShadowVulkanTest.cpp
)
add_executable(PathTracingVulkanTest
    PathTracingVulkanTest.cpp
)
target_link_libraries(PathTracingVulkanTest PRIVATE Core RenderingGpuTestSupport)
target_include_directories(PathTracingVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private)
target_compile_definitions(PathTracingVulkanTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
set_target_properties(PathTracingVulkanTest PROPERTIES FOLDER "Test/Core/Rendering")
register_rendering_gpu_test(PathTracingVulkanTest PathTracingVulkanTest)
add_executable(PathTracingMaterialVulkanTest
    PathTracingMaterialVulkanTest.cpp
)
target_link_libraries(PathTracingMaterialVulkanTest PRIVATE Core RenderingGpuTestSupport)
target_include_directories(PathTracingMaterialVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private)
target_compile_definitions(PathTracingMaterialVulkanTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
set_target_properties(PathTracingMaterialVulkanTest PROPERTIES FOLDER "Test/Core/Rendering")
register_rendering_gpu_test(PathTracingMaterialVulkanTest PathTracingMaterialVulkanTest)
add_executable(PathTracingLightingVulkanTest
    PathTracingLightingVulkanTest.cpp
)
target_link_libraries(PathTracingLightingVulkanTest PRIVATE Core RenderingGpuTestSupport)
target_include_directories(PathTracingLightingVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private)
target_compile_definitions(PathTracingLightingVulkanTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
set_target_properties(PathTracingLightingVulkanTest PROPERTIES FOLDER "Test/Core/Rendering")
register_rendering_gpu_test(PathTracingLightingVulkanTest PathTracingLightingVulkanTest)
add_executable(PathTracingConvergenceVulkanTest
    PathTracingConvergenceVulkanTest.cpp
)
target_link_libraries(PathTracingConvergenceVulkanTest PRIVATE
    Core RenderingGpuTestSupport RenderingFloatImageSupport)
target_include_directories(PathTracingConvergenceVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private)
target_compile_definitions(PathTracingConvergenceVulkanTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
set_target_properties(PathTracingConvergenceVulkanTest PROPERTIES FOLDER "Test/Core/Rendering")
register_rendering_gpu_test(PathTracingConvergenceVulkanTest PathTracingConvergenceVulkanTest)
add_executable(PathTracingOutdoorVulkanTest PathTracingVulkanTest.cpp)
target_link_libraries(PathTracingOutdoorVulkanTest PRIVATE Core RenderingGpuTestSupport)
target_include_directories(PathTracingOutdoorVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private)
target_compile_definitions(PathTracingOutdoorVulkanTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    NORVES_PATH_TRACING_OUTDOOR_TEST)
set_target_properties(PathTracingOutdoorVulkanTest PROPERTIES FOLDER "Test/Core/Rendering")
register_rendering_gpu_test(PathTracingOutdoorVulkanTest PathTracingOutdoorVulkanTest)
add_executable(PathTracingVolumetricTest PathTracingVulkanTest.cpp)
target_link_libraries(PathTracingVolumetricTest PRIVATE Core RenderingGpuTestSupport)
target_include_directories(PathTracingVolumetricTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private)
target_compile_definitions(PathTracingVolumetricTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    NORVES_PATH_TRACING_VOLUMETRIC_TEST)
set_target_properties(PathTracingVolumetricTest PROPERTIES FOLDER "Test/Core/Rendering")
register_rendering_gpu_test(PathTracingVolumetricTest PathTracingVolumetricTest)
add_executable(PathTracingCameraTest PathTracingCameraTest.cpp)
target_link_libraries(PathTracingCameraTest PRIVATE Core)
set_target_properties(PathTracingCameraTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME PathTracingCameraTest COMMAND PathTracingCameraTest)
add_executable(PathTracingCameraVulkanTest PathTracingCameraVulkanTest.cpp)
target_link_libraries(PathTracingCameraVulkanTest PRIVATE Core RenderingGpuTestSupport)
target_include_directories(PathTracingCameraVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private)
target_compile_definitions(PathTracingCameraVulkanTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
set_target_properties(PathTracingCameraVulkanTest PROPERTIES FOLDER "Test/Core/Rendering")
register_rendering_gpu_test(PathTracingCameraVulkanTest PathTracingCameraVulkanTest)
# R8: PTの連番の1フレーム（前後のカメラ・変換とシャッター区間の固定、動きぼけの幅、CoC、履歴の継続）を実GPUで検証する。
add_executable(R8PathTracingSequenceFrameVulkanTest R8PathTracingSequenceFrameVulkanTest.cpp)
target_link_libraries(R8PathTracingSequenceFrameVulkanTest PRIVATE Core RenderingGpuTestSupport)
target_include_directories(R8PathTracingSequenceFrameVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private)
target_compile_definitions(R8PathTracingSequenceFrameVulkanTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
set_target_properties(R8PathTracingSequenceFrameVulkanTest PROPERTIES FOLDER "Test/Core/Rendering")
register_rendering_gpu_test(R8PathTracingSequenceFrameVulkanTest R8PathTracingSequenceFrameVulkanTest)
# R8: EXR連番の検査（欠番・寸法・非有限の画素と、ACES 2.0 SDRのLUTで変換した隣接フレームのLDR-FLIPによるポッピング）。
add_library(R8ExrSequenceValidationSupport STATIC R8ExrSequenceValidation.cpp)
target_link_libraries(R8ExrSequenceValidationSupport PUBLIC
    RenderingFloatImageSupport
    RenderingPerceptualDiffSupport
    NorvesThirdParty_TinyEXR)
target_include_directories(R8ExrSequenceValidationSupport PUBLIC
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
    ${CMAKE_SOURCE_DIR}/Library/ThirdParty/tinyexr/include)
set_target_properties(R8ExrSequenceValidationSupport PROPERTIES FOLDER "Test/Core/Rendering")
add_executable(R8ExrSequenceValidator R8ExrSequenceValidatorMain.cpp)
target_link_libraries(R8ExrSequenceValidator PRIVATE R8ExrSequenceValidationSupport)
target_compile_definitions(R8ExrSequenceValidator PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
set_target_properties(R8ExrSequenceValidator PROPERTIES FOLDER "Test/Core/Rendering")
add_executable(R8ExrSequenceValidatorTest R8ExrSequenceValidatorTest.cpp)
target_link_libraries(R8ExrSequenceValidatorTest PRIVATE R8ExrSequenceValidationSupport)
target_compile_definitions(R8ExrSequenceValidatorTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    NORVES_BINARY_ROOT="${CMAKE_BINARY_DIR}")
set_target_properties(R8ExrSequenceValidatorTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME R8ExrSequenceValidatorTest COMMAND R8ExrSequenceValidatorTest)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(PATH_TRACING_EXR_OUTPUT_ROOT
    "${CMAKE_BINARY_DIR}/RenderingValidation/PathTracingExrOutputRuns")
file(MAKE_DIRECTORY
    "${PATH_TRACING_EXR_OUTPUT_ROOT}/first"
    "${PATH_TRACING_EXR_OUTPUT_ROOT}/second"
    "${PATH_TRACING_EXR_OUTPUT_ROOT}/known")
add_executable(PathTracingExrOutputTest PathTracingCameraVulkanTest.cpp)
target_link_libraries(PathTracingExrOutputTest PRIVATE Core RenderingGpuTestSupport)
target_include_directories(PathTracingExrOutputTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private)
target_compile_definitions(PathTracingExrOutputTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    NORVES_EXR_OUTPUT_TEST
    NORVES_EXR_OUTPUT_ROOT="${PATH_TRACING_EXR_OUTPUT_ROOT}")
set_target_properties(PathTracingExrOutputTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME PathTracingExrOutputTest
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/PathTracingExrOutputTest.py
            $<TARGET_FILE:PathTracingExrOutputTest>
            ${PATH_TRACING_EXR_OUTPUT_ROOT})
set_tests_properties(PathTracingExrOutputTest PROPERTIES
    LABELS "GPU;RenderingValidation"
    RESOURCE_LOCK NorvesLibGPU
    SKIP_RETURN_CODE 125
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
# R1の白炉と既知光度の行をラスタとパストレーサーで取得し、同じ行のSceneColorを比べる。
set(PATH_TRACING_RASTER_PARITY_ROOT
    "${CMAKE_BINARY_DIR}/RenderingValidation/PathTracingRasterParityRuns")
add_custom_target(PathTracingRasterParityVulkanTest DEPENDS RenderingHdrSceneCaptureTest)
set_target_properties(PathTracingRasterParityVulkanTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME PathTracingRasterParityVulkanTest
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/PathTracingRasterParityTest.py
            $<TARGET_FILE:RenderingHdrSceneCaptureTest>
            ${PATH_TRACING_RASTER_PARITY_ROOT})
set_tests_properties(PathTracingRasterParityVulkanTest PROPERTIES
    LABELS "GPU;RenderingValidation"
    RESOURCE_LOCK NorvesLibGPU
    SKIP_RETURN_CODE 125
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
# ラスタとPTの同一シーンの画像をLDR-FLIPで比べる部品（R6参照比較とR7屋外比較が共有する）。
add_library(RenderingReferenceComparisonSupport STATIC
    RenderingValidation/RasterPathTracingComparison.cpp
)
target_link_libraries(RenderingReferenceComparisonSupport PUBLIC
    RenderingFloatImageSupport
    RenderingPerceptualDiffSupport
)
target_include_directories(RenderingReferenceComparisonSupport PUBLIC
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
set_target_properties(RenderingReferenceComparisonSupport PROPERTIES FOLDER "Test/Core/Rendering")
# 比較の部品のCPU契約（3組の中央値の選び方、太陽の可視による除外を影の縁の近くに限ること）。
add_executable(RasterPathTracingComparisonTest
    RasterPathTracingComparisonTest.cpp
)
target_link_libraries(RasterPathTracingComparisonTest PRIVATE RenderingReferenceComparisonSupport)
set_target_properties(RasterPathTracingComparisonTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RasterPathTracingComparisonTest COMMAND RasterPathTracingComparisonTest)
# R6 RTGIの静止収束SceneColorを、同じCornellシーンの自前PT参照とLDR-FLIPで比べる。
add_executable(R6RTGIPathTracingReferenceVulkanTest
    R6RTGIPathTracingReferenceVulkanTest.cpp
)
target_link_libraries(R6RTGIPathTracingReferenceVulkanTest PRIVATE
    RenderingValidationFixtureSupport
    RenderingGpuTestSupport
    RenderingFloatImageSupport
    RenderingPerceptualDiffSupport
    RenderingReferenceComparisonSupport
)
target_include_directories(R6RTGIPathTracingReferenceVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
set_target_properties(R6RTGIPathTracingReferenceVulkanTest PROPERTIES FOLDER "Test/Core/Rendering")
set(R6_RTGI_PATH_TRACING_REFERENCE_ROOT
    "${CMAKE_BINARY_DIR}/RenderingValidation/R6RTGIPathTracingReferenceRuns")
add_test(NAME R6RTGIPathTracingReferenceVulkanTest
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/R6RTGIPathTracingReferenceTest.py
            $<TARGET_FILE:R6RTGIPathTracingReferenceVulkanTest>
            ${R6_RTGI_PATH_TRACING_REFERENCE_ROOT})
set_tests_properties(R6RTGIPathTracingReferenceVulkanTest PROPERTIES
    LABELS "GPU;RenderingValidation"
    RESOURCE_LOCK NorvesLibGPU
    SKIP_RETURN_CODE 125
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
# 空・霧を含む屋外シーンのラスタとPTを、R2と同じ3時刻でLDR-FLIPで比べる（R7-O3）。
add_executable(R7OutdoorPathTracingReferenceVulkanTest
    R7OutdoorPathTracingReferenceVulkanTest.cpp
)
target_link_libraries(R7OutdoorPathTracingReferenceVulkanTest PRIVATE
    RenderingValidationFixtureSupport
    RenderingGpuTestSupport
    RenderingReferenceComparisonSupport
)
target_include_directories(R7OutdoorPathTracingReferenceVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
set_target_properties(R7OutdoorPathTracingReferenceVulkanTest PROPERTIES FOLDER "Test/Core/Rendering")
set(R7_OUTDOOR_PATH_TRACING_REFERENCE_ROOT
    "${CMAKE_BINARY_DIR}/RenderingValidation/R7OutdoorPathTracingReferenceRuns")
add_test(NAME R7OutdoorPathTracingReferenceVulkanTest
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/R7OutdoorPathTracingReferenceTest.py
            $<TARGET_FILE:R7OutdoorPathTracingReferenceVulkanTest>
            ${R7_OUTDOOR_PATH_TRACING_REFERENCE_ROOT})
set_tests_properties(R7OutdoorPathTracingReferenceVulkanTest PROPERTIES
    LABELS "GPU;RenderingValidation"
    RESOURCE_LOCK NorvesLibGPU
    SKIP_RETURN_CODE 125
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
# ラスタの被写界深度（DepthOfFieldPass）を、PTのピンホール像を入力に実GPUで掛け、PTの薄レンズ参照とLDR-FLIPで比べる（R8-P4）。
add_executable(R8DepthOfFieldPathTracingReferenceVulkanTest
    R8DepthOfFieldPathTracingReferenceVulkanTest.cpp
)
target_link_libraries(R8DepthOfFieldPathTracingReferenceVulkanTest PRIVATE
    RenderingValidationFixtureSupport
    RenderingGpuTestSupport
    RenderingFloatImageSupport
    RenderingPerceptualDiffSupport
    RenderingReferenceComparisonSupport
)
target_include_directories(R8DepthOfFieldPathTracingReferenceVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
target_compile_definitions(R8DepthOfFieldPathTracingReferenceVulkanTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
set_target_properties(R8DepthOfFieldPathTracingReferenceVulkanTest PROPERTIES FOLDER "Test/Core/Rendering")
set(R8_DEPTH_OF_FIELD_PATH_TRACING_REFERENCE_ROOT
    "${CMAKE_BINARY_DIR}/RenderingValidation/R8DepthOfFieldPathTracingReferenceRuns")
add_test(NAME R8DepthOfFieldPathTracingReferenceVulkanTest
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/R8DepthOfFieldPathTracingReferenceTest.py
            $<TARGET_FILE:R8DepthOfFieldPathTracingReferenceVulkanTest>
            ${R8_DEPTH_OF_FIELD_PATH_TRACING_REFERENCE_ROOT})
set_tests_properties(R8DepthOfFieldPathTracingReferenceVulkanTest PROPERTIES
    LABELS "GPU;RenderingValidation"
    RESOURCE_LOCK NorvesLibGPU
    SKIP_RETURN_CODE 125
    TIMEOUT 5400
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
# ラスタの動きぼけ（MotionBlurPass）を、PTのシャッター0の像とラスタの画面velocityを入力に実GPUで掛け、PTのシャッター参照とLDR-FLIPで比べる（R8-P5）。
add_executable(R8MotionBlurPathTracingReferenceVulkanTest
    R8MotionBlurPathTracingReferenceVulkanTest.cpp
)
target_link_libraries(R8MotionBlurPathTracingReferenceVulkanTest PRIVATE
    RenderingValidationFixtureSupport
    RenderingGpuTestSupport
    RenderingFloatImageSupport
    RenderingPerceptualDiffSupport
    RenderingReferenceComparisonSupport
)
target_include_directories(R8MotionBlurPathTracingReferenceVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
target_compile_definitions(R8MotionBlurPathTracingReferenceVulkanTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
set_target_properties(R8MotionBlurPathTracingReferenceVulkanTest PROPERTIES FOLDER "Test/Core/Rendering")
set(R8_MOTION_BLUR_PATH_TRACING_REFERENCE_ROOT
    "${CMAKE_BINARY_DIR}/RenderingValidation/R8MotionBlurPathTracingReferenceRuns")
add_test(NAME R8MotionBlurPathTracingReferenceVulkanTest
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/R8MotionBlurPathTracingReferenceTest.py
            $<TARGET_FILE:R8MotionBlurPathTracingReferenceVulkanTest>
            ${R8_MOTION_BLUR_PATH_TRACING_REFERENCE_ROOT})
set_tests_properties(R8MotionBlurPathTracingReferenceVulkanTest PROPERTIES
    LABELS "GPU;RenderingValidation"
    RESOURCE_LOCK NorvesLibGPU
    SKIP_RETURN_CODE 125
    TIMEOUT 5400
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
# R8: 屋内・屋外の決定論的なアニメーションをPTの連番の経路でEXR連番へ書き出すexeと、8フレームの書き出し・検査。
add_executable(R8SequenceRenderer R8SequenceRenderer.cpp)
target_link_libraries(R8SequenceRenderer PRIVATE
    RenderingValidationFixtureSupport
    RenderingGpuTestSupport
    RenderingFloatImageSupport
)
target_include_directories(R8SequenceRenderer PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
target_compile_definitions(R8SequenceRenderer PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
set_target_properties(R8SequenceRenderer PROPERTIES FOLDER "Test/Core/Rendering")
set(R8_SEQUENCE_SMOKE_ROOT "${CMAKE_BINARY_DIR}/RenderingValidation/R8SequenceSmokeRuns")
add_test(NAME R8SequenceSmokeTest
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/R8SequenceSmokeTest.py
            $<TARGET_FILE:R8SequenceRenderer>
            $<TARGET_FILE:R8ExrSequenceValidator>
            ${R8_SEQUENCE_SMOKE_ROOT})
set_tests_properties(R8SequenceSmokeTest PROPERTIES
    LABELS "GPU;RenderingValidation"
    RESOURCE_LOCK NorvesLibGPU
    SKIP_RETURN_CODE 125
    TIMEOUT 1800
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
add_executable(R4DDGIPathTracingReferenceVulkanTest
    R4DDGIPathTracingReferenceVulkanTest.cpp
)
target_link_libraries(R4DDGIPathTracingReferenceVulkanTest PRIVATE
    RenderingValidationFixtureSupport
    RenderingGpuTestSupport
    RenderingFloatImageSupport
)
target_include_directories(R4DDGIPathTracingReferenceVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
target_compile_definitions(R4DDGIPathTracingReferenceVulkanTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
set_target_properties(R4DDGIPathTracingReferenceVulkanTest PROPERTIES FOLDER "Test/Core/Rendering")
set(R4_DDGI_PATH_TRACING_REFERENCE_ROOT
    "${CMAKE_BINARY_DIR}/RenderingValidation/R4DDGIPathTracingReferenceRuns")
add_test(NAME R4DDGIPathTracingReferenceVulkanTest
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/R4DDGIPathTracingReferenceTest.py
            $<TARGET_FILE:R4DDGIPathTracingReferenceVulkanTest>
            ${R4_DDGI_PATH_TRACING_REFERENCE_ROOT})
set_tests_properties(R4DDGIPathTracingReferenceVulkanTest PROPERTIES
    LABELS "GPU;RenderingValidation"
    RESOURCE_LOCK NorvesLibGPU
    SKIP_RETURN_CODE 125
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
target_link_libraries(RenderingRayTracingShadowVulkanTest PRIVATE
    RenderingValidationFixtureSupport
    RenderingGpuTestSupport
)
target_include_directories(RenderingRayTracingShadowVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
set_target_properties(RenderingRayTracingShadowVulkanTest PROPERTIES FOLDER "Test/Core/Rendering")
register_rendering_gpu_test(
    RenderingRayTracingShadowVulkanTest
    RenderingRayTracingShadowVulkanTest
    --scene=outdoor
    --capture-source=back-buffer
    --scenario=raster-rt-shadow-ab)
register_rendering_gpu_test(
    RenderingRayTracingShadowDynamicVulkanTest
    RenderingRayTracingShadowVulkanTest
    --scene=outdoor
    --capture-source=back-buffer
    --scenario=dynamic-occluder-fallback)

add_executable(FrameCaptureThumbnailEncoderTest
    FrameCaptureThumbnailEncoderTest.cpp
)
target_link_libraries(FrameCaptureThumbnailEncoderTest PRIVATE Core)
target_include_directories(FrameCaptureThumbnailEncoderTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/ThirdParty/stb
)
set_target_properties(FrameCaptureThumbnailEncoderTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME FrameCaptureThumbnailEncoderTest COMMAND FrameCaptureThumbnailEncoderTest)

add_library(RenderingGoldenImageSupport STATIC
    RenderingValidation/RenderingGoldenImage.cpp
)
target_link_libraries(RenderingGoldenImageSupport PUBLIC Core)
target_include_directories(RenderingGoldenImageSupport
    PUBLIC ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
    PRIVATE ${CMAKE_SOURCE_DIR}/Library/ThirdParty/stb
)
set_target_properties(RenderingGoldenImageSupport PROPERTIES FOLDER "Test/Core/Rendering")

add_library(RenderingPerceptualDiffSupport STATIC
    RenderingValidation/RenderingPerceptualDiff.cpp
)
target_link_libraries(RenderingPerceptualDiffSupport PUBLIC RenderingGoldenImageSupport)
target_include_directories(RenderingPerceptualDiffSupport PUBLIC
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
target_include_directories(RenderingPerceptualDiffSupport SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/Library/ThirdParty/flip
)
set_target_properties(RenderingPerceptualDiffSupport PROPERTIES FOLDER "Test/Core/Rendering")

add_executable(RenderingPerceptualDiffTest
    RenderingPerceptualDiffTest.cpp
)
target_link_libraries(RenderingPerceptualDiffTest PRIVATE RenderingPerceptualDiffSupport)
target_include_directories(RenderingPerceptualDiffTest PRIVATE
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
target_compile_definitions(RenderingPerceptualDiffTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    NORVES_BINARY_ROOT="${CMAKE_BINARY_DIR}"
)
set_target_properties(RenderingPerceptualDiffTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RenderingPerceptualDiffTest COMMAND RenderingPerceptualDiffTest)
add_test(NAME RenderingPerceptualArtificialDifferenceTest
    COMMAND RenderingPerceptualDiffTest --validate-artificial-baselines)

add_executable(RenderingGoldenImageComparatorTest
    RenderingGoldenImageComparatorTest.cpp
)
target_link_libraries(RenderingGoldenImageComparatorTest PRIVATE RenderingGoldenImageSupport)
target_include_directories(RenderingGoldenImageComparatorTest PRIVATE
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
target_compile_definitions(RenderingGoldenImageComparatorTest PRIVATE
    NORVES_BINARY_ROOT="${CMAKE_BINARY_DIR}"
)
set_target_properties(RenderingGoldenImageComparatorTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RenderingGoldenImageComparatorTest COMMAND RenderingGoldenImageComparatorTest)

add_executable(RenderingGoldenImageTest
    RenderingGoldenImageTest.cpp
)
target_link_libraries(RenderingGoldenImageTest PRIVATE
    RenderingValidationFixtureSupport
    RenderingGpuTestSupport
    RenderingGoldenImageSupport
    RenderingPerceptualDiffSupport
)
target_include_directories(RenderingGoldenImageTest PRIVATE
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
target_compile_definitions(RenderingGoldenImageTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    NORVES_BINARY_ROOT="${CMAKE_BINARY_DIR}"
)
set_target_properties(RenderingGoldenImageTest PROPERTIES FOLDER "Test/Core/Rendering")

register_rendering_gpu_test(
    RenderingGoldenIndoorVulkanTest
    RenderingGoldenImageTest
    --scene=indoor
    --capture-source=back-buffer)
register_rendering_gpu_test(
    RenderingGoldenOutdoorVulkanTest
    RenderingGoldenImageTest
    --scene=outdoor
    --capture-source=back-buffer)

add_executable(RenderingCoordinatorStatsPropagationTest
    RenderingCoordinatorStatsPropagationTest.cpp
)
target_link_libraries(RenderingCoordinatorStatsPropagationTest PRIVATE Core)
target_include_directories(RenderingCoordinatorStatsPropagationTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
set_target_properties(RenderingCoordinatorStatsPropagationTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RenderingCoordinatorStatsPropagationTest COMMAND RenderingCoordinatorStatsPropagationTest)

add_executable(RenderingCoordinatorDiagnosticsSnapshotTest
    RenderingCoordinatorDiagnosticsSnapshotTest.cpp
)
target_link_libraries(RenderingCoordinatorDiagnosticsSnapshotTest PRIVATE Core)
target_include_directories(RenderingCoordinatorDiagnosticsSnapshotTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
set_target_properties(RenderingCoordinatorDiagnosticsSnapshotTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RenderingCoordinatorDiagnosticsSnapshotTest COMMAND RenderingCoordinatorDiagnosticsSnapshotTest)

add_executable(MeshBatcherTest
    MeshBatcherTest.cpp
)
target_link_libraries(MeshBatcherTest PRIVATE Core)
target_include_directories(MeshBatcherTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(MeshBatcherTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME MeshBatcherTest COMMAND MeshBatcherTest)

add_executable(MeshBatcherInstancingTest
    MeshBatcherInstancingTest.cpp
)
target_link_libraries(MeshBatcherInstancingTest PRIVATE Core)
target_include_directories(MeshBatcherInstancingTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(MeshBatcherInstancingTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME MeshBatcherInstancingTest COMMAND MeshBatcherInstancingTest)

add_executable(InstanceDataFlattenTest
    InstanceDataFlattenTest.cpp
)
target_link_libraries(InstanceDataFlattenTest PRIVATE Core)
target_include_directories(InstanceDataFlattenTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(InstanceDataFlattenTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME InstanceDataFlattenTest COMMAND InstanceDataFlattenTest)

add_executable(DrawCommandSorterTest
    DrawCommandSorterTest.cpp
)
target_link_libraries(DrawCommandSorterTest PRIVATE Core)
target_include_directories(DrawCommandSorterTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(DrawCommandSorterTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME DrawCommandSorterTest COMMAND DrawCommandSorterTest)

add_executable(GBufferMaterialDescriptorCacheTest
    GBufferMaterialDescriptorCacheTest.cpp
)
target_link_libraries(GBufferMaterialDescriptorCacheTest PRIVATE Core)
target_include_directories(GBufferMaterialDescriptorCacheTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
set_target_properties(GBufferMaterialDescriptorCacheTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME GBufferMaterialDescriptorCacheTest COMMAND GBufferMaterialDescriptorCacheTest)

add_executable(ViewScreenTopologyTest
    ViewScreenTopologyTest.cpp
)
target_link_libraries(ViewScreenTopologyTest PRIVATE Core)
target_include_directories(ViewScreenTopologyTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(ViewScreenTopologyTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME ViewScreenTopologyTest COMMAND ViewScreenTopologyTest)

add_executable(SceneViewViewportCommandTest
    SceneViewViewportCommandTest.cpp
)
target_link_libraries(SceneViewViewportCommandTest PRIVATE Core)
target_include_directories(SceneViewViewportCommandTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(SceneViewViewportCommandTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME SceneViewViewportCommandTest COMMAND SceneViewViewportCommandTest)

add_executable(CameraViewConstantsTest
    CameraViewConstantsTest.cpp
)
target_link_libraries(CameraViewConstantsTest PRIVATE Core)
target_include_directories(CameraViewConstantsTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(CameraViewConstantsTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME CameraViewConstantsTest COMMAND CameraViewConstantsTest)

add_executable(CameraPickingTest
    CameraPickingTest.cpp
)
target_link_libraries(CameraPickingTest PRIVATE Core)
target_include_directories(CameraPickingTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(CameraPickingTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME CameraPickingTest COMMAND CameraPickingTest)

add_executable(CameraTableTest
    CameraTableTest.cpp
)
target_link_libraries(CameraTableTest PRIVATE Core)
target_include_directories(CameraTableTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(CameraTableTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME CameraTableTest COMMAND CameraTableTest)

add_executable(ViewportCameraIdRenderPlanTest
    ViewportCameraIdRenderPlanTest.cpp
)
target_link_libraries(ViewportCameraIdRenderPlanTest PRIVATE Core)
target_include_directories(ViewportCameraIdRenderPlanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(ViewportCameraIdRenderPlanTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME ViewportCameraIdRenderPlanTest COMMAND ViewportCameraIdRenderPlanTest)

add_executable(OrthographicViewportMatrixTest
    OrthographicViewportMatrixTest.cpp
)
target_link_libraries(OrthographicViewportMatrixTest PRIVATE Core)
target_include_directories(OrthographicViewportMatrixTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(OrthographicViewportMatrixTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME OrthographicViewportMatrixTest COMMAND OrthographicViewportMatrixTest)

add_executable(ViewRenderContextActiveInputTest
    ViewRenderContextActiveInputTest.cpp
)
target_link_libraries(ViewRenderContextActiveInputTest PRIVATE Core)
target_include_directories(ViewRenderContextActiveInputTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(ViewRenderContextActiveInputTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME ViewRenderContextActiveInputTest COMMAND ViewRenderContextActiveInputTest)

add_executable(RenderingDDGILightingContractTest
    RenderingDDGILightingContractTest.cpp
)
target_link_libraries(RenderingDDGILightingContractTest PRIVATE Core)
target_include_directories(RenderingDDGILightingContractTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
target_compile_definitions(RenderingDDGILightingContractTest PRIVATE
    NORVES_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
set_target_properties(RenderingDDGILightingContractTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RenderingDDGILightingContractTest COMMAND RenderingDDGILightingContractTest)
add_executable(LightingLightBufferTest
    LightingLightBufferTest.cpp
)
target_link_libraries(LightingLightBufferTest PRIVATE Core)
target_include_directories(LightingLightBufferTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
target_compile_definitions(LightingLightBufferTest PRIVATE
    NORVES_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
set_target_properties(LightingLightBufferTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME LightingLightBufferTest COMMAND LightingLightBufferTest)

add_executable(DirectionalShadowLightMatricesTest
    DirectionalShadowLightMatricesTest.cpp
)
target_link_libraries(DirectionalShadowLightMatricesTest PRIVATE Core)
target_include_directories(DirectionalShadowLightMatricesTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
set_target_properties(DirectionalShadowLightMatricesTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME DirectionalShadowLightMatricesTest COMMAND DirectionalShadowLightMatricesTest)
add_executable(CascadedShadowLightMatricesTest
    CascadedShadowLightMatricesTest.cpp
)
target_link_libraries(CascadedShadowLightMatricesTest PRIVATE Core)
target_include_directories(CascadedShadowLightMatricesTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
set_target_properties(CascadedShadowLightMatricesTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME CascadedShadowLightMatricesTest COMMAND CascadedShadowLightMatricesTest)

add_executable(SkyAtmosphereModelTest
    SkyAtmosphereModelTest.cpp
)
target_link_libraries(SkyAtmosphereModelTest PRIVATE Core)
target_include_directories(SkyAtmosphereModelTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(SkyAtmosphereModelTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME SkyAtmosphereModelTest COMMAND SkyAtmosphereModelTest)

add_executable(VolumetricFogModelTest
    VolumetricFogModelTest.cpp
)
target_link_libraries(VolumetricFogModelTest PRIVATE Core)
target_include_directories(VolumetricFogModelTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(VolumetricFogModelTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME VolumetricFogModelTest COMMAND VolumetricFogModelTest)

add_executable(DDGIVolumeModelTest
    DDGIVolumeModelTest.cpp
)
target_link_libraries(DDGIVolumeModelTest PRIVATE Core)
target_include_directories(DDGIVolumeModelTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(DDGIVolumeModelTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME DDGIVolumeModelTest COMMAND DDGIVolumeModelTest)

add_executable(DDGISnapshotContractTest
    DDGISnapshotContractTest.cpp
)
target_link_libraries(DDGISnapshotContractTest PRIVATE Core RenderingGpuTestSupport)
target_include_directories(DDGISnapshotContractTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
set_target_properties(DDGISnapshotContractTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME DDGISnapshotContractTest COMMAND DDGISnapshotContractTest)
set_tests_properties(DDGISnapshotContractTest PROPERTIES
    LABELS "GPU;RenderingValidation"
    RESOURCE_LOCK NorvesLibGPU
    SKIP_RETURN_CODE 125
)

add_executable(DebugViewModeStringTest
    DebugViewModeStringTest.cpp
)
target_link_libraries(DebugViewModeStringTest PRIVATE Core)
target_include_directories(DebugViewModeStringTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(DebugViewModeStringTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME DebugViewModeStringTest COMMAND DebugViewModeStringTest)

add_executable(DebugDrawQueueTest
    DebugDrawQueueTest.cpp
)
target_link_libraries(DebugDrawQueueTest PRIVATE Core)
target_include_directories(DebugDrawQueueTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(DebugDrawQueueTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME DebugDrawQueueTest COMMAND DebugDrawQueueTest)

add_executable(ViewportDebugViewModeReleaseClampTest
    ViewportDebugViewModeReleaseClampTest.cpp
)
target_link_libraries(ViewportDebugViewModeReleaseClampTest PRIVATE Core)
target_include_directories(ViewportDebugViewModeReleaseClampTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(ViewportDebugViewModeReleaseClampTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME ViewportDebugViewModeReleaseClampTest COMMAND ViewportDebugViewModeReleaseClampTest)

add_executable(ViewportSnapshotDebugWiringTest
    ViewportSnapshotDebugWiringTest.cpp
)
target_link_libraries(ViewportSnapshotDebugWiringTest PRIVATE Core)
target_include_directories(ViewportSnapshotDebugWiringTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(ViewportSnapshotDebugWiringTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME ViewportSnapshotDebugWiringTest COMMAND ViewportSnapshotDebugWiringTest)

add_executable(MegaGeometryFrameCommandDebugModeTest
    MegaGeometryFrameCommandDebugModeTest.cpp
)
target_link_libraries(MegaGeometryFrameCommandDebugModeTest PRIVATE Core)
target_include_directories(MegaGeometryFrameCommandDebugModeTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
target_compile_definitions(MegaGeometryFrameCommandDebugModeTest PRIVATE
    NORVES_SHADER_DIR="${CMAKE_SOURCE_DIR}/Assets/Shaders"
)
set_target_properties(MegaGeometryFrameCommandDebugModeTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME MegaGeometryFrameCommandDebugModeTest COMMAND MegaGeometryFrameCommandDebugModeTest)

add_executable(SceneViewProxyReconcileTest
    SceneViewProxyReconcileTest.cpp
)
target_link_libraries(SceneViewProxyReconcileTest PRIVATE Core)
target_include_directories(SceneViewProxyReconcileTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(SceneViewProxyReconcileTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME SceneViewProxyReconcileTest COMMAND SceneViewProxyReconcileTest)

add_executable(SceneViewProxyLookupTest
    SceneViewProxyLookupTest.cpp
)
target_link_libraries(SceneViewProxyLookupTest PRIVATE Core)
target_include_directories(SceneViewProxyLookupTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(SceneViewProxyLookupTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME SceneViewProxyLookupTest COMMAND SceneViewProxyLookupTest)

add_executable(ComponentDirtyTrackingTest
    ComponentDirtyTrackingTest.cpp
)
target_link_libraries(ComponentDirtyTrackingTest PRIVATE Core)
target_include_directories(ComponentDirtyTrackingTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(ComponentDirtyTrackingTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME ComponentDirtyTrackingTest COMMAND ComponentDirtyTrackingTest)

add_executable(ComponentDataRegistryTest
    ComponentDataRegistryTest.cpp
)
target_link_libraries(ComponentDataRegistryTest PRIVATE Core)
target_include_directories(ComponentDataRegistryTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(ComponentDataRegistryTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME ComponentDataRegistryTest COMMAND ComponentDataRegistryTest)

add_executable(ComponentDataBenchmark
    ComponentDataBenchmark.cpp
)
target_link_libraries(ComponentDataBenchmark PRIVATE Core)
target_include_directories(ComponentDataBenchmark PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(ComponentDataBenchmark PROPERTIES FOLDER "Test/Core/Rendering")

add_executable(WorldSyncDifferentialTest
    WorldSyncDifferentialTest.cpp
)
target_link_libraries(WorldSyncDifferentialTest PRIVATE Core)
target_include_directories(WorldSyncDifferentialTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(WorldSyncDifferentialTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME WorldSyncDifferentialTest COMMAND WorldSyncDifferentialTest)

add_executable(RenderFrameExecutorPlanTest
    RenderFrameExecutorPlanTest.cpp
)
target_link_libraries(RenderFrameExecutorPlanTest PRIVATE Core)
target_include_directories(RenderFrameExecutorPlanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(RenderFrameExecutorPlanTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RenderFrameExecutorPlanTest COMMAND RenderFrameExecutorPlanTest)

add_executable(CanvasViewRenderTest
    CanvasViewRenderTest.cpp
)
target_link_libraries(CanvasViewRenderTest PRIVATE Core)
target_include_directories(CanvasViewRenderTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(CanvasViewRenderTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME CanvasViewRenderTest COMMAND CanvasViewRenderTest)

add_executable(CanvasLayerCompositeSnapshotTest
    CanvasLayerCompositeSnapshotTest.cpp
)
target_link_libraries(CanvasLayerCompositeSnapshotTest PRIVATE Core)
target_include_directories(CanvasLayerCompositeSnapshotTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(CanvasLayerCompositeSnapshotTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME CanvasLayerCompositeSnapshotTest COMMAND CanvasLayerCompositeSnapshotTest)

add_executable(BoardInstanceBatchingTest
    BoardInstanceBatchingTest.cpp
)
target_link_libraries(BoardInstanceBatchingTest PRIVATE Core)
target_include_directories(BoardInstanceBatchingTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(BoardInstanceBatchingTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME BoardInstanceBatchingTest COMMAND BoardInstanceBatchingTest)

add_executable(BoardComponentRoutingTest
    BoardComponentRoutingTest.cpp
)
target_link_libraries(BoardComponentRoutingTest PRIVATE Core)
target_include_directories(BoardComponentRoutingTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(BoardComponentRoutingTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME BoardComponentRoutingTest COMMAND BoardComponentRoutingTest)

add_executable(BillboardComponentTest
    BillboardComponentTest.cpp
)
target_link_libraries(BillboardComponentTest PRIVATE Core)
target_include_directories(BillboardComponentTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(BillboardComponentTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME BillboardComponentTest COMMAND BillboardComponentTest)

add_executable(ImpostorBakeServiceTest
    ImpostorBakeServiceTest.cpp
)
target_link_libraries(ImpostorBakeServiceTest PRIVATE Core)
target_include_directories(ImpostorBakeServiceTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(ImpostorBakeServiceTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME ImpostorBakeServiceTest COMMAND ImpostorBakeServiceTest)

add_executable(ImpostorOctahedralTest
    ImpostorOctahedralTest.cpp
)
target_link_libraries(ImpostorOctahedralTest PRIVATE Core)
target_include_directories(ImpostorOctahedralTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(ImpostorOctahedralTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME ImpostorOctahedralTest COMMAND ImpostorOctahedralTest)

add_executable(ImpostorLODSwitchTest
    ImpostorLODSwitchTest.cpp
)
target_link_libraries(ImpostorLODSwitchTest PRIVATE Core)
target_include_directories(ImpostorLODSwitchTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(ImpostorLODSwitchTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME ImpostorLODSwitchTest COMMAND ImpostorLODSwitchTest)

add_executable(SceneViewWorldBoardDrawCommandTest
    SceneViewWorldBoardDrawCommandTest.cpp
)
target_link_libraries(SceneViewWorldBoardDrawCommandTest PRIVATE Core)
target_include_directories(SceneViewWorldBoardDrawCommandTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(SceneViewWorldBoardDrawCommandTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME SceneViewWorldBoardDrawCommandTest COMMAND SceneViewWorldBoardDrawCommandTest)

add_executable(BoardTransformTest
    BoardTransformTest.cpp
)
target_link_libraries(BoardTransformTest PRIVATE Core)
target_include_directories(BoardTransformTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(BoardTransformTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME BoardTransformTest COMMAND BoardTransformTest)

add_executable(SpriteSheetUVRectTest
    SpriteSheetUVRectTest.cpp
)
target_link_libraries(SpriteSheetUVRectTest PRIVATE Core)
target_include_directories(SpriteSheetUVRectTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(SpriteSheetUVRectTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME SpriteSheetUVRectTest COMMAND SpriteSheetUVRectTest)

add_executable(FlipbookAdvanceTest
    FlipbookAdvanceTest.cpp
)
target_link_libraries(FlipbookAdvanceTest PRIVATE Core)
target_include_directories(FlipbookAdvanceTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(FlipbookAdvanceTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME FlipbookAdvanceTest COMMAND FlipbookAdvanceTest)

add_executable(BoardBlendModeTest
    BoardBlendModeTest.cpp
)
target_link_libraries(BoardBlendModeTest PRIVATE Core)
target_include_directories(BoardBlendModeTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(BoardBlendModeTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME BoardBlendModeTest COMMAND BoardBlendModeTest)

add_executable(BoardSortKeyStableTest
    BoardSortKeyStableTest.cpp
)
target_link_libraries(BoardSortKeyStableTest PRIVATE Core)
target_include_directories(BoardSortKeyStableTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(BoardSortKeyStableTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME BoardSortKeyStableTest COMMAND BoardSortKeyStableTest)

add_executable(MultiLayerOrderTest
    MultiLayerOrderTest.cpp
)
target_link_libraries(MultiLayerOrderTest PRIVATE Core)
target_include_directories(MultiLayerOrderTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(MultiLayerOrderTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME MultiLayerOrderTest COMMAND MultiLayerOrderTest)

add_executable(CompositePassTest
    CompositePassTest.cpp
)
target_link_libraries(CompositePassTest PRIVATE Core)
target_include_directories(CompositePassTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(CompositePassTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME CompositePassTest COMMAND CompositePassTest)

add_executable(PresentationComposerTest
    PresentationComposerTest.cpp
)
target_link_libraries(PresentationComposerTest PRIVATE Core)
target_include_directories(PresentationComposerTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(PresentationComposerTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME PresentationComposerTest COMMAND PresentationComposerTest)

add_executable(RenderGraphPresentationPassTest
    RenderGraphPresentationPassTest.cpp
)
target_link_libraries(RenderGraphPresentationPassTest PRIVATE Core)
target_include_directories(RenderGraphPresentationPassTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(RenderGraphPresentationPassTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RenderGraphPresentationPassTest COMMAND RenderGraphPresentationPassTest)

add_executable(RenderGraphTextureUsageContractTest
    RenderGraphTextureUsageContractTest.cpp
)
target_link_libraries(RenderGraphTextureUsageContractTest PRIVATE Core)
target_include_directories(RenderGraphTextureUsageContractTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(RenderGraphTextureUsageContractTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RenderGraphTextureUsageContractTest COMMAND RenderGraphTextureUsageContractTest)

add_executable(RenderGraphCompileTest
    RenderGraphCompileTest.cpp
)
target_link_libraries(RenderGraphCompileTest PRIVATE Core)
target_include_directories(RenderGraphCompileTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
target_compile_definitions(RenderGraphCompileTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
set_target_properties(RenderGraphCompileTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RenderGraphCompileTest COMMAND RenderGraphCompileTest)

add_executable(RenderGraphLifetimeTest
    RenderGraphLifetimeTest.cpp
)
target_link_libraries(RenderGraphLifetimeTest PRIVATE Core)
target_include_directories(RenderGraphLifetimeTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(RenderGraphLifetimeTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RenderGraphLifetimeTest COMMAND RenderGraphLifetimeTest)

add_executable(RenderGraphNamedResourceTest
    RenderGraphNamedResourceTest.cpp
)
target_link_libraries(RenderGraphNamedResourceTest PRIVATE Core)
target_include_directories(RenderGraphNamedResourceTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(RenderGraphNamedResourceTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RenderGraphNamedResourceTest COMMAND RenderGraphNamedResourceTest)

add_executable(RenderGraphAttachmentStateTest
    RenderGraphAttachmentStateTest.cpp
)
target_link_libraries(RenderGraphAttachmentStateTest PRIVATE Core)
target_include_directories(RenderGraphAttachmentStateTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(RenderGraphAttachmentStateTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RenderGraphAttachmentStateTest COMMAND RenderGraphAttachmentStateTest)

add_executable(RenderGraphDumpTest
    RenderGraphDumpTest.cpp
)
target_link_libraries(RenderGraphDumpTest PRIVATE Core)
target_include_directories(RenderGraphDumpTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(RenderGraphDumpTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RenderGraphDumpTest COMMAND RenderGraphDumpTest)

add_executable(LegacyViewPassAdapterTest
    LegacyViewPassAdapterTest.cpp
)
target_link_libraries(LegacyViewPassAdapterTest PRIVATE Core)
target_include_directories(LegacyViewPassAdapterTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(LegacyViewPassAdapterTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME LegacyViewPassAdapterTest COMMAND LegacyViewPassAdapterTest)

add_executable(TransientResourcePoolTest
    TransientResourcePoolTest.cpp
)
target_link_libraries(TransientResourcePoolTest PRIVATE Core)
target_include_directories(TransientResourcePoolTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(TransientResourcePoolTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME TransientResourcePoolTest COMMAND TransientResourcePoolTest)
add_executable(InstanceBufferRingTest
    InstanceBufferRingTest.cpp
)
target_link_libraries(InstanceBufferRingTest PRIVATE Core)
target_include_directories(InstanceBufferRingTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(InstanceBufferRingTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME InstanceBufferRingTest COMMAND InstanceBufferRingTest)

add_executable(CookedTextureUploadTest
    CookedTextureUploadTest.cpp
)
target_link_libraries(CookedTextureUploadTest PRIVATE Core)
target_include_directories(CookedTextureUploadTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
set_target_properties(CookedTextureUploadTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME CookedTextureUploadTest COMMAND CookedTextureUploadTest)

add_executable(TextureHandleCacheTest
    TextureHandleCacheTest.cpp
)
target_link_libraries(TextureHandleCacheTest PRIVATE Core)
target_include_directories(TextureHandleCacheTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
set_target_properties(TextureHandleCacheTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME TextureHandleCacheTest COMMAND TextureHandleCacheTest)

add_executable(ModelHandleCacheTest
    ModelHandleCacheTest.cpp
)
target_link_libraries(ModelHandleCacheTest PRIVATE Core)
target_include_directories(ModelHandleCacheTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
set_target_properties(ModelHandleCacheTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME ModelHandleCacheTest COMMAND ModelHandleCacheTest)

add_executable(ModelAsyncLoadQueueTest
    ModelAsyncLoadQueueTest.cpp
)
target_link_libraries(ModelAsyncLoadQueueTest PRIVATE Core)
target_include_directories(ModelAsyncLoadQueueTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
set_target_properties(ModelAsyncLoadQueueTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME ModelAsyncLoadQueueTest COMMAND ModelAsyncLoadQueueTest)

add_executable(ModelResourcesAssetRuntimeTest
    ModelResourcesAssetRuntimeTest.cpp
)
target_link_libraries(ModelResourcesAssetRuntimeTest PRIVATE Core)
target_include_directories(ModelResourcesAssetRuntimeTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
set_target_properties(ModelResourcesAssetRuntimeTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME ModelResourcesAssetRuntimeTest COMMAND ModelResourcesAssetRuntimeTest)

add_executable(AssetRuntimeSnapshotReloadTest
    AssetRuntimeSnapshotReloadTest.cpp
)
target_link_libraries(AssetRuntimeSnapshotReloadTest PRIVATE Core)
target_include_directories(AssetRuntimeSnapshotReloadTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
set_target_properties(AssetRuntimeSnapshotReloadTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME AssetRuntimeSnapshotReloadTest COMMAND AssetRuntimeSnapshotReloadTest)

add_executable(TextureAssetReloadAdmissionTest
    TextureAssetReloadAdmissionTest.cpp
)
target_link_libraries(TextureAssetReloadAdmissionTest PRIVATE Core)
target_include_directories(TextureAssetReloadAdmissionTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
set_target_properties(TextureAssetReloadAdmissionTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME TextureAssetReloadAdmissionTest COMMAND TextureAssetReloadAdmissionTest)

if(NORVES_BRIDGE_SDK_DIR)
    add_executable(AssetManifestReloadBridgeLoopbackTest
        AssetManifestReloadBridgeLoopbackTest.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameApplicationHandler.cpp
    ${CMAKE_SOURCE_DIR}/Game/Scripting/M6ScriptSmokeController.cpp
        ${CMAKE_SOURCE_DIR}/Game/Input/CameraInputCollector.cpp
        ${CMAKE_SOURCE_DIR}/Game/Input/PickingController.cpp
        ${CMAKE_SOURCE_DIR}/Game/Bridge/NorvesLibBridgeAdapter.cpp
        ${CMAKE_SOURCE_DIR}/Game/Bridge/BridgeBase64.cpp
        ${CMAKE_SOURCE_DIR}/Game/Bridge/BridgeServerHost.cpp
        ${CMAKE_SOURCE_DIR}/Game/Bridge/BridgeLogSink.cpp
        ${CMAKE_SOURCE_DIR}/Game/Bridge/ReadyHandshake.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameModes/MemoryAgingTest/MemoryAgingTestRoutine.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameModes/Rendering3DTest/DirectionalLightEditSubRoutine.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameModes/Rendering3DTest/Rendering3DTestDebugDraw.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameModes/Rendering3DTest/Rendering3DTestDebugInput.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameModes/Rendering3DTest/Rendering3DTestRoutine.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameModes/Rendering3DTest/M8MinimalPhysicsSmoke.cpp
        ${CMAKE_SOURCE_DIR}/Game/GameModes/Rendering3DTest/M9WorldAcceptance.cpp
    )
    target_link_libraries(AssetManifestReloadBridgeLoopbackTest PRIVATE
        Core
        NorvesModule_Dummy
        NorvesModule_Physics
        norves_bridge_engine_sdk
    )
    target_compile_definitions(AssetManifestReloadBridgeLoopbackTest PRIVATE
        NORVES_BRIDGE_ENABLED
    )
    target_include_directories(AssetManifestReloadBridgeLoopbackTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/Game
        ${CMAKE_SOURCE_DIR}/Library/Core/Private
    )
    add_dependencies(AssetManifestReloadBridgeLoopbackTest AssetCook Game)
    set_target_properties(AssetManifestReloadBridgeLoopbackTest PROPERTIES
        FOLDER "Test/Core/Rendering"
    )
    get_filename_component(NORVES_EDITOR_SOURCE_DIR
        "${NORVES_BRIDGE_SDK_DIR}/../.."
        ABSOLUTE
    )
    if(NOT EXISTS "${NORVES_EDITOR_SOURCE_DIR}/apps/editor/src-tauri/Cargo.toml")
        get_filename_component(NORVES_EDITOR_SOURCE_DIR
            "${NORVES_BRIDGE_SDK_DIR}/../../.."
            ABSOLUTE
        )
    endif()
    set(ASSET_MANIFEST_RELOAD_BRIDGE_E2E_DIR
        "${CMAKE_BINARY_DIR}/AssetManifestReloadBridgeLoopback/$<CONFIG>"
    )
    add_test(NAME AssetManifestReloadBridgeLoopbackTest
        COMMAND ${CMAKE_COMMAND}
            "-DASSET_COOK_EXE=$<TARGET_FILE:AssetCook>"
            "-DLOOPBACK_TEST_EXE=$<TARGET_FILE:AssetManifestReloadBridgeLoopbackTest>"
            "-DGAME_EXE=$<TARGET_FILE:Game>"
            "-DNORVES_EDITOR_SOURCE_DIR=${NORVES_EDITOR_SOURCE_DIR}"
            "-DE2E_DIR=${ASSET_MANIFEST_RELOAD_BRIDGE_E2E_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/AssetManifestReloadBridgeLoopback.cmake.in"
    )
    set_tests_properties(AssetManifestReloadBridgeLoopbackTest PROPERTIES TIMEOUT 600)
endif()

add_executable(ModelResourcesAssetLoadProfileContractTest
    ModelResourcesAssetLoadProfileContractTest.cpp
)
target_link_libraries(ModelResourcesAssetLoadProfileContractTest PRIVATE Core)
target_include_directories(ModelResourcesAssetLoadProfileContractTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(ModelResourcesAssetLoadProfileContractTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME ModelResourcesAssetLoadProfileContractTest COMMAND ModelResourcesAssetLoadProfileContractTest)

set(ASSET_LOAD_PROFILE_SUMMARY_CONTRACT_DIR
    "${CMAKE_BINARY_DIR}/AssetLoadProfileSummaryContract/$<CONFIG>"
)
add_test(NAME AssetLoadProfileSummaryContractTest
    COMMAND ${CMAKE_COMMAND}
        "-DSUMMARY_SCRIPT=${CMAKE_SOURCE_DIR}/Scripts/SummarizeAssetLoadProfile.ps1"
        "-DCASE_DIR=${ASSET_LOAD_PROFILE_SUMMARY_CONTRACT_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/AssetLoadProfileSummaryContract.cmake.in"
)

add_executable(TextureAsyncTypesTest
    TextureAsyncTypesTest.cpp
)
target_link_libraries(TextureAsyncTypesTest PRIVATE Core)
target_include_directories(TextureAsyncTypesTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
set_target_properties(TextureAsyncTypesTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME TextureAsyncTypesTest COMMAND TextureAsyncTypesTest)

add_executable(TextureResourcesTextureAssetTest
    TextureResourcesTextureAssetTest.cpp
)
target_link_libraries(TextureResourcesTextureAssetTest PRIVATE Core)
target_include_directories(TextureResourcesTextureAssetTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(TextureResourcesTextureAssetTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME TextureResourcesTextureAssetTest COMMAND TextureResourcesTextureAssetTest)

add_executable(TextureResourcesPreparedTextureAssetTest
    TextureResourcesPreparedTextureAssetTest.cpp
)
target_link_libraries(TextureResourcesPreparedTextureAssetTest PRIVATE Core)
target_include_directories(TextureResourcesPreparedTextureAssetTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(TextureResourcesPreparedTextureAssetTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME TextureResourcesPreparedTextureAssetTest COMMAND TextureResourcesPreparedTextureAssetTest)

add_executable(TextureResourcesAssetLoadProfileContractTest
    TextureResourcesAssetLoadProfileContractTest.cpp
)
target_link_libraries(TextureResourcesAssetLoadProfileContractTest PRIVATE Core)
target_include_directories(TextureResourcesAssetLoadProfileContractTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(TextureResourcesAssetLoadProfileContractTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME TextureResourcesAssetLoadProfileContractTest COMMAND TextureResourcesAssetLoadProfileContractTest)

add_executable(RenderResourcesDomainContractTest
    RenderResourcesDomainContractTest.cpp
)
target_link_libraries(RenderResourcesDomainContractTest PRIVATE Core)
target_include_directories(RenderResourcesDomainContractTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(RenderResourcesDomainContractTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME RenderResourcesDomainContractTest COMMAND RenderResourcesDomainContractTest)

add_executable(MeshResourcesProceduralGpuTest
    MeshResourcesProceduralGpuTest.cpp
)
target_link_libraries(MeshResourcesProceduralGpuTest PRIVATE Core)
target_include_directories(MeshResourcesProceduralGpuTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(MeshResourcesProceduralGpuTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME MeshResourcesProceduralGpuTest COMMAND MeshResourcesProceduralGpuTest)

add_executable(MeshResourcesPrimitiveTest
    MeshResourcesPrimitiveTest.cpp
)
target_link_libraries(MeshResourcesPrimitiveTest PRIVATE Core)
target_include_directories(MeshResourcesPrimitiveTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(MeshResourcesPrimitiveTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME MeshResourcesPrimitiveTest COMMAND MeshResourcesPrimitiveTest)

add_executable(MegaGeometryResourcesTest
    MegaGeometryResourcesTest.cpp
)
target_link_libraries(MegaGeometryResourcesTest PRIVATE Core)
target_include_directories(MegaGeometryResourcesTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(MegaGeometryResourcesTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME MegaGeometryResourcesTest COMMAND MegaGeometryResourcesTest)

add_executable(MaterialResourcesTest
    MaterialResourcesTest.cpp
)
target_link_libraries(MaterialResourcesTest PRIVATE Core)
target_include_directories(MaterialResourcesTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(MaterialResourcesTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME MaterialResourcesTest COMMAND MaterialResourcesTest)

add_executable(TextureResourcesAssetCookSmokeTest
    TextureResourcesAssetCookSmokeTest.cpp
)
target_link_libraries(TextureResourcesAssetCookSmokeTest PRIVATE Core)
target_include_directories(TextureResourcesAssetCookSmokeTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
add_dependencies(TextureResourcesAssetCookSmokeTest AssetCook)
set_target_properties(TextureResourcesAssetCookSmokeTest PROPERTIES FOLDER "Test/Core/Rendering")

set(TEXTURE_RESOURCES_ASSET_COOK_SMOKE_DIR
    "${CMAKE_BINARY_DIR}/TextureResourcesAssetCookSmoke/$<CONFIG>")
add_test(NAME TextureResourcesAssetCookSmoke
    COMMAND ${CMAKE_COMMAND}
        "-DASSET_COOK_EXE=$<TARGET_FILE:AssetCook>"
        "-DRUNTIME_TEST_EXE=$<TARGET_FILE:TextureResourcesAssetCookSmokeTest>"
        "-DSMOKE_DIR=${TEXTURE_RESOURCES_ASSET_COOK_SMOKE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/TextureResourcesAssetCookSmoke.cmake.in"
)

add_executable(ModelResourcesAssetCookSmokeTest
    ModelResourcesAssetCookSmokeTest.cpp
)
target_link_libraries(ModelResourcesAssetCookSmokeTest PRIVATE Core)
target_include_directories(ModelResourcesAssetCookSmokeTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
add_dependencies(ModelResourcesAssetCookSmokeTest AssetCook)
set_target_properties(ModelResourcesAssetCookSmokeTest PROPERTIES FOLDER "Test/Core/Rendering")

set(MODEL_RESOURCES_ASSET_COOK_SMOKE_DIR
    "${CMAKE_BINARY_DIR}/ModelResourcesAssetCookSmoke/$<CONFIG>")
add_test(NAME ModelResourcesAssetCookSmoke
    COMMAND ${CMAKE_COMMAND}
        "-DASSET_COOK_EXE=$<TARGET_FILE:AssetCook>"
        "-DRUNTIME_TEST_EXE=$<TARGET_FILE:ModelResourcesAssetCookSmokeTest>"
        "-DSMOKE_DIR=${MODEL_RESOURCES_ASSET_COOK_SMOKE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/ModelResourcesAssetCookSmoke.cmake.in"
)

add_executable(ApplicationExitFramePolicyTest
    ApplicationExitFramePolicyTest.cpp
)
target_link_libraries(ApplicationExitFramePolicyTest PRIVATE Core)
target_include_directories(ApplicationExitFramePolicyTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
set_target_properties(ApplicationExitFramePolicyTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME ApplicationExitFramePolicyTest COMMAND ApplicationExitFramePolicyTest)

if(NORVES_ENABLE_CORE_TEXT)
    add_executable(FontAtlasTest
        FontAtlasTest.cpp
    )
    target_link_libraries(FontAtlasTest PRIVATE Core)
    target_include_directories(FontAtlasTest PRIVATE
        ${CMAKE_SOURCE_DIR}
    )
    set_target_properties(FontAtlasTest PROPERTIES FOLDER "Test/Core/Rendering")
    add_test(NAME FontAtlasTest COMMAND FontAtlasTest)

    add_executable(TextComponentBoardProxyTest
        TextComponentBoardProxyTest.cpp
    )
    target_link_libraries(TextComponentBoardProxyTest PRIVATE Core)
    target_include_directories(TextComponentBoardProxyTest PRIVATE
        ${CMAKE_SOURCE_DIR}
    )
    set_target_properties(TextComponentBoardProxyTest PROPERTIES FOLDER "Test/Core/Rendering")
    add_test(NAME TextComponentBoardProxyTest COMMAND TextComponentBoardProxyTest)
endif()

add_executable(ParticleSystemTest
    ParticleSystemTest.cpp
)
target_link_libraries(ParticleSystemTest PRIVATE Core)
set_target_properties(ParticleSystemTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME ParticleSystemTest COMMAND ParticleSystemTest)

add_executable(ParticleFrameSnapshotTest
    ParticleFrameSnapshotTest.cpp
)
target_link_libraries(ParticleFrameSnapshotTest PRIVATE Core)
target_compile_definitions(ParticleFrameSnapshotTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
set_target_properties(ParticleFrameSnapshotTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME ParticleFrameSnapshotTest COMMAND ParticleFrameSnapshotTest)

add_executable(CanvasTransientBoardSnapshotTest
    CanvasTransientBoardSnapshotTest.cpp
)
target_link_libraries(CanvasTransientBoardSnapshotTest PRIVATE Core)
target_include_directories(CanvasTransientBoardSnapshotTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(CanvasTransientBoardSnapshotTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME CanvasTransientBoardSnapshotTest COMMAND CanvasTransientBoardSnapshotTest)

add_executable(GLTFSkeletalAnalyzerTest
    GLTFSkeletalAnalyzerTest.cpp
)
target_link_libraries(GLTFSkeletalAnalyzerTest PRIVATE Core)
target_include_directories(GLTFSkeletalAnalyzerTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(GLTFSkeletalAnalyzerTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME GLTFSkeletalAnalyzerTest COMMAND GLTFSkeletalAnalyzerTest)

add_executable(SkeletalAnimationSamplingTest
    SkeletalAnimationSamplingTest.cpp
)
target_link_libraries(SkeletalAnimationSamplingTest PRIVATE Core)
set_target_properties(SkeletalAnimationSamplingTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME SkeletalAnimationSamplingTest COMMAND SkeletalAnimationSamplingTest)

add_executable(SkeletalFramePacketSnapshotTest
    SkeletalFramePacketSnapshotTest.cpp
)
target_link_libraries(SkeletalFramePacketSnapshotTest PRIVATE Core)

add_executable(SkinnedRenderPathContractTest
    SkinnedRenderPathContractTest.cpp
)
target_link_libraries(SkinnedRenderPathContractTest PRIVATE Core)
target_include_directories(SkinnedRenderPathContractTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
target_compile_definitions(SkinnedRenderPathContractTest PRIVATE
    NORVES_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
set_target_properties(SkinnedRenderPathContractTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME SkinnedRenderPathContractTest COMMAND SkinnedRenderPathContractTest)
set_target_properties(SkeletalFramePacketSnapshotTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME SkeletalFramePacketSnapshotTest COMMAND SkeletalFramePacketSnapshotTest)

add_executable(CameraComponentProxyTest
    CameraComponentProxyTest.cpp
)
target_link_libraries(CameraComponentProxyTest PRIVATE Core)
set_target_properties(CameraComponentProxyTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME CameraComponentProxyTest COMMAND CameraComponentProxyTest)

add_executable(WorldCameraSyncTest
    WorldCameraSyncTest.cpp
    ${CMAKE_SOURCE_DIR}/Game/Input/CameraInputCollector.cpp
)
target_link_libraries(WorldCameraSyncTest PRIVATE Core)
target_include_directories(WorldCameraSyncTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Game
)
set_target_properties(WorldCameraSyncTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME WorldCameraSyncTest COMMAND WorldCameraSyncTest)

# 実行に数分から1時間以上かかるPT参照比較と連番の確認。普段は回さず、節目にだけ `ctest -L Reference` で回す。
set(NORVES_REFERENCE_TESTS
    PathTracingConvergenceVulkanTest
    PathTracingRasterParityVulkanTest
    R4DDGIPathTracingReferenceVulkanTest
    R6RTGIPathTracingReferenceVulkanTest
    R7OutdoorPathTracingReferenceVulkanTest
    R8DepthOfFieldPathTracingReferenceVulkanTest
    R8MotionBlurPathTracingReferenceVulkanTest
    R8SequenceSmokeTest
)
foreach(test_name IN LISTS NORVES_REFERENCE_TESTS)
    set_property(TEST ${test_name} APPEND PROPERTY LABELS Reference)
endforeach()

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

add_test(NAME RenderGraphGPUTimestampScopeTest COMMAND RenderingCoordinatorGPUTimestampPublicationTest --test=RenderGraphGPUTimestampScopeTest)


norves_add_test_bundle(RenderingCoordinatorGPUTimestampPublicationTest
    FOLDER "Test/Core/Rendering"
    MEMBER RenderingCoordinatorGPUTimestampPublicationTest RenderingCoordinatorGPUTimestampPublicationTest.cpp
    MEMBER RenderGraphGPUTimestampScopeTest RenderGraphGPUTimestampScopeTest.cpp
)
target_link_libraries(RenderingCoordinatorGPUTimestampPublicationTest PRIVATE Core)
target_include_directories(RenderingCoordinatorGPUTimestampPublicationTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
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

register_rendering_gpu_test(
    RHIBufferDeviceAddressVulkanTest
    RHIAccelerationStructureVulkanTest --test=RHIBufferDeviceAddressVulkanTest
)

norves_add_test_bundle(RHIAccelerationStructureVulkanTest
    FOLDER "Test/Core/Rendering"
    MEMBER RHIAccelerationStructureVulkanTest RHIAccelerationStructureVulkanTest.cpp
    MEMBER RHIBufferDeviceAddressVulkanTest RHIBufferDeviceAddressVulkanTest.cpp
    MEMBER RayTracingCapabilityContractTest RayTracingCapabilityContractTest.cpp
)
target_link_libraries(RHIAccelerationStructureVulkanTest PRIVATE RenderingGpuTestSupport)
target_include_directories(RHIAccelerationStructureVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
    ${CMAKE_SOURCE_DIR}
)
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
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
set_target_properties(RayTracingSceneSnapshotTest PROPERTIES FOLDER "Test/Core/Rendering")
register_rendering_gpu_test(
    RayTracingSceneSnapshotTest
    RayTracingSceneSnapshotTest
)
register_rendering_gpu_test(
    DDGIProbeRayQueryVulkanTest
    DDGIProbeRadianceVulkanTest --test=DDGIProbeRayQueryVulkanTest
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

norves_add_test_bundle(R8AcesLutToneMappingVulkanTest
    FOLDER "Test/Core/Rendering"
    MEMBER R8AcesLutToneMappingVulkanTest R8AcesLutToneMappingVulkanTest.cpp
    MEMBER R8FilmGrainVulkanTest R8FilmGrainVulkanTest.cpp
    MEMBER AutoExposureHistogramVulkanTest AutoExposureHistogramVulkanTest.cpp
    MEMBER GradingLutIdentityVulkanTest GradingLutIdentityVulkanTest.cpp
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
register_rendering_gpu_test(
    R8AcesLutToneMappingVulkanTest
    R8AcesLutToneMappingVulkanTest
)
# R8: フィルムグレイン（既定オフ、強さ0で従来と一致、統計・フレームごとの模様・再実行の一致）を実GPUで確かめる。
register_rendering_gpu_test(R8FilmGrainVulkanTest R8AcesLutToneMappingVulkanTest --test=R8FilmGrainVulkanTest)
# 自動露出: 既知の輝度の画素から作ったヒストグラムを実GPUで読み戻し、全区間がCPUの数え方と一致すること。
register_rendering_gpu_test(AutoExposureHistogramVulkanTest R8AcesLutToneMappingVulkanTest --test=AutoExposureHistogramVulkanTest)
# 見た目の3D LUT: 恒等のLUTで出力が変わらず、見た目のLUTがCPUの三線形補間と一致し、ACES 2.0 SDR LUTの演算子と読めないLUTでは掛からないことを実GPUで確かめる。
register_rendering_gpu_test(GradingLutIdentityVulkanTest R8AcesLutToneMappingVulkanTest --test=GradingLutIdentityVulkanTest)
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

norves_add_test_bundle(DDGIProbeRadianceVulkanTest
    FOLDER "Test/Core/Rendering"
    MEMBER DDGIProbeRadianceVulkanTest DDGIProbeRadianceVulkanTest.cpp
    MEMBER DDGIProbeRayQueryVulkanTest DDGIProbeRayQueryVulkanTest.cpp
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
register_rendering_gpu_test(
    RayTracingCapabilityContractTest
    RHIAccelerationStructureVulkanTest --test=RayTracingCapabilityContractTest
)


norves_add_test_bundle(RHIRayTracingApiContractTest
    FOLDER "Test/Core/Rendering"
    MEMBER RHIRayTracingApiContractTest RHIRayTracingApiContractTest.cpp
    MEMBER RHIRayTracingShaderStageCompileTest RHIRayTracingShaderStageCompileTest.cpp
)
target_link_libraries(RHIRayTracingApiContractTest PRIVATE Core)
target_include_directories(RHIRayTracingApiContractTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
target_compile_definitions(RHIRayTracingApiContractTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
add_test(NAME RHIRayTracingApiContractTest COMMAND RHIRayTracingApiContractTest)

add_test(NAME RHIRayTracingShaderStageCompileTest COMMAND RHIRayTracingApiContractTest --test=RHIRayTracingShaderStageCompileTest)

add_test(NAME RHITextureToBufferReadbackVulkanTest COMMAND RHITextureUpdateVulkanTest --test=RHITextureToBufferReadbackVulkanTest)
set_tests_properties(RHITextureToBufferReadbackVulkanTest PROPERTIES
    LABELS "GPU;RenderingValidation"
    RESOURCE_LOCK NorvesLibGPU
    SKIP_RETURN_CODE 125
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# BC1・BC4・BC5・BC7 と全ミップを渡した R16 の 8x8・2段のテクスチャを GpuResourceStore 経由で上げ、計算シェーダーでミップごとに読み戻す。
add_test(NAME RHIBlockCompressedTextureVulkanTest COMMAND RHITextureUpdateVulkanTest --test=RHIBlockCompressedTextureVulkanTest)
set_tests_properties(RHIBlockCompressedTextureVulkanTest PROPERTIES
    LABELS "GPU;RenderingValidation"
    RESOURCE_LOCK NorvesLibGPU
    SKIP_RETURN_CODE 125
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_test(NAME VideoMemoryBudgetLogGateTest COMMAND RHITextureUpdateVulkanTest --test=VideoMemoryBudgetLogGateTest)

add_test(NAME SparseCapabilitiesVulkanTest COMMAND RHITextureUpdateVulkanTest --test=SparseCapabilitiesVulkanTest)
set_tests_properties(SparseCapabilitiesVulkanTest PROPERTIES
    LABELS "GPU;RenderingValidation"
    RESOURCE_LOCK NorvesLibGPU
    SKIP_RETURN_CODE 125
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_test(NAME SparseTextureVulkanTest COMMAND RHITextureUpdateVulkanTest --test=SparseTextureVulkanTest)
set_tests_properties(SparseTextureVulkanTest PROPERTIES
    LABELS "GPU;RenderingValidation"
    RESOURCE_LOCK NorvesLibGPU
    SKIP_RETURN_CODE 125
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# sparse テクスチャへ物理ページを結び・外す（ミップテイルと2タイル）。結んだタイルへ書いた色を計算シェーダーで読み戻し、
# 外したページが GpuRetireQueue を通ってプールの空きへ戻ることを確かめる。
add_test(NAME SparseBindVulkanTest COMMAND RHITextureUpdateVulkanTest --test=SparseBindVulkanTest)
set_tests_properties(SparseBindVulkanTest PROPERTIES
    LABELS "GPU;RenderingValidation"
    RESOURCE_LOCK NorvesLibGPU
    SKIP_RETURN_CODE 125
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# ステージングのリング経由で、sparse テクスチャのタイル・ミップテイルへ GPU を待たずに書く（TileUploader）。
# 連続フレームで書いて読み戻し、その間に WaitIdle を呼ばないこと、リングの区画が提出の完了まで再利用されないこと、
# 1フレームのコピー量の上限、提出しなかったフレームの出し直しを確かめる。
add_test(NAME SparseTileUploadVulkanTest COMMAND RHITextureUpdateVulkanTest --test=SparseTileUploadVulkanTest)
set_tests_properties(SparseTileUploadVulkanTest PROPERTIES
    LABELS "GPU;RenderingValidation"
    RESOURCE_LOCK NorvesLibGPU
    SKIP_RETURN_CODE 125
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# ステージングのリング経由で、ジオメトリのプール（DeviceLocal の大きなバッファ）の区画へ GPU を待たずに書く（TileUploader::EnqueueBufferCopy）。
# 連続フレームで区画の途中へ書き、計算シェーダーで読み戻して一致を確かめる。その間に WaitIdle を呼ばないこと、
# テクスチャとバッファがリングと1フレームの上限を共有すること、重なるバイト範囲が次のフレームへ送られること、
# 解放した区画宛てのコピーが無効になることを確かめる。
add_test(NAME GpuUploadRingVulkanTest COMMAND RHITextureUpdateVulkanTest --test=GpuUploadRingVulkanTest)
set_tests_properties(GpuUploadRingVulkanTest PROPERTIES
    LABELS "GPU;RenderingValidation"
    RESOURCE_LOCK NorvesLibGPU
    SKIP_RETURN_CODE 125
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# ミップ0のタイル1枚とミップテイルだけを結んだ sparse テクスチャを、材質のシェーダーの標本関数（常駐しなければ粗いミップへ逃げる）で
# 引く。結んでいない領域が粗いミップの色になり、黒や未定義の値にならないこと、ミップ1を結ぶと1段だけ下げて止まることを確かめる。
add_test(NAME VirtualTextureResidencyVulkanTest COMMAND RHITextureUpdateVulkanTest --test=VirtualTextureResidencyVulkanTest)
set_tests_properties(VirtualTextureResidencyVulkanTest PROPERTIES
    LABELS "GPU;RenderingValidation"
    RESOURCE_LOCK NorvesLibGPU
    SKIP_RETURN_CODE 125
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# 材質のシェーダーと同じ関数で sparse テクスチャを既知の UV の面に描き、タイルの要求（フィードバック）が GPU のバッファへ書かれることを確かめる。
# 常駐の領域は 4x4 画素のうち巡回する 1 画素だけが書き、非常駐で粗いミップへ逃げた領域は全画素が書くこと、同じタイルの要求が 1 件に減ること、
# パラメータ 0 では書かないこと、capacity を超えた件数が溢れた件数に残ることを確かめる。
add_test(NAME VirtualTextureFeedbackVulkanTest COMMAND RHITextureUpdateVulkanTest --test=VirtualTextureFeedbackVulkanTest)
set_tests_properties(VirtualTextureFeedbackVulkanTest PROPERTIES
    LABELS "GPU;RenderingValidation"
    RESOURCE_LOCK NorvesLibGPU
    SKIP_RETURN_CODE 125
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_test(NAME VideoMemoryBudgetVulkanTest COMMAND RHITextureUpdateVulkanTest --test=VideoMemoryBudgetVulkanTest)
set_tests_properties(VideoMemoryBudgetVulkanTest PROPERTIES
    LABELS "GPU;RenderingValidation"
    RESOURCE_LOCK NorvesLibGPU
    SKIP_RETURN_CODE 125
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# 今のフレームの深度から作る HZB（R32_FLOAT の全ミップ、2x2 の最大で縮める）の全ミップを読み戻す。奇数の大きさ（37x23 など）で、
# 各 texel が覆う深度の最大と一致し（はみ出す行・列も含める）、深度の入れ替え・解像度の変更でも作り直されることを確かめる。
add_test(NAME HiZPyramidVulkanTest COMMAND RHITextureUpdateVulkanTest --test=HiZPyramidVulkanTest)
set_tests_properties(HiZPyramidVulkanTest PROPERTIES
    LABELS "GPU;RenderingValidation"
    RESOURCE_LOCK NorvesLibGPU
    SKIP_RETURN_CODE 125
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Hi-Z による遮蔽の判定（Common/HiZOcclusion.glsl）を、合成した HZB と球の組（完全に隠れる・一部見える・近平面をまたぐ・
# 画面の端・小さくて 1 texel に収まる・乱数の場面）で確かめる。見えているものを隠れていると判定する誤りが 0 件であること。
add_test(NAME HiZOcclusionTestVulkanTest COMMAND RHITextureUpdateVulkanTest --test=HiZOcclusionTestVulkanTest)
set_tests_properties(HiZOcclusionTestVulkanTest PROPERTIES
    LABELS "GPU;RenderingValidation"
    RESOURCE_LOCK NorvesLibGPU
    SKIP_RETURN_CODE 125
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# ジオメトリのページの要求（Common/MegaGeometryCull.glsl のページの常駐の判定と要求の書き込み）を、実際のカリング（cluster_cull.comp）で確かめる。
# 合成したクラスタとページの表を置いて 1 回判定し、子のページが非常駐のとき穴を作らず親が描かれて要求が書かれること、同じページの要求が 1 件に減ること、
# 容量 0 では書かないこと、容量を超えた件数が溢れに残ること、自分の誤差が許容に収まる親は要求を出さないこと、同じフレームの印で重ねて積まないことを確かめる。
add_test(NAME GeometryPageRequestVulkanTest COMMAND RHITextureUpdateVulkanTest --test=GeometryPageRequestVulkanTest)
set_tests_properties(GeometryPageRequestVulkanTest PROPERTIES
    LABELS "GPU;RenderingValidation"
    RESOURCE_LOCK NorvesLibGPU
    SKIP_RETURN_CODE 125
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# R32_UINT・R32G32_UINT の添付へ、フラグメントシェーダーが「描画の番号 << 7 | gl_PrimitiveID」（ビジビリティバッファの ID と同じ並び）を書き、
# 読み戻して三角形ごとに期待の値になることを確かめる。覆われない画素は整数のクリア値（0xDEADBEEF）のままであること。
# geometryShader（gl_PrimitiveID に要る）が無いデバイスでは理由を出して 125 を返す。
add_test(NAME IntegerAttachmentVulkanTest COMMAND RHITextureUpdateVulkanTest --test=IntegerAttachmentVulkanTest)
set_tests_properties(IntegerAttachmentVulkanTest PROPERTIES
    LABELS "GPU;RenderingValidation"
    RESOURCE_LOCK NorvesLibGPU
    SKIP_RETURN_CODE 125
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# 計算シェーダーでのスキニング（skinning_compute.comp）を、実際の SkinnedMeshGpuStore が作る今・前のフレームのパレットで動かし、
# 読み戻した今・前の頂点（位置・法線・UV。ワールド空間）が CPU で計算した値と許容 1e-4 で一致することを確かめる。
# 骨 1〜4 本・重みの合計が 1 でない頂点・重み 0・範囲外の骨の番号・2 インスタンスの別の出力範囲・範囲外へ書かないこと・validation error 0 件。
add_test(NAME ComputeSkinningVulkanTest COMMAND RHITextureUpdateVulkanTest --test=ComputeSkinningVulkanTest)
set_tests_properties(ComputeSkinningVulkanTest PROPERTIES
    LABELS "GPU;RenderingValidation"
    RESOURCE_LOCK NorvesLibGPU
    SKIP_RETURN_CODE 125
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# 画面の 8x8 のタイルを材質ごとに分ける計算シェーダー（material_tile_classify.comp）を、合成した VisBuffer.Id の画像と描画の記録の表で動かし、
# 材質ごとのタイルの一覧・間接 dispatch の引数・統計が期待どおりになることを確かめる。3 つの材質が混じるタイル・空のタイル・部分タイル・
# 上限以上の材質・引けない ID・一覧が足りないときの切り詰めを含む。validation error 0 件。
add_test(NAME MaterialTileClassifyVulkanTest COMMAND RHITextureUpdateVulkanTest --test=MaterialTileClassifyVulkanTest)
set_tests_properties(MaterialTileClassifyVulkanTest PROPERTIES
    LABELS "GPU;RenderingValidation"
    RESOURCE_LOCK NorvesLibGPU
    SKIP_RETURN_CODE 125
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# ビジビリティバッファの幾何の解決（visbuffer_resolve.comp）を、合成した ID の画像・描画の記録の表・頂点とインデックスのバッファで動かし、
# GBuffer の Albedo・Normal・Velocity と、画素ごとの重心座標・UV・解析的な微分・接線の基底・前のクリップ座標が、
# ラスタライザと同じ式で作った CPU の参照（倍精度）と一致することを確かめる。手続き（32bit・16bit インデックス）・MegaGeometry・
# スキニング 2 体（1 つのバッファの別の位置）の記録、空の画素・引けない ID は何も書かないこと、前のカメラが無いと速度 0 を含む。
# 製品の版と検証用の版（書き出しつき）の両方で validation error 0 件。
add_test(NAME VisibilityResolveVulkanTest COMMAND RHITextureUpdateVulkanTest --test=VisibilityResolveVulkanTest)
set_tests_properties(VisibilityResolveVulkanTest PROPERTIES
    LABELS "GPU;RenderingValidation"
    RESOURCE_LOCK NorvesLibGPU
    SKIP_RETURN_CODE 125
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# 間接 dispatch（ICommandList::DispatchIndirect）を確認用の計算シェーダー（indirect_dispatch_probe.comp）で動かし、GPU 側の引数のとおりに
# スレッドグループが走ることを読み戻して確かめる。計算シェーダーが書いた引数の GenericRead での読み取り・転送で書いた引数の IndirectArgument での読み取り・
# 表の途中のオフセット・y への広げ（グループの番号 g = y * 数x + x）・x = 0・z > 1・不正な引数（false を返し何も記録しない）・
# バリアの段（DRAW_INDIRECT・INDIRECT_COMMAND_READ）を含む。validation error 0 件。
add_test(NAME IndirectDispatchVulkanTest COMMAND RHITextureUpdateVulkanTest --test=IndirectDispatchVulkanTest)
set_tests_properties(IndirectDispatchVulkanTest PROPERTIES
    LABELS "GPU;RenderingValidation"
    RESOURCE_LOCK NorvesLibGPU
    SKIP_RETURN_CODE 125
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

norves_add_test_bundle(RHITextureUpdateVulkanTest
    FOLDER "Test/Core/Rendering"
    MEMBER RHITextureUpdateVulkanTest RHITextureUpdateVulkanTest.cpp
    MEMBER RHITextureToBufferReadbackVulkanTest RHITextureToBufferReadbackVulkanTest.cpp
    MEMBER RHIBlockCompressedTextureVulkanTest RHIBlockCompressedTextureVulkanTest.cpp
    MEMBER VideoMemoryBudgetVulkanTest VideoMemoryBudgetVulkanTest.cpp
    MEMBER SparseCapabilitiesVulkanTest SparseCapabilitiesVulkanTest.cpp
    MEMBER SparseTextureVulkanTest SparseTextureVulkanTest.cpp
    MEMBER SparseBindVulkanTest SparseBindVulkanTest.cpp
    MEMBER SparseTileUploadVulkanTest SparseTileUploadVulkanTest.cpp
    MEMBER GpuUploadRingVulkanTest GpuUploadRingVulkanTest.cpp
    MEMBER VirtualTextureResidencyVulkanTest VirtualTextureResidencyVulkanTest.cpp
    MEMBER VirtualTextureFeedbackVulkanTest VirtualTextureFeedbackVulkanTest.cpp
    MEMBER VideoMemoryBudgetLogGateTest VideoMemoryBudgetLogGateTest.cpp
    MEMBER HiZPyramidVulkanTest HiZPyramidVulkanTest.cpp
    MEMBER HiZOcclusionTestVulkanTest HiZOcclusionTestVulkanTest.cpp
    MEMBER GeometryPageRequestVulkanTest GeometryPageRequestVulkanTest.cpp
    MEMBER IntegerAttachmentVulkanTest IntegerAttachmentVulkanTest.cpp
    MEMBER ComputeSkinningVulkanTest ComputeSkinningVulkanTest.cpp
    MEMBER MaterialTileClassifyVulkanTest MaterialTileClassifyVulkanTest.cpp
    MEMBER VisibilityResolveVulkanTest VisibilityResolveVulkanTest.cpp
    MEMBER IndirectDispatchVulkanTest IndirectDispatchVulkanTest.cpp
)
target_link_libraries(RHITextureUpdateVulkanTest PRIVATE Core)
target_include_directories(RHITextureUpdateVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
target_compile_definitions(RHITextureUpdateVulkanTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
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
register_rendering_gpu_test(
    RenderingVelocitySkinnedVulkanTest
    RenderingVelocityVulkanTest
    --scene=outdoor
    --capture-source=gbuffer-velocity
    --scenario=skinned-motion)

# 上の既定（ビジビリティバッファの On。GBuffer.Velocity を幾何の解決 visbuffer_resolve.comp が書く）に加えて、予備の経路
# （--visibility-buffer=off。装置が対応しないときに戻る GBuffer のラスタ）でも同じ速度の検証を通す。
# 静止・カメラ・物体・スキニングの動きの符号と大きさが、どちらの経路でも同じ期待値を満たすこと。
register_rendering_gpu_test(
    RenderingVelocityStaticGBufferFallbackVulkanTest
    RenderingVelocityVulkanTest
    --scene=indoor
    --capture-source=gbuffer-velocity
    --scenario=static
    --visibility-buffer=off)
register_rendering_gpu_test(
    RenderingVelocityMotionGBufferFallbackVulkanTest
    RenderingVelocityVulkanTest
    --scene=outdoor
    --capture-source=gbuffer-velocity
    --scenario=camera-object-motion
    --visibility-buffer=off)
register_rendering_gpu_test(
    RenderingVelocityObjectGBufferFallbackVulkanTest
    RenderingVelocityVulkanTest
    --scene=outdoor
    --capture-source=gbuffer-velocity
    --scenario=object-motion
    --visibility-buffer=off)
register_rendering_gpu_test(
    RenderingVelocitySkinnedGBufferFallbackVulkanTest
    RenderingVelocityVulkanTest
    --scene=outdoor
    --capture-source=gbuffer-velocity
    --scenario=skinned-motion
    --visibility-buffer=off)

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

add_test(NAME RenderingFloatImageTest COMMAND RenderingPerceptualDiffTest --test=RenderingFloatImageTest)

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
# 昼の露出（EV100 約14.6）で 1,000,000 nits の発光面が SceneColor で有限であることを確かめる
# （GBufferへプリエクスポージャ後の発光を書く規約の検証）。
register_rendering_gpu_test(
    RenderingHdrEmissiveDaylightVulkanTest
    RenderingHdrSceneCaptureTest
    --scene=indoor
    --capture-source=scene-color
    --emissive-scenario=daylight-1m-nits)
# 同じ露出で、プリエクスポージャ後の発光が32768を超え65504未満のとき頭打ちにならず
# そのまま SceneColor へ届くことを確かめる。
register_rendering_gpu_test(
    RenderingHdrEmissiveAboveHalfVulkanTest
    RenderingHdrSceneCaptureTest
    --scene=indoor
    --capture-source=scene-color
    --emissive-scenario=daylight-above-32768)

add_executable(RenderingRayTracingShadowVulkanTest
    RenderingRayTracingShadowVulkanTest.cpp
)

norves_add_test_bundle(PathTracingVulkanTest
    FOLDER "Test/Core/Rendering"
    MEMBER PathTracingVulkanTest PathTracingVulkanTest.cpp
    MEMBER PathTracingOutdoorVulkanTest PathTracingVulkanTest.cpp NORVES_PATH_TRACING_OUTDOOR_TEST
    MEMBER PathTracingVolumetricTest PathTracingVulkanTest.cpp NORVES_PATH_TRACING_VOLUMETRIC_TEST
)
target_link_libraries(PathTracingVulkanTest PRIVATE Core RenderingGpuTestSupport)
target_include_directories(PathTracingVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
target_compile_definitions(PathTracingVulkanTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
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
register_rendering_gpu_test(PathTracingOutdoorVulkanTest PathTracingVulkanTest --test=PathTracingOutdoorVulkanTest)
register_rendering_gpu_test(PathTracingVolumetricTest PathTracingVulkanTest --test=PathTracingVolumetricTest)
add_executable(PathTracingCameraTest PathTracingCameraTest.cpp)
target_link_libraries(PathTracingCameraTest PRIVATE Core)
set_target_properties(PathTracingCameraTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME PathTracingCameraTest COMMAND PathTracingCameraTest)
set(PATH_TRACING_EXR_OUTPUT_ROOT
    "${CMAKE_BINARY_DIR}/RenderingValidation/PathTracingExrOutputRuns")

norves_add_test_bundle(PathTracingCameraVulkanTest
    FOLDER "Test/Core/Rendering"
    MEMBER PathTracingCameraVulkanTest PathTracingCameraVulkanTest.cpp
    MEMBER R8PathTracingSequenceFrameVulkanTest R8PathTracingSequenceFrameVulkanTest.cpp
    MEMBER PathTracingExrOutputTest PathTracingCameraVulkanTest.cpp NORVES_EXR_OUTPUT_TEST NORVES_EXR_OUTPUT_ROOT="${PATH_TRACING_EXR_OUTPUT_ROOT}"
)
target_link_libraries(PathTracingCameraVulkanTest PRIVATE Core RenderingGpuTestSupport)
target_include_directories(PathTracingCameraVulkanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
target_compile_definitions(PathTracingCameraVulkanTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
register_rendering_gpu_test(PathTracingCameraVulkanTest PathTracingCameraVulkanTest)
# R8: PTの連番の1フレーム（前後のカメラ・変換とシャッター区間の固定、動きぼけの幅、CoC、履歴の継続）を実GPUで検証する。
register_rendering_gpu_test(R8PathTracingSequenceFrameVulkanTest PathTracingCameraVulkanTest --test=R8PathTracingSequenceFrameVulkanTest)
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
file(MAKE_DIRECTORY
    "${PATH_TRACING_EXR_OUTPUT_ROOT}/first"
    "${PATH_TRACING_EXR_OUTPUT_ROOT}/second"
    "${PATH_TRACING_EXR_OUTPUT_ROOT}/known")
add_test(NAME PathTracingExrOutputTest
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/PathTracingExrOutputTest.py
            $<TARGET_FILE:PathTracingCameraVulkanTest>
            ${PATH_TRACING_EXR_OUTPUT_ROOT})
set_property(TEST PathTracingExrOutputTest APPEND PROPERTY ENVIRONMENT "NORVES_TEST_BUNDLE_MEMBER=PathTracingExrOutputTest")
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


norves_add_test_bundle(RenderingPerceptualDiffTest
    FOLDER "Test/Core/Rendering"
    MEMBER RenderingPerceptualDiffTest RenderingPerceptualDiffTest.cpp
    MEMBER RenderingFloatImageTest RenderingFloatImageTest.cpp
)
target_link_libraries(RenderingPerceptualDiffTest PRIVATE RenderingPerceptualDiffSupport RenderingFloatImageSupport)
target_include_directories(RenderingPerceptualDiffTest PRIVATE
    ${CMAKE_SOURCE_DIR}/Test/Core/Rendering
)
target_compile_definitions(RenderingPerceptualDiffTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    NORVES_BINARY_ROOT="${CMAKE_BINARY_DIR}"
)
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
# R2の空のgoldenが今の空のモデルから作った画像と画素で一致することを確かめる（空のモデルを変えたら再承認が要る）。
add_test(NAME R2SkyGoldenCurrentModelTest
    COMMAND RenderingGoldenImageComparatorTest --compare-r2-sky-goldens)

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

# 既定（ビジビリティバッファの On。解決が GBuffer を書く）の基準画像は Indoor.png・Outdoor.png。
# 実際に解決を通ったこと（VISBUFFER_FALLBACK が無く VISBUFFER_RESOLVE_TILES がある）も、画像の比較の後にログで確かめる。
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
# 予備の経路（--visibility-buffer=off。geometryShader の無い装置が使う GBuffer のラスタ）の基準画像は
# IndoorGBufferFallback.png・OutdoorGBufferFallback.png。解決を通っていないこともログで確かめる。
register_rendering_gpu_test(
    RenderingGoldenIndoorGBufferFallbackVulkanTest
    RenderingGoldenImageTest
    --scene=indoor
    --capture-source=back-buffer
    --visibility-buffer=off)
register_rendering_gpu_test(
    RenderingGoldenOutdoorGBufferFallbackVulkanTest
    RenderingGoldenImageTest
    --scene=outdoor
    --capture-source=back-buffer
    --visibility-buffer=off)
# GTAO: 部屋の大きさ（Cornell）の壁・床・天井が遮蔽で黒くならず、隅・接地部だけが暗くなること
register_rendering_gpu_test(
    RenderingGTAOCornellRoomVulkanTest
    RenderingGoldenImageTest
    --scene=indoor
    --capture-source=back-buffer
    --ao-scenario=cornell-room)

add_test(NAME RenderingCoordinatorStatsPropagationTest COMMAND RenderingCoordinatorDiagnosticsSnapshotTest --test=RenderingCoordinatorStatsPropagationTest)


norves_add_test_bundle(RenderingCoordinatorDiagnosticsSnapshotTest
    FOLDER "Test/Core/Rendering"
    MEMBER RenderingCoordinatorDiagnosticsSnapshotTest RenderingCoordinatorDiagnosticsSnapshotTest.cpp
    MEMBER RenderingCoordinatorStatsPropagationTest RenderingCoordinatorStatsPropagationTest.cpp
)
target_link_libraries(RenderingCoordinatorDiagnosticsSnapshotTest PRIVATE Core)
target_include_directories(RenderingCoordinatorDiagnosticsSnapshotTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
add_test(NAME RenderingCoordinatorDiagnosticsSnapshotTest COMMAND RenderingCoordinatorDiagnosticsSnapshotTest)

add_test(NAME MeshBatcherTest COMMAND MeshBatcherInstancingTest --test=MeshBatcherTest)


norves_add_test_bundle(MeshBatcherInstancingTest
    FOLDER "Test/Core/Rendering"
    MEMBER MeshBatcherInstancingTest MeshBatcherInstancingTest.cpp
    MEMBER MeshBatcherTest MeshBatcherTest.cpp
    MEMBER InstanceDataFlattenTest InstanceDataFlattenTest.cpp
)
target_link_libraries(MeshBatcherInstancingTest PRIVATE Core)
target_include_directories(MeshBatcherInstancingTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
add_test(NAME MeshBatcherInstancingTest COMMAND MeshBatcherInstancingTest)

add_test(NAME InstanceDataFlattenTest COMMAND MeshBatcherInstancingTest --test=InstanceDataFlattenTest)

add_executable(DrawCommandSorterTest
    DrawCommandSorterTest.cpp
)
target_link_libraries(DrawCommandSorterTest PRIVATE Core)
target_include_directories(DrawCommandSorterTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(DrawCommandSorterTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME DrawCommandSorterTest COMMAND DrawCommandSorterTest)

add_test(NAME GBufferMaterialDescriptorCacheTest COMMAND MaterialResourcesTest --test=GBufferMaterialDescriptorCacheTest)

add_test(NAME ViewScreenTopologyTest COMMAND ViewportCameraIdRenderPlanTest --test=ViewScreenTopologyTest)

add_executable(SceneViewViewportCommandTest
    SceneViewViewportCommandTest.cpp
)
target_link_libraries(SceneViewViewportCommandTest PRIVATE Core)
target_include_directories(SceneViewViewportCommandTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(SceneViewViewportCommandTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME SceneViewViewportCommandTest COMMAND SceneViewViewportCommandTest)


norves_add_test_bundle(CameraViewConstantsTest
    FOLDER "Test/Core/Rendering"
    MEMBER CameraViewConstantsTest CameraViewConstantsTest.cpp
    MEMBER OrthographicViewportMatrixTest OrthographicViewportMatrixTest.cpp
    MEMBER PointShadowFaceMatricesTest PointShadowFaceMatricesTest.cpp
    MEMBER VirtualShadowMapClipmapTest VirtualShadowMapClipmapTest.cpp
    MEMBER AutoExposureMathTest AutoExposureMathTest.cpp
    MEMBER TemporalAAJitterTest TemporalAAJitterTest.cpp
)
target_link_libraries(CameraViewConstantsTest PRIVATE Core)
target_include_directories(CameraViewConstantsTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
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

add_test(NAME CameraTableTest COMMAND ViewportCameraIdRenderPlanTest --test=CameraTableTest)


norves_add_test_bundle(ViewportCameraIdRenderPlanTest
    FOLDER "Test/Core/Rendering"
    MEMBER ViewportCameraIdRenderPlanTest ViewportCameraIdRenderPlanTest.cpp
    MEMBER ViewScreenTopologyTest ViewScreenTopologyTest.cpp
    MEMBER CameraTableTest CameraTableTest.cpp
)
target_link_libraries(ViewportCameraIdRenderPlanTest PRIVATE Core)
target_include_directories(ViewportCameraIdRenderPlanTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
add_test(NAME ViewportCameraIdRenderPlanTest COMMAND ViewportCameraIdRenderPlanTest)

add_test(NAME OrthographicViewportMatrixTest COMMAND CameraViewConstantsTest --test=OrthographicViewportMatrixTest)
add_test(NAME PointShadowFaceMatricesTest COMMAND CameraViewConstantsTest --test=PointShadowFaceMatricesTest)
add_test(NAME VirtualShadowMapClipmapTest COMMAND CameraViewConstantsTest --test=VirtualShadowMapClipmapTest)
add_test(NAME AutoExposureMathTest COMMAND CameraViewConstantsTest --test=AutoExposureMathTest)
add_test(NAME TemporalAAJitterTest COMMAND CameraViewConstantsTest --test=TemporalAAJitterTest)

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

add_test(NAME DebugViewModeStringTest COMMAND ViewportSnapshotDebugWiringTest --test=DebugViewModeStringTest)

add_executable(DebugDrawQueueTest
    DebugDrawQueueTest.cpp
)
target_link_libraries(DebugDrawQueueTest PRIVATE Core)
target_include_directories(DebugDrawQueueTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(DebugDrawQueueTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME DebugDrawQueueTest COMMAND DebugDrawQueueTest)

add_test(NAME ViewportDebugViewModeReleaseClampTest COMMAND ViewportSnapshotDebugWiringTest --test=ViewportDebugViewModeReleaseClampTest)


norves_add_test_bundle(ViewportSnapshotDebugWiringTest
    FOLDER "Test/Core/Rendering"
    MEMBER ViewportSnapshotDebugWiringTest ViewportSnapshotDebugWiringTest.cpp
    MEMBER DebugViewModeStringTest DebugViewModeStringTest.cpp
    MEMBER ViewportDebugViewModeReleaseClampTest ViewportDebugViewModeReleaseClampTest.cpp
    MEMBER MegaGeometryFrameCommandDebugModeTest MegaGeometryFrameCommandDebugModeTest.cpp
)
target_link_libraries(ViewportSnapshotDebugWiringTest PRIVATE Core)
target_include_directories(ViewportSnapshotDebugWiringTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
target_compile_definitions(ViewportSnapshotDebugWiringTest PRIVATE
    NORVES_SHADER_DIR="${CMAKE_SOURCE_DIR}/Assets/Shaders"
)
add_test(NAME ViewportSnapshotDebugWiringTest COMMAND ViewportSnapshotDebugWiringTest)

add_test(NAME MegaGeometryFrameCommandDebugModeTest COMMAND ViewportSnapshotDebugWiringTest --test=MegaGeometryFrameCommandDebugModeTest)

add_test(NAME SceneViewProxyReconcileTest COMMAND SceneViewProxyLookupTest --test=SceneViewProxyReconcileTest)


norves_add_test_bundle(SceneViewProxyLookupTest
    FOLDER "Test/Core/Rendering"
    MEMBER SceneViewProxyLookupTest SceneViewProxyLookupTest.cpp
    MEMBER SceneViewProxyReconcileTest SceneViewProxyReconcileTest.cpp
)
target_link_libraries(SceneViewProxyLookupTest PRIVATE Core)
target_include_directories(SceneViewProxyLookupTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
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


norves_add_test_bundle(CanvasViewRenderTest
    FOLDER "Test/Core/Rendering"
    MEMBER CanvasViewRenderTest CanvasViewRenderTest.cpp
    MEMBER CanvasLayerCompositeSnapshotTest CanvasLayerCompositeSnapshotTest.cpp
)
target_link_libraries(CanvasViewRenderTest PRIVATE Core)
target_include_directories(CanvasViewRenderTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
add_test(NAME CanvasViewRenderTest COMMAND CanvasViewRenderTest)

add_test(NAME CanvasLayerCompositeSnapshotTest COMMAND CanvasViewRenderTest --test=CanvasLayerCompositeSnapshotTest)

add_executable(BoardInstanceBatchingTest
    BoardInstanceBatchingTest.cpp
)
target_link_libraries(BoardInstanceBatchingTest PRIVATE Core)
target_include_directories(BoardInstanceBatchingTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(BoardInstanceBatchingTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME BoardInstanceBatchingTest COMMAND BoardInstanceBatchingTest)


norves_add_test_bundle(BoardComponentRoutingTest
    FOLDER "Test/Core/Rendering"
    MEMBER BoardComponentRoutingTest BoardComponentRoutingTest.cpp
    MEMBER BillboardComponentTest BillboardComponentTest.cpp
)
target_link_libraries(BoardComponentRoutingTest PRIVATE Core)
target_include_directories(BoardComponentRoutingTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
add_test(NAME BoardComponentRoutingTest COMMAND BoardComponentRoutingTest)

add_test(NAME BillboardComponentTest COMMAND BoardComponentRoutingTest --test=BillboardComponentTest)

add_executable(ImpostorBakeServiceTest
    ImpostorBakeServiceTest.cpp
)
target_link_libraries(ImpostorBakeServiceTest PRIVATE Core)
target_include_directories(ImpostorBakeServiceTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
set_target_properties(ImpostorBakeServiceTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME ImpostorBakeServiceTest COMMAND ImpostorBakeServiceTest)

add_test(NAME ImpostorOctahedralTest COMMAND ImpostorLODSwitchTest --test=ImpostorOctahedralTest)


norves_add_test_bundle(ImpostorLODSwitchTest
    FOLDER "Test/Core/Rendering"
    MEMBER ImpostorLODSwitchTest ImpostorLODSwitchTest.cpp
    MEMBER ImpostorOctahedralTest ImpostorOctahedralTest.cpp
)
target_link_libraries(ImpostorLODSwitchTest PRIVATE Core)
target_include_directories(ImpostorLODSwitchTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
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

add_test(NAME SpriteSheetUVRectTest COMMAND FlipbookAdvanceTest --test=SpriteSheetUVRectTest)


norves_add_test_bundle(FlipbookAdvanceTest
    FOLDER "Test/Core/Rendering"
    MEMBER FlipbookAdvanceTest FlipbookAdvanceTest.cpp
    MEMBER SpriteSheetUVRectTest SpriteSheetUVRectTest.cpp
)
target_link_libraries(FlipbookAdvanceTest PRIVATE Core)
target_include_directories(FlipbookAdvanceTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
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

add_test(NAME BoardSortKeyStableTest COMMAND MultiLayerOrderTest --test=BoardSortKeyStableTest)


norves_add_test_bundle(MultiLayerOrderTest
    FOLDER "Test/Core/Rendering"
    MEMBER MultiLayerOrderTest MultiLayerOrderTest.cpp
    MEMBER BoardSortKeyStableTest BoardSortKeyStableTest.cpp
)
target_link_libraries(MultiLayerOrderTest PRIVATE Core)
target_include_directories(MultiLayerOrderTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
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

add_test(NAME RenderGraphTextureUsageContractTest COMMAND RenderGraphCompileTest --test=RenderGraphTextureUsageContractTest)


norves_add_test_bundle(RenderGraphCompileTest
    FOLDER "Test/Core/Rendering"
    MEMBER RenderGraphCompileTest RenderGraphCompileTest.cpp
    MEMBER RenderGraphTextureUsageContractTest RenderGraphTextureUsageContractTest.cpp
    MEMBER RenderGraphDumpTest RenderGraphDumpTest.cpp
    MEMBER LegacyViewPassAdapterTest LegacyViewPassAdapterTest.cpp
)
target_link_libraries(RenderGraphCompileTest PRIVATE Core)
target_include_directories(RenderGraphCompileTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
target_compile_definitions(RenderGraphCompileTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
add_test(NAME RenderGraphCompileTest COMMAND RenderGraphCompileTest)


norves_add_test_bundle(RenderGraphLifetimeTest
    FOLDER "Test/Core/Rendering"
    MEMBER RenderGraphLifetimeTest RenderGraphLifetimeTest.cpp
    MEMBER RenderGraphAttachmentStateTest RenderGraphAttachmentStateTest.cpp
)
target_link_libraries(RenderGraphLifetimeTest PRIVATE Core)
target_include_directories(RenderGraphLifetimeTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
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

add_test(NAME RenderGraphAttachmentStateTest COMMAND RenderGraphLifetimeTest --test=RenderGraphAttachmentStateTest)

add_test(NAME RenderGraphDumpTest COMMAND RenderGraphCompileTest --test=RenderGraphDumpTest)

add_test(NAME LegacyViewPassAdapterTest COMMAND RenderGraphCompileTest --test=LegacyViewPassAdapterTest)

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

add_test(NAME TextureHandleCacheTest COMMAND TextureResourcesTextureAssetTest --test=TextureHandleCacheTest)

add_test(NAME ModelHandleCacheTest COMMAND ModelResourcesAssetRuntimeTest --test=ModelHandleCacheTest)

add_test(NAME ModelAsyncLoadQueueTest COMMAND ModelResourcesAssetRuntimeTest --test=ModelAsyncLoadQueueTest)


norves_add_test_bundle(ModelResourcesAssetRuntimeTest
    FOLDER "Test/Core/Rendering"
    MEMBER ModelResourcesAssetRuntimeTest ModelResourcesAssetRuntimeTest.cpp
    MEMBER ModelHandleCacheTest ModelHandleCacheTest.cpp
    MEMBER ModelAsyncLoadQueueTest ModelAsyncLoadQueueTest.cpp
)
target_link_libraries(ModelResourcesAssetRuntimeTest PRIVATE Core)
target_include_directories(ModelResourcesAssetRuntimeTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
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

add_test(NAME TextureAssetReloadAdmissionTest COMMAND TextureResourcesTextureAssetTest --test=TextureAssetReloadAdmissionTest)

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
    # 外部のエディタと cargo が要るので、常設の段から外す。
    set_property(TEST AssetManifestReloadBridgeLoopbackTest APPEND PROPERTY LABELS Reference)
endif()

add_test(NAME ModelResourcesAssetLoadProfileContractTest COMMAND TextureResourcesAssetLoadProfileContractTest --test=ModelResourcesAssetLoadProfileContractTest)

set(ASSET_LOAD_PROFILE_SUMMARY_CONTRACT_DIR
    "${CMAKE_BINARY_DIR}/AssetLoadProfileSummaryContract/$<CONFIG>"
)
add_test(NAME AssetLoadProfileSummaryContractTest
    COMMAND ${CMAKE_COMMAND}
        "-DSUMMARY_SCRIPT=${CMAKE_SOURCE_DIR}/Scripts/SummarizeAssetLoadProfile.ps1"
        "-DCASE_DIR=${ASSET_LOAD_PROFILE_SUMMARY_CONTRACT_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/AssetLoadProfileSummaryContract.cmake.in"
)

add_test(NAME TextureAsyncTypesTest COMMAND TextureResourcesAssetLoadProfileContractTest --test=TextureAsyncTypesTest)


norves_add_test_bundle(TextureResourcesTextureAssetTest
    FOLDER "Test/Core/Rendering"
    MEMBER TextureResourcesTextureAssetTest TextureResourcesTextureAssetTest.cpp
    MEMBER TextureHandleCacheTest TextureHandleCacheTest.cpp
    MEMBER TextureAssetReloadAdmissionTest TextureAssetReloadAdmissionTest.cpp
)
target_link_libraries(TextureResourcesTextureAssetTest PRIVATE Core)
target_include_directories(TextureResourcesTextureAssetTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
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


norves_add_test_bundle(TextureResourcesAssetLoadProfileContractTest
    FOLDER "Test/Core/Rendering"
    MEMBER TextureResourcesAssetLoadProfileContractTest TextureResourcesAssetLoadProfileContractTest.cpp
    MEMBER ModelResourcesAssetLoadProfileContractTest ModelResourcesAssetLoadProfileContractTest.cpp
    MEMBER TextureAsyncTypesTest TextureAsyncTypesTest.cpp
)
target_link_libraries(TextureResourcesAssetLoadProfileContractTest PRIVATE Core)
target_include_directories(TextureResourcesAssetLoadProfileContractTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
add_test(NAME TextureResourcesAssetLoadProfileContractTest COMMAND TextureResourcesAssetLoadProfileContractTest)


norves_add_test_bundle(RenderResourcesDomainContractTest
    FOLDER "Test/Core/Rendering"
    MEMBER RenderResourcesDomainContractTest RenderResourcesDomainContractTest.cpp
    MEMBER MeshResourcesProceduralGpuTest MeshResourcesProceduralGpuTest.cpp
    MEMBER TextureMemoryLedgerTest TextureMemoryLedgerTest.cpp
    MEMBER VideoMemoryBudgetManagerTest VideoMemoryBudgetManagerTest.cpp
    MEMBER GpuRetireQueueTest GpuRetireQueueTest.cpp
    MEMBER GeometryPoolAllocatorTest GeometryPoolAllocatorTest.cpp
    MEMBER GeometryPageStreamerTest GeometryPageStreamerTest.cpp
    MEMBER VirtualTextureRequestSetTest VirtualTextureRequestSetTest.cpp
    MEMBER VirtualTextureStreamerTest VirtualTextureStreamerTest.cpp
    MEMBER RHIBlockCompressedFormatTest RHIBlockCompressedFormatTest.cpp
    MEMBER VisibilityBufferEncodingTest VisibilityBufferEncodingTest.cpp
    MEMBER VisibilityMaterialTableTest VisibilityMaterialTableTest.cpp
)
target_link_libraries(RenderResourcesDomainContractTest PRIVATE Core)
target_include_directories(RenderResourcesDomainContractTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
target_compile_definitions(RenderResourcesDomainContractTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
add_test(NAME RenderResourcesDomainContractTest COMMAND RenderResourcesDomainContractTest)

add_test(NAME MeshResourcesProceduralGpuTest COMMAND RenderResourcesDomainContractTest --test=MeshResourcesProceduralGpuTest)
add_test(NAME TextureMemoryLedgerTest COMMAND RenderResourcesDomainContractTest --test=TextureMemoryLedgerTest)
add_test(NAME VideoMemoryBudgetManagerTest COMMAND RenderResourcesDomainContractTest --test=VideoMemoryBudgetManagerTest)
add_test(NAME GpuRetireQueueTest COMMAND RenderResourcesDomainContractTest --test=GpuRetireQueueTest)
# ジオメトリの共有プールの区画の確保・解放・結合・整列・断片化の統計、塊の追加と上限・失敗、遅延解放（GpuRetireQueue）の提出順。
add_test(NAME GeometryPoolAllocatorTest COMMAND RenderResourcesDomainContractTest --test=GeometryPoolAllocatorTest)
# ジオメトリのページのストリーマ（読み込み・書き込み・公開・追い出しは偽の窓口）。優先度・1 フレームの上限・根のページの常駐・
# 書き終えてからの公開・親が先・追い出しの順（LRU・子から）と目標以下への収まり・外した区画の再利用の順序・失敗の再試行・
# メッシュの解放と、ページの配置の計算（範囲の検査・親子の関係・クラスタの記録の位置の書き換え）。
add_test(NAME GeometryPageStreamerTest COMMAND RenderResourcesDomainContractTest --test=GeometryPageStreamerTest)
# 1語へ詰めた VT の要求の復号・重複の除去・溢れた件数と、3つのバッファのリングが完了したものだけを GPU を待たずに読み戻すこと。
add_test(NAME VirtualTextureRequestSetTest COMMAND RenderResourcesDomainContractTest --test=VirtualTextureRequestSetTest)
# 要求の集合から未常駐のタイルを優先度順に読み、ページを結び、コピーを積んで常駐させること。1 フレームの上限・同じタイルの二重の要求・読み込みの失敗・プールが尽きたときの扱い。
add_test(NAME VirtualTextureStreamerTest COMMAND RenderResourcesDomainContractTest --test=VirtualTextureStreamerTest)
add_test(NAME RHIBlockCompressedFormatTest COMMAND RenderResourcesDomainContractTest --test=RHIBlockCompressedFormatTest)
# ビジビリティバッファの ID（(記録の番号 << 7) | 三角形の番号）の往復・記録の数の上限（2^25）・空（0）の扱い、描画の記録の表の追加・引き・容量、VisBuffer.Id と記録の表の RenderGraph の資源の記述、GLSL（Common/VisibilityBuffer.glsl）の定数の C++ との一致。
add_test(NAME VisibilityBufferEncodingTest COMMAND RenderResourcesDomainContractTest --test=VisibilityBufferEncodingTest)
# 記録の材質の番号がフレームで一意な材質の表の番号になること。同じ材質（MegaGeometry の区間・手続き・スキニングの描画）が同じ番号に、違う材質が違う番号に、番号が 0 から詰まること、上限を超えたときに予備の番号へ寄せること、記録を作る計算シェーダーが表の番号を書くこと。
add_test(NAME VisibilityMaterialTableTest COMMAND RenderResourcesDomainContractTest --test=VisibilityMaterialTableTest)

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
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
set_target_properties(MegaGeometryResourcesTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME MegaGeometryResourcesTest COMMAND MegaGeometryResourcesTest)


norves_add_test_bundle(MaterialResourcesTest
    FOLDER "Test/Core/Rendering"
    MEMBER MaterialResourcesTest MaterialResourcesTest.cpp
    MEMBER GBufferMaterialDescriptorCacheTest GBufferMaterialDescriptorCacheTest.cpp
)
target_link_libraries(MaterialResourcesTest PRIVATE Core)
target_include_directories(MaterialResourcesTest PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Library/Core/Private
)
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
    add_test(NAME FontAtlasTest COMMAND TextComponentBoardProxyTest --test=FontAtlasTest)


norves_add_test_bundle(TextComponentBoardProxyTest
    FOLDER "Test/Core/Rendering"
    MEMBER TextComponentBoardProxyTest TextComponentBoardProxyTest.cpp
    MEMBER FontAtlasTest FontAtlasTest.cpp
)
target_link_libraries(TextComponentBoardProxyTest PRIVATE Core)
target_include_directories(TextComponentBoardProxyTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
    add_test(NAME TextComponentBoardProxyTest COMMAND TextComponentBoardProxyTest)
endif()

add_executable(ParticleSystemTest
    ParticleSystemTest.cpp
)
target_link_libraries(ParticleSystemTest PRIVATE Core)
set_target_properties(ParticleSystemTest PROPERTIES FOLDER "Test/Core/Rendering")
add_test(NAME ParticleSystemTest COMMAND ParticleSystemTest)

add_test(NAME ParticleFrameSnapshotTest COMMAND CanvasTransientBoardSnapshotTest --test=ParticleFrameSnapshotTest)


norves_add_test_bundle(CanvasTransientBoardSnapshotTest
    FOLDER "Test/Core/Rendering"
    MEMBER CanvasTransientBoardSnapshotTest CanvasTransientBoardSnapshotTest.cpp
    MEMBER ParticleFrameSnapshotTest ParticleFrameSnapshotTest.cpp
)
target_link_libraries(CanvasTransientBoardSnapshotTest PRIVATE Core)
target_include_directories(CanvasTransientBoardSnapshotTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)
target_compile_definitions(CanvasTransientBoardSnapshotTest PRIVATE
    NORVES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
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

# 普段は回さず、節目（受入れ）にだけ `ctest -L Reference` で回すテスト。実行に数分から1時間以上かかる
# PT参照比較と連番の確認、閾値が決めごとの受入れの記録、参照比較の道具とスクリプトの自己検査。
set(NORVES_REFERENCE_TESTS
    PathTracingConvergenceVulkanTest
    PathTracingRasterParityVulkanTest
    R4DDGIPathTracingReferenceVulkanTest
    R6RTGIPathTracingReferenceVulkanTest
    R7OutdoorPathTracingReferenceVulkanTest
    R8DepthOfFieldPathTracingReferenceVulkanTest
    R8MotionBlurPathTracingReferenceVulkanTest
    R8SequenceSmokeTest
    R6RTGIAcceptanceVulkanTest
    RenderingDDGIVulkanTest
    RenderingDDGIVulkanDynamicTest
    RasterPathTracingComparisonTest
    R8ExrSequenceValidatorTest
    AssetLoadProfileSummaryContractTest
)
foreach(test_name IN LISTS NORVES_REFERENCE_TESTS)
    set_property(TEST ${test_name} APPEND PROPERTY LABELS Reference)
endforeach()

#include "VulkanImGuiRenderer.h"

// このファイル全体はゲート ON 時のみ意味を持つ（OFF 時は空 TU）。
#if defined(NORVES_ENABLE_IMGUI)

#include "VulkanDevice.h"
#include "VulkanRenderPass.h"
#include "VulkanCommandList.h"
#include "Logging/LogMacros.h"

// Core は VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1 で関数を動的ロードするため、
// imgui_impl_vulkan も生プロトタイプを使わず LoadFunctions 経由で解決させる。
// IMGUI_IMPL_VULKAN_NO_PROTOTYPES は CMake の ON ブロックで Core PRIVATE に
// 付与する（impl_vulkan.cpp と本 TU の両方が一貫して見る）。
#include "imgui.h"
#include "imgui_impl_vulkan.h"

namespace NorvesLib::RHI::Vulkan
{

    VulkanImGuiRenderer::VulkanImGuiRenderer(VulkanDevice *device)
        : m_device(device)
    {
    }

    VulkanImGuiRenderer::~VulkanImGuiRenderer()
    {
        // 冪等 Shutdown。device は本クラスより長命である前提（連れ破棄順序）。
        Shutdown();
    }

    void VulkanImGuiRenderer::CheckVkResult(VkResult result)
    {
        // imgui バックエンド内部の Vk 呼び出し失敗を握り潰さずログへ出す。
        if (result != VK_SUCCESS)
        {
            NORVES_LOG_ERROR("ImGui", "imgui バックエンド内部の Vulkan 呼び出しが失敗 (VkResult=%d)",
                             static_cast<int>(result));
        }
    }

    bool VulkanImGuiRenderer::Initialize(IRenderPass *loadRenderPass, uint32_t minImageCount, uint32_t imageCount)
    {
        // ImGui コンテキストは呼び出し側（ImGuiModule）が事前生成済みである前提。
        // 本クラスは ImGui::CreateContext を呼ばない。
        if (m_bInitialized)
        {
            return true;
        }
        if (m_device == nullptr || loadRenderPass == nullptr)
        {
            NORVES_LOG_ERROR("ImGui", "VulkanImGuiRenderer::Initialize: device/renderPass が null");
            return false;
        }

        // load render pass（最終 blit 後の back buffer へ overlay を load-blend する）。
        auto *vulkanRenderPass = static_cast<VulkanRenderPass *>(loadRenderPass);
        const vk::RenderPass vkRenderPass = vulkanRenderPass->GetVkRenderPass();

        // imgui 1.92.8 は分割ディスクリプタモデル（SAMPLED_IMAGE + SAMPLER）を採用し、
        // 旧 eCombinedImageSampler を撤去した。自前プールを構築すると必要型が 0 個になり
        // vkAllocateDescriptorSets が VK_ERROR_OUT_OF_POOL_MEMORY で実行時クラッシュする。
        // そのため DescriptorPool=VK_NULL_HANDLE + DescriptorPoolSize>0 とし、imgui に
        // 正しい型（SAMPLED_IMAGE + SAMPLER）の内部プールを own/解放させる。
        // DescriptorPoolSize は IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE(=8) 以上。
        constexpr uint32_t kDescriptorPoolSize = 64;

        // imgui のインスタンス apiVersion は VkApplicationInfo::apiVersion と一致させる必要がある
        // （VulkanDevice::CreateInstance が設定した値を供給する。ハードコードしない）。
        const uint32_t apiVersion = m_device->GetInstanceApiVersion();

        // 生 Vk ハンドルへ降格して InitInfo を構成する。
        ImGui_ImplVulkan_InitInfo initInfo = {};
        initInfo.ApiVersion = apiVersion;
        initInfo.Instance = static_cast<VkInstance>(m_device->GetVkInstance());
        initInfo.PhysicalDevice = static_cast<VkPhysicalDevice>(m_device->GetVkPhysicalDevice());
        initInfo.Device = static_cast<VkDevice>(m_device->GetVkDevice());
        initInfo.QueueFamily = m_device->GetGraphicsQueueFamilyIndex();
        initInfo.Queue = static_cast<VkQueue>(m_device->GetGraphicsQueue());
        initInfo.DescriptorPool = VK_NULL_HANDLE;             // imgui に内部生成させる
        initInfo.DescriptorPoolSize = kDescriptorPoolSize;    // >0 で imgui が正しい型のプールを own
        initInfo.CheckVkResultFn = &VulkanImGuiRenderer::CheckVkResult; // 内部失敗をログ化
        initInfo.MinImageCount = minImageCount;
        initInfo.ImageCount = imageCount;
        initInfo.UseDynamicRendering = false;
        // imgui 1.92: RenderPass/MSAASamples は PipelineInfoMain に移動済み。
        initInfo.PipelineInfoMain.RenderPass = static_cast<VkRenderPass>(vkRenderPass);
        initInfo.PipelineInfoMain.Subpass = 0;
        initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

        // Core は動的ディスパッチャを使うため、Init 前に必ず関数ローダを供給する。
        // imgui 1.92 の LoadFunctions は (api_version, loader_func, user_data) 形式。
        // ローダは instance とともに defaultDispatcher の vkGetInstanceProcAddr を呼ぶ。
        const bool bLoaded = ImGui_ImplVulkan_LoadFunctions(
            apiVersion,
            [](const char *functionName, void *userData) -> PFN_vkVoidFunction
            {
                auto vkInstance = reinterpret_cast<VkInstance>(userData);
                return VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr(vkInstance, functionName);
            },
            reinterpret_cast<void *>(static_cast<VkInstance>(m_device->GetVkInstance())));

        if (!bLoaded)
        {
            NORVES_LOG_ERROR("ImGui", "ImGui_ImplVulkan_LoadFunctions に失敗");
            return false;
        }

        if (!ImGui_ImplVulkan_Init(&initInfo))
        {
            NORVES_LOG_ERROR("ImGui", "ImGui_ImplVulkan_Init に失敗");
            return false;
        }

        m_bInitialized = true;
        return true;
    }

    void VulkanImGuiRenderer::Shutdown()
    {
        // 二重 Shutdown を冪等にする。device 生存中前提。
        // DescriptorPool は imgui が内部で own/解放するため本クラスでは破棄しない。
        if (m_bInitialized)
        {
            // imgui バックエンドの Vulkan リソース（内部 DescriptorPool / パイプライン /
            // フレームバッファ等）を解放する前に GPU を idle まで待つ。ST モードでは
            // RenderThread が非起動で、最後のインライン RenderFrame が投入した GPU work の
            // 完了が module 経由 Shutdown 時点で保証されないため、使用中リソースの解放で
            // AV になる（MT は RenderThread::Stop の Join で暗黙に idle 保証される）。
            // WaitIdle は MT でも既に idle なら即返るため両モードで安全・冪等。
            if (m_device != nullptr)
            {
                m_device->WaitIdle();
            }
            ImGui_ImplVulkan_Shutdown();
            m_bInitialized = false;
        }
    }

    bool VulkanImGuiRenderer::BuildFontAtlas()
    {
        if (!m_bInitialized)
        {
            return false;
        }

        // imgui 1.92 は ImGuiBackendFlags_RendererHasTextures による動的アトラスを採用し、
        // 旧 ImGui_ImplVulkan_CreateFontsTexture は撤去された。ここではアトラスの
        // ピクセルデータ生成（Build）のみを明示的に行い、GPU アップロードは
        // UploadFontAtlas() が GameThread で同期実行する（MT 安全化）。
        ImGuiIO &io = ImGui::GetIO();
        if (io.Fonts == nullptr)
        {
            return false;
        }
        return io.Fonts->Build();
    }

    bool VulkanImGuiRenderer::UploadFontAtlas()
    {
        // MT 安全化の核。imgui の動的テクスチャは本来 RecordDrawData（RenderThread）内で
        // ImGui_ImplVulkan_UpdateTexture により遅延アップロードされるが、その経路は
        // ImTextureData::Status を GameThread（NewFrame）と RenderThread（描画）が無ロックで
        // 共有書込みする競合を生む。ここで GameThread の初期化時（フレーム未投入＝グラフィックス
        // キューがアイドルで RenderThread と同時 submit し得ない時点）に全テクスチャを同期
        // アップロードして Status=OK に確定し、以後 RenderThread がテクスチャに触れないようにする。
        //
        // ImGui_ImplVulkan_UpdateTexture は内部専用のコマンドプール（Init で生成済み）に
        // copy + バリアを記録し自前で submit して vkQueueWaitIdle する自己完結処理であり、
        // render pass の内外いずれからも（録画窓に依存せず）呼べる。よって GameThread から
        // 直接呼んでフォントアトラスを GPU へ載せられる。
        if (!m_bInitialized)
        {
            return false;
        }

        ImGuiIO &io = ImGui::GetIO();
        if (io.Fonts == nullptr)
        {
            return false;
        }

        // アトラスのピクセルが未生成なら確実に生成しておく（冪等）。
        if (!io.Fonts->TexIsBuilt)
        {
            io.Fonts->Build();
        }

        // アトラス所有のテクスチャ一覧（TexList）を直接走査する。PlatformIO.Textures は
        // ImGui::Render（EndFrame）でしか再構築されないため、初期化時点では空のことがある。
        // TexList はアトラス所有で Build/NewFrame 後に当該テクスチャ（WantCreate）を含む。
        bool bAllOk = true;
        for (ImTextureData *tex : io.Fonts->TexList)
        {
            if (tex == nullptr)
            {
                continue;
            }
            if (tex->Status != ImTextureStatus_OK)
            {
                // 同期アップロード（内部で submit + WaitIdle し Status=OK に遷移）。
                ImGui_ImplVulkan_UpdateTexture(tex);
            }
            if (tex->Status != ImTextureStatus_OK)
            {
                bAllOk = false;
                NORVES_LOG_ERROR("ImGui",
                                 "UploadFontAtlas: テクスチャのアップロード後も Status!=OK (UniqueID=%d)",
                                 tex->UniqueID);
            }
        }

        return bAllOk;
    }

    void VulkanImGuiRenderer::RecordDrawData(ICommandList *commandList, const void *imguiDrawData)
    {
        // BeginRenderPass 済みの録画窓内で呼ばれる前提。自身では Begin/End しない。
        if (!m_bInitialized || commandList == nullptr || imguiDrawData == nullptr)
        {
            return;
        }

        auto *vulkanCommandList = static_cast<VulkanCommandList *>(commandList);
        const vk::CommandBuffer vkCmd = vulkanCommandList->GetVkCommandBuffer();
        if (!vkCmd)
        {
            return;
        }

        // ImDrawData* は IImGuiRenderer 抽象を imgui 非依存に保つため void* で受ける。
        ImDrawData *drawData = const_cast<ImDrawData *>(static_cast<const ImDrawData *>(imguiDrawData));

        // MT 安全化（多重防御）: ImGui_ImplVulkan_RenderDrawData は draw_data->Textures が
        // 非 null だと先頭で各 ImTextureData の Status をチェックし、未確定なら
        // ImGui_ImplVulkan_UpdateTexture でアップロードしようとする（＝RenderThread が
        // ImTextureData::Status を書込み、GameThread の NewFrame と競合する）。本構成では
        // アップロードは GameThread の UploadFontAtlas で完了済み（Status=OK・静的アトラス）の
        // ため、ここで Textures を null 化して RenderThread のテクスチャ更新ループを構造的に
        // 完全スキップさせる。これによりスナップショット側の対策と二重に、RenderThread が
        // ImTextureData へ一切触れないことを保証する。
        drawData->Textures = nullptr;

        ImGui_ImplVulkan_RenderDrawData(drawData, static_cast<VkCommandBuffer>(vkCmd));
    }

    void VulkanImGuiRenderer::NotifySwapChainRecreated(uint32_t minImageCount, uint32_t /*imageCount*/)
    {
        if (!m_bInitialized)
        {
            return;
        }
        // imgui は最小イメージ数のみ更新 API を公開する。
        ImGui_ImplVulkan_SetMinImageCount(minImageCount);
    }

} // namespace NorvesLib::RHI::Vulkan

#endif // defined(NORVES_ENABLE_IMGUI)

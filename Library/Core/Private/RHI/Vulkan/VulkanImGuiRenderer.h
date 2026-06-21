#pragma once

// このファイルは ImGui オーバーレイのゲート ON 時のみ意味を持つ。
// OFF 時は中身が一切無く空 TU となる（imgui / impl_vulkan を 1 行も見ない）。
#if defined(NORVES_ENABLE_IMGUI)

#include "RHI/IImGuiRenderer.h"

#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>

namespace NorvesLib::RHI
{
    class IRenderPass;
    class ICommandList;
} // namespace NorvesLib::RHI

namespace NorvesLib::RHI::Vulkan
{
    class VulkanDevice;

    /**
     * @brief IImGuiRenderer の Vulkan 実装（imgui_impl_vulkan のラッパ）
     *
     * imgui の Vulkan バックエンド（imgui_impl_vulkan）を NorvesLib の RHI 抽象
     * IImGuiRenderer に適合させる。生 Vulkan / imgui 型はこの実装内（Core の
     * Private/RHI/Vulkan）に閉じ込め、抽象ヘッダ IImGuiRenderer.h からは漏らさない。
     *
     * 所有権 / 寿命: 本クラスは VulkanDevice が所有する（VulkanDevice の
     * メンバ TUniquePtr が保持）。VulkanDevice を借用ポインタで保持するが、
     * device がより長命であることが保証される（device が自身の Inner として
     * 連れ破棄するため）。Shutdown は VkDevice 破棄前に呼ばれる前提。
     *
     * ImGui コンテキスト（ImGui::CreateContext）は本クラスでは生成しない。
     * 呼び出し側（第2段 B の ImGuiModule）が事前にコンテキストを生成済みである
     * ことが前提で、本クラスは Vulkan バックエンド側の RHI リソースのみを扱う。
     *
     * スレッド: 各メソッドは RenderThread から呼ばれる前提。
     */
    class VulkanImGuiRenderer final : public IImGuiRenderer
    {
    public:
        /**
         * @brief コンストラクタ
         * @param device 借用する VulkanDevice（device がより長命であること）
         */
        explicit VulkanImGuiRenderer(VulkanDevice *device);

        /**
         * @brief デストラクタ（Shutdown を冪等に呼ぶ）
         */
        ~VulkanImGuiRenderer() override;

        VulkanImGuiRenderer(const VulkanImGuiRenderer &) = delete;
        VulkanImGuiRenderer &operator=(const VulkanImGuiRenderer &) = delete;
        VulkanImGuiRenderer(VulkanImGuiRenderer &&) = delete;
        VulkanImGuiRenderer &operator=(VulkanImGuiRenderer &&) = delete;

        // IImGuiRenderer 実装
        bool Initialize(IRenderPass *loadRenderPass, uint32_t minImageCount, uint32_t imageCount) override;
        void Shutdown() override;
        bool BuildFontAtlas() override;
        void RecordDrawData(ICommandList *commandList, const void *imguiDrawData) override;
        void NotifySwapChainRecreated(uint32_t minImageCount, uint32_t imageCount) override;

    private:
        /// imgui バックエンドの内部 VkResult エラーをログへ流す静的フック
        /// （InitInfo.CheckVkResultFn に渡す。サイレント失敗を防ぐ）。
        static void CheckVkResult(VkResult result);

        VulkanDevice *m_device = nullptr;        ///< 借用（device がより長命）
        bool m_bInitialized = false;             ///< Init 済みフラグ（Shutdown 冪等化）
    };

} // namespace NorvesLib::RHI::Vulkan

#endif // defined(NORVES_ENABLE_IMGUI)

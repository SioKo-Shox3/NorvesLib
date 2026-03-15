#pragma once

#include "RHI/IShaderCompiler.h"
#include "Container/Containers.h"

namespace NorvesLib::RHI::Vulkan
{
    // 明示的なusing宣言
    using ::NorvesLib::Core::Container::String;
    using ::NorvesLib::Core::Container::VariableArray;

    /**
     * @brief Slangシェーダーコンパイラ実装
     *
     * NVIDIA Slang Compilerを使用して .slang シェーダーソースを
     * SPIR-Vバイトコードにランタイムでコンパイルします。
     *
     * 主にCooperative Vector (Neural Shaders) 用のシェーダーを処理します。
     * Slang SDKが未導入の場合はスタブとして動作し、常にエラーを返します。
     *
     * @note NORVES_HAS_SLANG マクロが定義されている場合のみ実際のコンパイルが有効
     */
    class VulkanSlangCompiler : public IShaderCompiler
    {
    public:
        VulkanSlangCompiler();
        ~VulkanSlangCompiler() override;

        // コピー禁止
        VulkanSlangCompiler(const VulkanSlangCompiler &) = delete;
        VulkanSlangCompiler &operator=(const VulkanSlangCompiler &) = delete;

        /**
         * @brief Slangコンパイラが利用可能かどうか
         * @return NORVES_HAS_SLANG が有効で初期化に成功していればtrue
         */
        bool IsAvailable() const;

        // IShaderCompiler実装
        ShaderCompileResult CompileFromSource(
            const String &source,
            ShaderStage stage,
            const String &filename = "shader",
            const String &entryPoint = "main") override;

        ShaderCompileResult CompileFromFile(
            const String &filePath,
            ShaderStage stage,
            const String &entryPoint = "main") override;

    private:
        /** @brief Slang GlobalSession ハンドル（ヘッダーへの依存を隠蔽） */
        void *m_GlobalSession = nullptr;

        /** @brief 初期化フラグ */
        bool m_bInitialized = false;
    };

} // namespace NorvesLib::RHI::Vulkan

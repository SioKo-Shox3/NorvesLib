#pragma once

#include "RHI/IShaderCompiler.h"
#include "RHI/RHITypes.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Text/IdentityPool.h"
#include <cstdint>

using namespace NorvesLib::Core::Container;

namespace NorvesLib::RHI
{
    class IDevice;
    class IShader;
} // namespace NorvesLib::RHI

namespace NorvesLib::Core::Rendering
{

    /**
     * @brief シェーダーマネージャー
     *
     * シェーダーアセットの読み込み・コンパイル・キャッシュを管理します。
     * GLSLソースファイルをランタイムでSPIR-Vにコンパイルし、
     * RHIシェーダーオブジェクトを生成して返します。
     *
     * アセットパス:
     * - ベースディレクトリからの相対パスでシェーダーを指定
     * - 例: "gbuffer.vert", "lighting.frag"
     *
     * シェーダーキャッシュ:
     * - 同一のシェーダーファイルは初回コンパイル後キャッシュされます
     * - ReloadAll() で全シェーダーを強制再コンパイルできます
     *
     * 使用例:
     * ```cpp
     * ShaderManager shaderMgr;
     * shaderMgr.Initialize(device, "Assets/Shaders");
     *
     * auto vertShader = shaderMgr.LoadShader("gbuffer.vert", ShaderStage::Vertex);
     * auto fragShader = shaderMgr.LoadShader("gbuffer.frag", ShaderStage::Pixel);
     * ```
     */
    class ShaderManager
    {
    public:
        ShaderManager() = default;
        ~ShaderManager() = default;

        /**
         * @brief 初期化
         * @param device RHIデバイス
         * @param shaderDirectory シェーダーアセットのベースディレクトリ
         * @return 初期化成功時true
         */
        bool Initialize(RHI::IDevice *device, const String &shaderDirectory);

        /**
         * @brief 終了処理
         */
        void Shutdown();

        /**
         * @brief シェーダーファイルからRHIシェーダーを取得（キャッシュあり）
         * @param filename シェーダーファイル名（相対パス）
         * @param stage シェーダーステージ
         * @param entryPoint エントリーポイント名
         * @return RHIシェーダー（失敗時nullptr）
         */
        TSharedPtr<RHI::IShader> LoadShader(
            const String &filename,
            RHI::ShaderStage stage,
            const String &entryPoint = "main");

        /**
         * @brief 全キャッシュシェーダーを再コンパイル
         * @return 再コンパイル成功数
         */
        uint32_t ReloadAll();

        /**
         * @brief 特定シェーダーを再コンパイル
         * @param filename シェーダーファイル名
         * @return 成功時true
         */
        bool ReloadShader(const String &filename);

        /**
         * @brief シェーダーキャッシュをクリア
         */
        void ClearCache();

        /**
         * @brief シェーダーベースディレクトリを取得
         */
        const String &GetShaderDirectory() const { return m_ShaderDirectory; }

        /**
         * @brief シェーダーベースディレクトリを変更
         */
        void SetShaderDirectory(const String &directory) { m_ShaderDirectory = directory; }

    private:
        /** @brief キャッシュエントリ */
        struct CachedShader
        {
            TSharedPtr<RHI::IShader> Shader;
            RHI::ShaderStage Stage;
            String EntryPoint;
            String Filename;
        };

        /** @brief フルパスを構築 */
        String BuildFullPath(const String &filename) const;

        RHI::IDevice *m_Device = nullptr;
        String m_ShaderDirectory;
        RHI::ShaderCompilerPtr m_Compiler;
        UnorderedMap<Identity, CachedShader, Identity::Hasher> m_Cache;
        bool m_bInitialized = false;
    };

} // namespace NorvesLib::Core::Rendering

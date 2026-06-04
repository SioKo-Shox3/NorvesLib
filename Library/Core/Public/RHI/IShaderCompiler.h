#pragma once

#include "RHITypes.h"
#include "Container/Containers.h"
#include <cstdint>

namespace NorvesLib::RHI
{
    using NorvesLib::Core::Container::String;
    using NorvesLib::Core::Container::TSharedPtr;
    using NorvesLib::Core::Container::VariableArray;

    /**
     * @brief シェーダーコンパイル結果
     */
    struct ShaderCompileResult
    {
        /** @brief コンパイル成功フラグ */
        bool bSuccess = false;

        /** @brief コンパイル済みバイトコード（SPIR-V, DXIL等） */
        VariableArray<uint8_t> ByteCode;

        /** @brief エラー/警告メッセージ */
        String ErrorMessage;
    };

    /**
     * @brief シェーダーコンパイラインターフェース
     *
     * シェーダーソースコードをGPU用バイトコードにコンパイルする
     * RHI抽象インターフェースです。
     * 各レンダリングAPI（Vulkan, DirectX等）が固有の実装を提供します。
     *
     * 使用例:
     * ```cpp
     * auto compiler = device->CreateShaderCompiler();
     * auto result = compiler->CompileFromSource(glslSource, ShaderStage::Vertex, "shader.vert");
     * if (result.bSuccess)
     * {
     *     // result.ByteCode を使用してシェーダーを作成
     * }
     * ```
     */
    class IShaderCompiler
    {
    public:
        virtual ~IShaderCompiler() = default;

        /**
         * @brief ソースコードからシェーダーをコンパイル
         * @param source シェーダーソースコード
         * @param stage シェーダーステージ
         * @param filename デバッグ用ファイル名
         * @param entryPoint エントリーポイント名
         * @return コンパイル結果
         */
        virtual ShaderCompileResult CompileFromSource(
            const String &source,
            ShaderStage stage,
            const String &filename = "shader",
            const String &entryPoint = "main") = 0;

        /**
         * @brief ファイルからシェーダーを読み込みコンパイル
         * @param filePath シェーダーファイルパス
         * @param stage シェーダーステージ
         * @param entryPoint エントリーポイント名
         * @return コンパイル結果
         */
        virtual ShaderCompileResult CompileFromFile(
            const String &filePath,
            ShaderStage stage,
            const String &entryPoint = "main") = 0;
    };

    // スマートポインタエイリアス
    using ShaderCompilerPtr = TSharedPtr<IShaderCompiler>;

} // namespace NorvesLib::RHI

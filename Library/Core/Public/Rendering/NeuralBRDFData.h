#pragma once

#include "Container/Containers.h"
#include "Logging/LogMacros.h"
#include <cstdint>
#include <cstring>
#include <cmath>
#include <fstream>

namespace NorvesLib::Core::Rendering
{

    /**
     * @brief ニューラルBRDFのレイヤー情報
     *
     * 各レイヤーの入出力サイズとFP32配列内のオフセットを保持します。
     */
    struct NeuralBRDFLayer
    {
        uint32_t Inputs = 0;       ///< 入力ニューロン数
        uint32_t Outputs = 0;      ///< 出力ニューロン数
        uint32_t WeightOffset = 0; ///< FP32配列内の重みデータ開始インデックス
        uint32_t BiasOffset = 0;   ///< FP32配列内のバイアスデータ開始インデックス
    };

    /**
     * @brief ニューラルBRDFデータローダー
     *
     * NVIDIA-RTX/RTXNS の .ns.bin 形式のニューラルBRDF学習済みデータを読み込みます。
     * ファイルから FP16 の重み/バイアスデータを FP32 に変換して保持します。
     *
     * Disney BRDFモデル:
     * - 入力: NdotL, NdotV, NdotH, LdotH, roughness（5パラメータ）
     * - 周波数エンコーディング: 5 × 6 = 30 入力ニューロン
     * - 隠れ層: 32ニューロン × 3層（ReLU活性化）
     * - 出力: 4値（exp活性化）
     *
     * 使用フロー:
     * 1. LoadFromFile() で .ns.bin ファイルを読み込み
     * 2. GetWeightDataFP32() で FP32 重みデータをGPUバッファにアップロード
     * 3. GetLayers() で各レイヤーのオフセット情報をシェーダーに渡す
     */
    class NeuralBRDFData
    {
    public:
        NeuralBRDFData() = default;

        /**
         * @brief .ns.bin ファイルから学習済みデータを読み込み
         * @param filePath ファイルパス
         * @return 成功時 true
         */
        bool LoadFromFile(const Container::String &filePath)
        {
            std::ifstream file(filePath.c_str(), std::ios::binary | std::ios::ate);
            if (!file.is_open())
            {
                NORVES_LOG_ERROR("NeuralBRDFData", "Failed to open file: %s", filePath.c_str());
                return false;
            }

            size_t fileSize = static_cast<size_t>(file.tellg());
            file.seekg(0, std::ios::beg);

            if (fileSize < 32)
            {
                NORVES_LOG_ERROR("NeuralBRDFData", "File too small: %zu bytes", fileSize);
                return false;
            }

            // ========================================
            // ヘッダー読み込み
            // ========================================
            uint32_t magic = 0;
            file.read(reinterpret_cast<char *>(&magic), 4);
            if (magic != 0xA1C0DE01)
            {
                NORVES_LOG_ERROR("NeuralBRDFData", "Invalid magic number: 0x%08X", magic);
                return false;
            }

            uint32_t numHiddenLayers = 0;
            uint32_t inputNeurons = 0;
            uint32_t hiddenNeurons = 0;
            uint32_t outputNeurons = 0;
            uint32_t weightPrecision = 0;
            uint32_t biasPrecision = 0;

            file.read(reinterpret_cast<char *>(&numHiddenLayers), 4);
            file.read(reinterpret_cast<char *>(&inputNeurons), 4);
            file.read(reinterpret_cast<char *>(&hiddenNeurons), 4);
            file.read(reinterpret_cast<char *>(&outputNeurons), 4);
            file.read(reinterpret_cast<char *>(&weightPrecision), 4);
            file.read(reinterpret_cast<char *>(&biasPrecision), 4);

            m_NumInputFeatures = 5; // Disney BRDF: NdotL, NdotV, NdotH, LdotH, roughness
            m_NumFrequencies = inputNeurons / (m_NumInputFeatures * 2);
            m_InputNeurons = inputNeurons;
            m_HiddenNeurons = hiddenNeurons;
            m_OutputNeurons = outputNeurons;
            m_NumHiddenLayers = numHiddenLayers;
            m_NumTotalLayers = numHiddenLayers + 1;

            NORVES_LOG_INFO("NeuralBRDFData",
                            "Architecture: %u hidden layers, %u→%u→%u neurons, FP%s weights",
                            numHiddenLayers, inputNeurons, hiddenNeurons, outputNeurons,
                            (weightPrecision == 0) ? "16" : "32");

            // ========================================
            // レイヤーオフセット計算（RowMajor FP16 レイアウト）
            // ========================================
            m_Layers.resize(m_NumTotalLayers);
            size_t fp16ByteOffset = 0;

            for (uint32_t i = 0; i < m_NumTotalLayers; ++i)
            {
                uint32_t layerInputs = (i == 0) ? inputNeurons : hiddenNeurons;
                uint32_t layerOutputs = (i == numHiddenLayers) ? outputNeurons : hiddenNeurons;

                uint32_t weightSizeBytes = layerInputs * layerOutputs * 2; // FP16 = 2 bytes
                uint32_t biasSizeBytes = layerOutputs * 2;

                m_Layers[i].Inputs = layerInputs;
                m_Layers[i].Outputs = layerOutputs;
                m_Layers[i].WeightOffset = static_cast<uint32_t>(fp16ByteOffset / 2); // FP16 element index → FP32 index
                fp16ByteOffset += weightSizeBytes;
                m_Layers[i].BiasOffset = static_cast<uint32_t>(fp16ByteOffset / 2);
                fp16ByteOffset += biasSizeBytes;
            }

            size_t expectedDataSize = fp16ByteOffset;
            size_t fp16ElementCount = expectedDataSize / 2;

            // ========================================
            // 重みデータ読み込み（ファイル末尾から計算）
            // ========================================
            if (fileSize < expectedDataSize)
            {
                NORVES_LOG_ERROR("NeuralBRDFData", "File too small for weight data: need %zu, have %zu",
                                 expectedDataSize, fileSize);
                return false;
            }

            size_t dataOffset = fileSize - expectedDataSize;
            file.seekg(static_cast<std::streampos>(dataOffset), std::ios::beg);

            Container::VariableArray<uint16_t> fp16Data(fp16ElementCount);
            file.read(reinterpret_cast<char *>(fp16Data.data()), expectedDataSize);

            if (!file.good())
            {
                NORVES_LOG_ERROR("NeuralBRDFData", "Failed to read weight data");
                return false;
            }

            // ========================================
            // FP16 → FP32 変換
            // ========================================
            m_WeightsFP32.resize(fp16ElementCount);
            for (size_t i = 0; i < fp16ElementCount; ++i)
            {
                m_WeightsFP32[i] = HalfToFloat(fp16Data[i]);
            }

            m_bLoaded = true;

            NORVES_LOG_INFO("NeuralBRDFData",
                            "Loaded: %zu parameters (%zu bytes FP32), %u layers",
                            fp16ElementCount, m_WeightsFP32.size() * sizeof(float),
                            m_NumTotalLayers);

            return true;
        }

        // ========================================
        // アクセサ
        // ========================================

        bool IsLoaded() const { return m_bLoaded; }

        /** @brief FP32重みデータ（GPU StorageBufferアップロード用） */
        const Container::VariableArray<float> &GetWeightDataFP32() const { return m_WeightsFP32; }

        /** @brief FP32重みデータのバイトサイズ */
        size_t GetWeightDataSizeFP32() const { return m_WeightsFP32.size() * sizeof(float); }

        /** @brief レイヤー情報 */
        const Container::VariableArray<NeuralBRDFLayer> &GetLayers() const { return m_Layers; }

        /** @brief 総レイヤー数（隠れ層 + 出力層） */
        uint32_t GetTotalLayerCount() const { return m_NumTotalLayers; }

        uint32_t GetInputNeurons() const { return m_InputNeurons; }
        uint32_t GetHiddenNeurons() const { return m_HiddenNeurons; }
        uint32_t GetOutputNeurons() const { return m_OutputNeurons; }
        uint32_t GetNumInputFeatures() const { return m_NumInputFeatures; }
        uint32_t GetNumFrequencies() const { return m_NumFrequencies; }

    private:
        /**
         * @brief FP16 → FP32 変換
         */
        static float HalfToFloat(uint16_t h)
        {
            uint32_t sign = (h >> 15) & 0x1;
            uint32_t exponent = (h >> 10) & 0x1F;
            uint32_t mantissa = h & 0x3FF;

            if (exponent == 0)
            {
                if (mantissa == 0)
                {
                    // ±0
                    uint32_t result = sign << 31;
                    float f;
                    std::memcpy(&f, &result, sizeof(float));
                    return f;
                }
                // Denormalized
                while (!(mantissa & 0x400))
                {
                    mantissa <<= 1;
                    exponent--;
                }
                exponent++;
                mantissa &= ~0x400;
            }
            else if (exponent == 31)
            {
                // Inf/NaN
                uint32_t result = (sign << 31) | 0x7F800000 | (mantissa << 13);
                float f;
                std::memcpy(&f, &result, sizeof(float));
                return f;
            }

            exponent = exponent + (127 - 15);
            mantissa = mantissa << 13;

            uint32_t result = (sign << 31) | (exponent << 23) | mantissa;
            float f;
            std::memcpy(&f, &result, sizeof(float));
            return f;
        }

        Container::VariableArray<float> m_WeightsFP32;
        Container::VariableArray<NeuralBRDFLayer> m_Layers;

        uint32_t m_NumInputFeatures = 0;
        uint32_t m_NumFrequencies = 0;
        uint32_t m_InputNeurons = 0;
        uint32_t m_HiddenNeurons = 0;
        uint32_t m_OutputNeurons = 0;
        uint32_t m_NumHiddenLayers = 0;
        uint32_t m_NumTotalLayers = 0;
        bool m_bLoaded = false;
    };

} // namespace NorvesLib::Core::Rendering

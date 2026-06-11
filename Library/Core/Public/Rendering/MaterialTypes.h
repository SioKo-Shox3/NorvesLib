#pragma once

#include "RenderTypes.h"
#include "Container/Containers.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{

    // ========================================
    // シェーダー関連型
    // ========================================

    /**
     * @brief シェーダーステージ
     */
    enum class ShaderStage : uint8_t
    {
        Vertex,
        Pixel,
        Geometry,
        Hull,
        Domain,
        Compute,
        Count
    };

    /**
     * @brief シェーダーパラメータタイプ
     */
    enum class ShaderParameterType : uint8_t
    {
        Float,
        Float2,
        Float3,
        Float4,
        Int,
        Int2,
        Int3,
        Int4,
        Matrix3x3,
        Matrix4x4,
        Texture2D,
        TextureCube,
        Sampler,
        ConstantBuffer,
        StructuredBuffer
    };

    // ========================================
    // テクスチャ関連型
    // ========================================

    /**
     * @brief テクスチャタイプ
     */
    enum class TextureType : uint8_t
    {
        Texture2D,
        Texture3D,
        TextureCube,
        Texture2DArray
    };

    /**
     * @brief テクスチャ使用目的
     */
    enum class TextureUsage : uint8_t
    {
        Diffuse,   // ディフューズ/アルベド
        Normal,    // 法線マップ
        Metallic,  // メタリック
        Roughness, // ラフネス
        AO,        // アンビエントオクルージョン
        Emissive,  // エミッシブ
        Height,    // ハイトマップ
        Opacity,   // 不透明度
        Custom     // カスタム用途
    };

    /**
     * @brief テクスチャスロット定義
     */
    struct TextureSlot
    {
        Container::String Name;
        TextureUsage Usage = TextureUsage::Custom;
        TextureHandle Texture;
        SamplerHandle Sampler;

        TextureSlot() = default;

        TextureSlot(const Container::String &name, TextureUsage usage)
            : Name(name), Usage(usage)
        {
        }
    };

    // ========================================
    // マテリアルパラメータ
    // ========================================

    /**
     * @brief マテリアルパラメータ値
     *
     * 型安全なパラメータ値を表現するためのユニオン構造体
     */
    struct MaterialParameterValue
    {
        ShaderParameterType Type = ShaderParameterType::Float;

        union
        {
            float FloatValue;
            float Float2Value[2];
            float Float3Value[3];
            float Float4Value[4];
            int32_t IntValue;
            int32_t Int2Value[2];
            int32_t Int3Value[3];
            int32_t Int4Value[4];
            float Matrix3x3Value[9];
            float Matrix4x4Value[16];
        };

        MaterialParameterValue() : FloatValue(0.0f) {}

        static MaterialParameterValue CreateFloat(float value)
        {
            MaterialParameterValue param;
            param.Type = ShaderParameterType::Float;
            param.FloatValue = value;
            return param;
        }

        static MaterialParameterValue CreateFloat2(float x, float y)
        {
            MaterialParameterValue param;
            param.Type = ShaderParameterType::Float2;
            param.Float2Value[0] = x;
            param.Float2Value[1] = y;
            return param;
        }

        static MaterialParameterValue CreateFloat3(float x, float y, float z)
        {
            MaterialParameterValue param;
            param.Type = ShaderParameterType::Float3;
            param.Float3Value[0] = x;
            param.Float3Value[1] = y;
            param.Float3Value[2] = z;
            return param;
        }

        static MaterialParameterValue CreateFloat4(float x, float y, float z, float w)
        {
            MaterialParameterValue param;
            param.Type = ShaderParameterType::Float4;
            param.Float4Value[0] = x;
            param.Float4Value[1] = y;
            param.Float4Value[2] = z;
            param.Float4Value[3] = w;
            return param;
        }

        static MaterialParameterValue CreateInt(int32_t value)
        {
            MaterialParameterValue param;
            param.Type = ShaderParameterType::Int;
            param.IntValue = value;
            return param;
        }
    };

    /**
     * @brief マテリアルパラメータ定義
     */
    struct MaterialParameter
    {
        Container::String Name;
        MaterialParameterValue Value;

        MaterialParameter() = default;

        MaterialParameter(const Container::String &name, const MaterialParameterValue &value)
            : Name(name), Value(value)
        {
        }
    };

    // ========================================
    // ブレンドモード
    // ========================================

    /**
     * @brief ブレンドモード
     */
    enum class BlendMode : uint8_t
    {
        Opaque,      // 不透明
        Masked,      // マスク（アルファテスト）
        Translucent, // 半透明
        Additive,    // 加算合成
        Modulate     // 乗算合成
    };

    /**
     * @brief シェーディングモデル
     */
    enum class ShadingModel : uint8_t
    {
        Unlit,      // ライティングなし
        DefaultLit, // 標準ライティング
        Subsurface, // サブサーフェイススキャッタリング
        ClearCoat,  // クリアコート
        Cloth,      // 布
        Hair,       // 髪
        Eye,        // 目
        Custom      // カスタム
    };

    /**
     * @brief マテリアル作成情報
     */
    struct MaterialCreateData
    {
        TextureHandle AlbedoTexture;
        TextureHandle NormalTexture;
        TextureHandle MetallicTexture;
        TextureHandle RoughnessTexture;
        TextureHandle AOTexture;
        TextureHandle HeightTexture; ///< ディスプレイスメントマップ（POM用）

        float HeightScale = 0.05f; ///< POMの高さスケール（0.0～0.1程度が自然）

        float EmissiveColor[3] = {0.0f, 0.0f, 0.0f};
        float EmissiveStrength = 0.0f;

        BlendMode Blend = BlendMode::Opaque;
        ShadingModel Shading = ShadingModel::DefaultLit;
        bool bTwoSided = false;
        bool bCastShadows = true;

        Container::String DebugName;
    };

    /**
     * @brief マテリアルリソースデータ（内部用）
     */
    struct MaterialResourceData
    {
        TextureHandle AlbedoTexture;
        TextureHandle NormalTexture;
        TextureHandle MetallicTexture;
        TextureHandle RoughnessTexture;
        TextureHandle AOTexture;
        TextureHandle HeightTexture; ///< ディスプレイスメントマップ（POM用）

        float HeightScale = 0.05f; ///< POMの高さスケール

        float EmissiveColor[3] = {0.0f, 0.0f, 0.0f};
        float EmissiveStrength = 0.0f;

        BlendMode Blend = BlendMode::Opaque;
        ShadingModel Shading = ShadingModel::DefaultLit;
        bool bTwoSided = false;
        bool bCastShadows = true;

        uint32_t RefCount = 0;
        Container::String DebugName;
    };

    // ========================================
    // マテリアル定義
    // ========================================

    /**
     * @brief マテリアル作成情報
     *
     * MaterialResources経由でマテリアルを作成するための情報。
     */
    struct MaterialCreateInfo
    {
        // シェーダー
        ShaderHandle VertexShader;
        ShaderHandle PixelShader;

        // マテリアル設定
        BlendMode Blend = BlendMode::Opaque;
        ShadingModel Shading = ShadingModel::DefaultLit;

        // テクスチャ
        Container::VariableArray<TextureSlot> TextureSlots;

        // パラメータ
        Container::VariableArray<MaterialParameter> Parameters;

        // レンダリング設定
        bool bTwoSided = false;      // 両面描画
        bool bCastShadows = true;    // 影を落とす
        bool bReceiveShadows = true; // 影を受ける
        bool bWireframe = false;     // ワイヤーフレーム表示
        float AlphaCutoff = 0.5f;    // アルファカットオフ（Masked用）

        // デバッグ
        Container::String DebugName;
    };

    /**
     * @brief マテリアルGPUデータ（内部用）
     *
     * Renderingシステムが管理する内部構造体。
     */
    struct MaterialGPUData
    {
        PipelineHandle Pipeline;

        ShaderHandle VertexShader;
        ShaderHandle PixelShader;

        BlendMode Blend = BlendMode::Opaque;
        ShadingModel Shading = ShadingModel::DefaultLit;

        Container::VariableArray<TextureSlot> TextureSlots;
        Container::VariableArray<MaterialParameter> Parameters;

        bool bTwoSided = false;
        bool bCastShadows = true;
        bool bReceiveShadows = true;
        float AlphaCutoff = 0.5f;

        uint32_t RefCount = 0;
        Container::String DebugName;
    };

    /**
     * @brief マテリアル記述子（Game側向け）
     *
     * Game側がマテリアルの情報を参照するための読み取り専用構造体。
     */
    struct MaterialDescriptor
    {
        MaterialHandle Handle;

        BlendMode Blend = BlendMode::Opaque;
        ShadingModel Shading = ShadingModel::DefaultLit;

        uint32_t TextureSlotCount = 0;
        uint32_t ParameterCount = 0;

        bool bTwoSided = false;
        bool bCastShadows = true;
        bool bReceiveShadows = true;

        Container::String Name;

        bool IsValid() const { return Handle.IsValid(); }
    };

    // ========================================
    // マテリアルインスタンス
    // ========================================

    /**
     * @brief マテリアルインスタンスパラメータオーバーライド
     *
     * ベースマテリアルのパラメータを個別にオーバーライドするための構造体
     */
    struct MaterialInstanceOverride
    {
        MaterialHandle BaseMaterial;

        // オーバーライドするテクスチャ
        Container::VariableArray<TextureSlot> TextureOverrides;

        // オーバーライドするパラメータ
        Container::VariableArray<MaterialParameter> ParameterOverrides;

        /**
         * @brief テクスチャをオーバーライド
         */
        void SetTexture(const Container::String &slotName, TextureHandle texture)
        {
            for (auto &slot : TextureOverrides)
            {
                if (slot.Name == slotName)
                {
                    slot.Texture = texture;
                    return;
                }
            }
            TextureSlot newSlot;
            newSlot.Name = slotName;
            newSlot.Texture = texture;
            TextureOverrides.push_back(newSlot);
        }

        /**
         * @brief Floatパラメータをオーバーライド
         */
        void SetFloat(const Container::String &name, float value)
        {
            for (auto &param : ParameterOverrides)
            {
                if (param.Name == name)
                {
                    param.Value = MaterialParameterValue::CreateFloat(value);
                    return;
                }
            }
            ParameterOverrides.push_back(MaterialParameter(name, MaterialParameterValue::CreateFloat(value)));
        }

        /**
         * @brief Float4パラメータ（色など）をオーバーライド
         */
        void SetColor(const Container::String &name, float r, float g, float b, float a = 1.0f)
        {
            for (auto &param : ParameterOverrides)
            {
                if (param.Name == name)
                {
                    param.Value = MaterialParameterValue::CreateFloat4(r, g, b, a);
                    return;
                }
            }
            ParameterOverrides.push_back(MaterialParameter(name, MaterialParameterValue::CreateFloat4(r, g, b, a)));
        }
    };

} // namespace NorvesLib::Core::Rendering

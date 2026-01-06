#pragma once

#include "RenderTypes.h"
#include "Container/Containers.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{

    // ========================================
    // 頂点属性定義
    // ========================================

    /**
     * @brief 頂点属性のセマンティクス
     */
    enum class VertexSemantic : uint8_t
    {
        Position,    // 位置座標
        Normal,      // 法線ベクトル
        Tangent,     // 接線ベクトル
        Binormal,    // 従法線ベクトル
        Color0,      // 頂点カラー0
        Color1,      // 頂点カラー1
        TexCoord0,   // テクスチャ座標0
        TexCoord1,   // テクスチャ座標1
        TexCoord2,   // テクスチャ座標2
        TexCoord3,   // テクスチャ座標3
        BoneIndices, // ボーンインデックス
        BoneWeights, // ボーンウェイト
        Custom0,     // カスタム属性0
        Custom1,     // カスタム属性1
        Custom2,     // カスタム属性2
        Custom3,     // カスタム属性3
        Count
    };

    /**
     * @brief 頂点属性のデータフォーマット
     */
    enum class VertexFormat : uint8_t
    {
        Float1,  // float x 1 (4 bytes)
        Float2,  // float x 2 (8 bytes)
        Float3,  // float x 3 (12 bytes)
        Float4,  // float x 4 (16 bytes)
        Half2,   // half x 2 (4 bytes)
        Half4,   // half x 4 (8 bytes)
        UByte4,  // uint8_t x 4 (4 bytes)
        UByte4N, // uint8_t x 4 normalized (4 bytes)
        Short2,  // int16_t x 2 (4 bytes)
        Short2N, // int16_t x 2 normalized (4 bytes)
        Short4,  // int16_t x 4 (8 bytes)
        Short4N, // int16_t x 4 normalized (8 bytes)
        UInt1,   // uint32_t x 1 (4 bytes)
        UInt2,   // uint32_t x 2 (8 bytes)
        UInt4,   // uint32_t x 4 (16 bytes)
        Int1,    // int32_t x 1 (4 bytes)
        Int2,    // int32_t x 2 (8 bytes)
        Int4,    // int32_t x 4 (16 bytes)
    };

    /**
     * @brief 頂点フォーマットのバイトサイズを取得
     * @param format 頂点フォーマット
     * @return バイトサイズ
     */
    constexpr uint32_t GetVertexFormatSize(VertexFormat format)
    {
        switch (format)
        {
        case VertexFormat::Float1:
            return 4;
        case VertexFormat::Float2:
            return 8;
        case VertexFormat::Float3:
            return 12;
        case VertexFormat::Float4:
            return 16;
        case VertexFormat::Half2:
            return 4;
        case VertexFormat::Half4:
            return 8;
        case VertexFormat::UByte4:
        case VertexFormat::UByte4N:
            return 4;
        case VertexFormat::Short2:
        case VertexFormat::Short2N:
            return 4;
        case VertexFormat::Short4:
        case VertexFormat::Short4N:
            return 8;
        case VertexFormat::UInt1:
        case VertexFormat::Int1:
            return 4;
        case VertexFormat::UInt2:
        case VertexFormat::Int2:
            return 8;
        case VertexFormat::UInt4:
        case VertexFormat::Int4:
            return 16;
        default:
            return 0;
        }
    }

    /**
     * @brief 頂点フォーマットのコンポーネント数を取得
     * @param format 頂点フォーマット
     * @return コンポーネント数
     */
    constexpr uint32_t GetVertexFormatComponentCount(VertexFormat format)
    {
        switch (format)
        {
        case VertexFormat::Float1:
        case VertexFormat::UInt1:
        case VertexFormat::Int1:
            return 1;
        case VertexFormat::Float2:
        case VertexFormat::Half2:
        case VertexFormat::Short2:
        case VertexFormat::Short2N:
        case VertexFormat::UInt2:
        case VertexFormat::Int2:
            return 2;
        case VertexFormat::Float3:
            return 3;
        case VertexFormat::Float4:
        case VertexFormat::Half4:
        case VertexFormat::UByte4:
        case VertexFormat::UByte4N:
        case VertexFormat::Short4:
        case VertexFormat::Short4N:
        case VertexFormat::UInt4:
        case VertexFormat::Int4:
            return 4;
        default:
            return 0;
        }
    }

    // ========================================
    // 頂点属性要素
    // ========================================

    /**
     * @brief 頂点属性の定義
     */
    struct VertexElement
    {
        VertexSemantic Semantic = VertexSemantic::Position;
        VertexFormat Format = VertexFormat::Float3;
        uint32_t Offset = 0;        // バッファ内オフセット（バイト）
        uint32_t BufferSlot = 0;    // 頂点バッファスロット（複数バッファ対応）
        uint32_t SemanticIndex = 0; // 同一セマンティクスの複数インスタンス用

        constexpr VertexElement() = default;

        constexpr VertexElement(VertexSemantic semantic, VertexFormat format,
                                uint32_t offset = 0, uint32_t bufferSlot = 0,
                                uint32_t semanticIndex = 0)
            : Semantic(semantic), Format(format), Offset(offset),
              BufferSlot(bufferSlot), SemanticIndex(semanticIndex)
        {
        }

        uint32_t GetSize() const
        {
            return GetVertexFormatSize(Format);
        }
    };

    // ========================================
    // 頂点レイアウト
    // ========================================

    /**
     * @brief 最大頂点属性数
     */
    constexpr uint32_t MAX_VERTEX_ELEMENTS = 16;

    /**
     * @brief 頂点レイアウト定義
     *
     * 頂点バッファのメモリレイアウトを定義します。
     * Game側からはこのレイアウト情報のみを扱い、
     * 実際のデータ操作はRenderingシステム内部で行われます。
     */
    struct VertexLayout
    {
        Container::FixedArray<VertexElement, MAX_VERTEX_ELEMENTS> Elements;
        uint32_t ElementCount = 0;
        uint32_t Stride = 0; // 1頂点のバイトサイズ

        /**
         * @brief デフォルトコンストラクタ
         */
        VertexLayout() = default;

        /**
         * @brief 要素リストからストライドを計算
         */
        void CalculateStride()
        {
            Stride = 0;
            for (uint32_t i = 0; i < ElementCount; ++i)
            {
                uint32_t elementEnd = Elements[i].Offset + Elements[i].GetSize();
                if (elementEnd > Stride)
                {
                    Stride = elementEnd;
                }
            }
        }

        /**
         * @brief 頂点要素を追加
         * @param element 追加する要素
         * @return 追加に成功した場合true
         */
        bool AddElement(const VertexElement &element)
        {
            if (ElementCount >= MAX_VERTEX_ELEMENTS)
            {
                return false;
            }
            Elements[ElementCount++] = element;
            CalculateStride();
            return true;
        }

        /**
         * @brief 指定されたセマンティクスの要素を取得
         * @param semantic 検索するセマンティクス
         * @param semanticIndex セマンティクスインデックス
         * @return 要素へのポインタ（見つからない場合はnullptr）
         */
        const VertexElement *FindElement(VertexSemantic semantic, uint32_t semanticIndex = 0) const
        {
            for (uint32_t i = 0; i < ElementCount; ++i)
            {
                if (Elements[i].Semantic == semantic &&
                    Elements[i].SemanticIndex == semanticIndex)
                {
                    return &Elements[i];
                }
            }
            return nullptr;
        }

        /**
         * @brief 指定されたセマンティクスを持つかどうか
         */
        bool HasSemantic(VertexSemantic semantic) const
        {
            return FindElement(semantic) != nullptr;
        }

        // ========================================
        // プリセットレイアウト
        // ========================================

        /**
         * @brief 位置のみのレイアウト
         */
        static VertexLayout CreatePositionOnly()
        {
            VertexLayout layout;
            layout.Elements[0] = VertexElement(VertexSemantic::Position, VertexFormat::Float3, 0);
            layout.ElementCount = 1;
            layout.CalculateStride();
            return layout;
        }

        /**
         * @brief 標準レイアウト（Position + Normal + UV）
         */
        static VertexLayout CreateStandard()
        {
            VertexLayout layout;
            uint32_t offset = 0;

            layout.Elements[0] = VertexElement(VertexSemantic::Position, VertexFormat::Float3, offset);
            offset += 12;

            layout.Elements[1] = VertexElement(VertexSemantic::Normal, VertexFormat::Float3, offset);
            offset += 12;

            layout.Elements[2] = VertexElement(VertexSemantic::TexCoord0, VertexFormat::Float2, offset);
            offset += 8;

            layout.ElementCount = 3;
            layout.Stride = offset;
            return layout;
        }

        /**
         * @brief 拡張レイアウト（Position + Normal + Tangent + UV）
         */
        static VertexLayout CreateExtended()
        {
            VertexLayout layout;
            uint32_t offset = 0;

            layout.Elements[0] = VertexElement(VertexSemantic::Position, VertexFormat::Float3, offset);
            offset += 12;

            layout.Elements[1] = VertexElement(VertexSemantic::Normal, VertexFormat::Float3, offset);
            offset += 12;

            layout.Elements[2] = VertexElement(VertexSemantic::Tangent, VertexFormat::Float4, offset);
            offset += 16;

            layout.Elements[3] = VertexElement(VertexSemantic::TexCoord0, VertexFormat::Float2, offset);
            offset += 8;

            layout.ElementCount = 4;
            layout.Stride = offset;
            return layout;
        }

        /**
         * @brief スキンメッシュ用レイアウト（Position + Normal + UV + BoneIndices + BoneWeights）
         */
        static VertexLayout CreateSkinned()
        {
            VertexLayout layout;
            uint32_t offset = 0;

            layout.Elements[0] = VertexElement(VertexSemantic::Position, VertexFormat::Float3, offset);
            offset += 12;

            layout.Elements[1] = VertexElement(VertexSemantic::Normal, VertexFormat::Float3, offset);
            offset += 12;

            layout.Elements[2] = VertexElement(VertexSemantic::TexCoord0, VertexFormat::Float2, offset);
            offset += 8;

            layout.Elements[3] = VertexElement(VertexSemantic::BoneIndices, VertexFormat::UInt4, offset);
            offset += 16;

            layout.Elements[4] = VertexElement(VertexSemantic::BoneWeights, VertexFormat::Float4, offset);
            offset += 16;

            layout.ElementCount = 5;
            layout.Stride = offset;
            return layout;
        }

        /**
         * @brief 頂点カラー付きレイアウト（Position + Normal + UV + Color）
         */
        static VertexLayout CreateWithColor()
        {
            VertexLayout layout;
            uint32_t offset = 0;

            layout.Elements[0] = VertexElement(VertexSemantic::Position, VertexFormat::Float3, offset);
            offset += 12;

            layout.Elements[1] = VertexElement(VertexSemantic::Normal, VertexFormat::Float3, offset);
            offset += 12;

            layout.Elements[2] = VertexElement(VertexSemantic::TexCoord0, VertexFormat::Float2, offset);
            offset += 8;

            layout.Elements[3] = VertexElement(VertexSemantic::Color0, VertexFormat::UByte4N, offset);
            offset += 4;

            layout.ElementCount = 4;
            layout.Stride = offset;
            return layout;
        }
    };

    // ========================================
    // 頂点レイアウトハンドル
    // ========================================

    /**
     * @brief 頂点レイアウトへのハンドル
     *
     * 登録済みのレイアウトを参照するための軽量なハンドル
     */
    struct VertexLayoutHandleTag
    {
    };
    using VertexLayoutHandle = ResourceHandle<VertexLayoutHandleTag>;

} // namespace NorvesLib::Core::Rendering

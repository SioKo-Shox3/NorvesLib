#pragma once

#include "Object/Resource.h"
#include "Object/Reflection.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/MaterialTypes.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"

namespace NorvesLib::Core::Rendering
{
    class NeuralMaterialResource;
}

namespace NorvesLib::Core
{

    /**
     * @brief NeuralMaterialの出力スロットとPBRスロットのマッピング
     *
     * NeuralMaterialResourceの出力スロットインデックスを
     * MaterialResourceのPBRテクスチャスロットに対応付けます。
     * 無効な値（UINT32_MAX）はそのスロットにニューラル出力を適用しないことを意味します。
     */
    struct NeuralMaterialSlotMapping
    {
        uint32_t AlbedoSlotIndex = UINT32_MAX;    ///< Albedoに対応するNeuralMaterialの出力スロット
        uint32_t NormalSlotIndex = UINT32_MAX;    ///< Normalに対応するNeuralMaterialの出力スロット
        uint32_t MetallicSlotIndex = UINT32_MAX;  ///< Metallicに対応するNeuralMaterialの出力スロット
        uint32_t RoughnessSlotIndex = UINT32_MAX; ///< Roughnessに対応するNeuralMaterialの出力スロット
        uint32_t AOSlotIndex = UINT32_MAX;        ///< AOに対応するNeuralMaterialの出力スロット

        /**
         * @brief デフォルトPBRマッピングを生成
         *
         * NeuralMaterialDesc::DefaultPBR()に対応:
         *   スロット0 → Albedo
         *   スロット1 → Normal
         *   スロット2 → ARM（AO/Roughness/Metallicを1つのスロットで提供）
         */
        static NeuralMaterialSlotMapping DefaultPBR()
        {
            NeuralMaterialSlotMapping mapping;
            mapping.AlbedoSlotIndex = 0;
            mapping.NormalSlotIndex = 1;
            // ARMスロット（スロット2）は AO/Roughness/Metallic を統合
            // 個別チャンネルの分離はシェーダー側で行う
            mapping.AOSlotIndex = 2;
            mapping.RoughnessSlotIndex = 2;
            mapping.MetallicSlotIndex = 2;
            return mapping;
        }
    };

    /**
     * @brief マテリアルリソース
     *
     * PBRマテリアルのテクスチャとパラメータを保持するリソースクラス。
     * Resourceを継承し、GEngine（ResourceRegistry経由）で管理されます。
     *
     * 責任者: GEngine（ResourceRegistry経由）
     * 寿命管理: 参照カウント方式
     *
     * 使用例:
     * ```cpp
     * auto &resourceManager = GEngine->GetRenderWorld().GetResourceManager();
     *
     * // テクスチャを個別にロード
     * auto albedoTex = resourceManager.LoadTexture("Assets/Textures/Silver/silver_albedo.png");
     * auto normalTex = resourceManager.LoadTexture("Assets/Textures/Silver/silver_normal-ogl.png");
     *
     * // マテリアルを作成
     * MaterialResourceCreateInfo info;
     * info.AlbedoTexture = albedoTex;
     * info.NormalTexture = normalTex;
     * info.DebugName = "Silver";
     * auto matHandle = resourceManager.CreateMaterial(info);
     *
     * // メッシュにマテリアルを設定
     * meshComponent->SetMaterial(0, matHandle);
     * ```
     */
    class MaterialResource : public Resource
    {
        REFLECTION_CLASS(MaterialResource, Resource)

    public:
        /**
         * @brief デフォルトコンストラクタ
         */
        MaterialResource();

        /**
         * @brief 初期化子を使用したコンストラクタ
         * @param initializer フィールド初期化子
         */
        explicit MaterialResource(const FieldInitializer *initializer);

        /**
         * @brief 任意のIUnknownオブジェクトからコピーするコンストラクタ
         * @param sourceObject コピー元となるオブジェクト
         */
        explicit MaterialResource(const IUnknown *sourceObject);

        /**
         * @brief デストラクタ
         */
        virtual ~MaterialResource();

        /**
         * @brief リソースを初期化します
         */
        void Initialize() override;

        /**
         * @brief リソースの破棄前処理を行います
         */
        void Finalize() override;

        // ========================================
        // Resource実装
        // ========================================

        bool Load() override;
        void Unload() override;
        size_t GetMemorySize() const override;

        // ========================================
        // PBRテクスチャスロット
        // ========================================

        /** @brief アルベドテクスチャを設定 */
        void SetAlbedoTexture(Rendering::TextureHandle handle) { m_AlbedoTexture = handle; }
        Rendering::TextureHandle GetAlbedoTexture() const { return m_AlbedoTexture; }

        /** @brief ノーマルマップを設定 */
        void SetNormalTexture(Rendering::TextureHandle handle) { m_NormalTexture = handle; }
        Rendering::TextureHandle GetNormalTexture() const { return m_NormalTexture; }

        /** @brief メタリックマップを設定 */
        void SetMetallicTexture(Rendering::TextureHandle handle) { m_MetallicTexture = handle; }
        Rendering::TextureHandle GetMetallicTexture() const { return m_MetallicTexture; }

        /** @brief ラフネスマップを設定 */
        void SetRoughnessTexture(Rendering::TextureHandle handle) { m_RoughnessTexture = handle; }
        Rendering::TextureHandle GetRoughnessTexture() const { return m_RoughnessTexture; }

        /** @brief AOマップを設定 */
        void SetAOTexture(Rendering::TextureHandle handle) { m_AOTexture = handle; }
        Rendering::TextureHandle GetAOTexture() const { return m_AOTexture; }

        // ========================================
        // エミッシブ設定
        // ========================================

        /** @brief エミッシブカラーを設定 */
        void SetEmissiveColor(float r, float g, float b);
        void GetEmissiveColor(float &r, float &g, float &b) const;

        /** @brief エミッシブ強度を設定 */
        void SetEmissiveStrength(float strength) { m_EmissiveStrength = strength; }
        float GetEmissiveStrength() const { return m_EmissiveStrength; }

        // ========================================
        // マテリアルプロパティ
        // ========================================

        /** @brief ブレンドモードを設定 */
        void SetBlendMode(Rendering::BlendMode mode) { m_BlendMode = mode; }
        Rendering::BlendMode GetBlendMode() const { return m_BlendMode; }

        /** @brief シェーディングモデルを設定 */
        void SetShadingModel(Rendering::ShadingModel model) { m_ShadingModel = model; }
        Rendering::ShadingModel GetShadingModel() const { return m_ShadingModel; }

        /** @brief 両面描画を設定 */
        void SetTwoSided(bool bTwoSided) { m_bTwoSided = bTwoSided; }
        bool IsTwoSided() const { return m_bTwoSided; }

        /** @brief シャドウキャストを設定 */
        void SetCastShadows(bool bCast) { m_bCastShadows = bCast; }
        bool GetCastShadows() const { return m_bCastShadows; }

        // ========================================
        // NeuralMaterial統合
        // ========================================

        /**
         * @brief NeuralMaterialResourceを設定
         *
         * NeuralMaterialが設定された場合、ResolveXxxTexture()メソッドは
         * スロットマッピングに従ってニューラルデコード結果のTextureHandleを返します。
         * NeuralMaterialが未設定の場合は通常のPBRテクスチャハンドルが使用されます。
         *
         * @param neuralMaterial NeuralMaterialResourceへのポインタ（nullptrで解除）
         * @param mapping 出力スロット→PBRスロットのマッピング
         */
        void SetNeuralMaterial(Rendering::NeuralMaterialResource* neuralMaterial,
                               const NeuralMaterialSlotMapping& mapping = NeuralMaterialSlotMapping::DefaultPBR());

        /**
         * @brief NeuralMaterialが設定されているか
         */
        bool HasNeuralMaterial() const { return m_NeuralMaterial != nullptr; }

        /**
         * @brief 解決済みAlbedoテクスチャを取得
         *
         * NeuralMaterialが有効でスロットがマッピングされている場合はニューラル出力、
         * そうでなければ通常のPBRテクスチャハンドルを返します。
         */
        Rendering::TextureHandle ResolveAlbedoTexture() const;

        /**
         * @brief 解決済みNormalテクスチャを取得
         */
        Rendering::TextureHandle ResolveNormalTexture() const;

        /**
         * @brief 解決済みMetallicテクスチャを取得
         */
        Rendering::TextureHandle ResolveMetallicTexture() const;

        /**
         * @brief 解決済みRoughnessテクスチャを取得
         */
        Rendering::TextureHandle ResolveRoughnessTexture() const;

        /**
         * @brief 解決済みAOテクスチャを取得
         */
        Rendering::TextureHandle ResolveAOTexture() const;

    private:
        // PBRテクスチャスロット
        Rendering::TextureHandle m_AlbedoTexture;    ///< アルベドテクスチャ
        Rendering::TextureHandle m_NormalTexture;    ///< ノーマルマップ
        Rendering::TextureHandle m_MetallicTexture;  ///< メタリックマップ
        Rendering::TextureHandle m_RoughnessTexture; ///< ラフネスマップ
        Rendering::TextureHandle m_AOTexture;        ///< AOマップ

        // エミッシブ
        float m_EmissiveColor[3] = {0.0f, 0.0f, 0.0f}; ///< エミッシブカラー
        float m_EmissiveStrength = 0.0f;               ///< エミッシブ強度

        // マテリアルプロパティ
        Rendering::BlendMode m_BlendMode = Rendering::BlendMode::Opaque;
        Rendering::ShadingModel m_ShadingModel = Rendering::ShadingModel::DefaultLit;
        bool m_bTwoSided = false;
        bool m_bCastShadows = true;

        // NeuralMaterial統合
        Rendering::NeuralMaterialResource* m_NeuralMaterial = nullptr; ///< NeuralMaterialリソース（非所有）
        NeuralMaterialSlotMapping m_NeuralSlotMapping;                 ///< スロットマッピング

        /**
         * @brief NeuralMaterialの指定スロットからTextureHandleを取得するヘルパー
         * @param slotIndex NeuralMaterialの出力スロットインデックス
         * @param fallback NeuralMaterial未使用時のフォールバックハンドル
         * @return 解決済みTextureHandle
         */
        Rendering::TextureHandle ResolveNeuralSlot(uint32_t slotIndex,
                                                    Rendering::TextureHandle fallback) const;
    };

    // スマートポインタエイリアス
    using MaterialResourcePtr = Container::TSharedPtr<MaterialResource>;
    using MaterialResourceWeakPtr = Container::TWeakPtr<MaterialResource>;

} // namespace NorvesLib::Core

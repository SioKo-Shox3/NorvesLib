#pragma once

#include "Component/BillboardComponent.h"
#include "Rendering/ImpostorBake.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    class RenderWorld;
    class TextureResources;
}

namespace NorvesLib::Core::Component
{
    /// アトラスの焼き込み情報を、保存可能な8整数の文字列で保持する。
    struct ImpostorAtlasMetadataValue
    {
        static constexpr TypeKind SchemaTypeKind = TypeKind::String;
        static constexpr const char *SchemaTypeName = "ImpostorAtlasMetadata";

        Rendering::ImpostorBakeMetadata Value;

        bool SerializeToString(Container::String &out) const;
        bool DeserializeFromString(const Container::String &in);
        bool operator==(const ImpostorAtlasMetadataValue &other) const;
    };

    class ImpostorComponent : public BillboardComponent
    {
        REFLECTION_CLASS(ImpostorComponent, BillboardComponent)

    public:
        ImpostorComponent();
        explicit ImpostorComponent(const FieldInitializer *initializer);
        explicit ImpostorComponent(const IUnknown *sourceObject);
        virtual ~ImpostorComponent();

        void BeginPlay() override;
        void Finalize() override;
        void OnPropertyApplied(const ClassProperty &property) override;

        bool SetBakedAtlas(Rendering::TextureHandle texture,
                           const Rendering::ImpostorBakeMetadata &metadata);
        bool SetBakedAtlas(Rendering::TextureHandle texture,
                           const Rendering::ImpostorBakeMetadata &metadata,
                           Container::StringView sourcePath);
        /// 外部の色アトラスがあるとき、法線・ORM・深度を引き受ける。無効なハンドルも渡せる。
        /// 異なるハンドルへ交換する前に ReleaseBakedAtlas で返す。失敗時は新しいハンドルを引き受けない。
        bool SetBakedMaterialAtlases(Rendering::TextureHandle normal,
                                     Rendering::TextureHandle orm,
                                     Rendering::TextureHandle depth);
        /// 焼きの結果から色と材質のアトラスを順に引き受ける。
        /// 材質の設定に失敗しても、先に設定した色アトラスは元に戻さない。
        bool SetBakedAtlases(const Rendering::ImpostorBakeResult& result);
        const TextureAssetRef &GetBakedAtlasAsset() const { return BakedAtlasAsset.Get(); }
        void SetBakedAtlasAsset(const TextureAssetRef &reference);
        void ReleaseBakedAtlas(Rendering::TextureResources &textures,
                               Rendering::RenderWorld *renderWorld = nullptr);
        void SetSourceMeshComponentId(uint64_t componentId);
        uint64_t GetSourceMeshComponentId() const { return m_SourceMeshComponentId; }
        /// 画面の大きさのしきい値が 0 のときに使う、距離の予備しきい値を設定する。
        void SetLODSwitchDistance(float distance);
        float GetLODSwitchDistance() const { return LODSwitchDistance; }
        /// 板の境界球の画面の大きさ（半径 / (距離 × tan(fov/2))、正射影は半径 / (OrthoHeight/2)）が
        /// この値より小さいとインポスターにする。0 なら LODSwitchDistance の距離のしきい値を使う。
        void SetLODSwitchScreenSize(float screenSize);
        float GetLODSwitchScreenSize() const { return LODSwitchScreenSize; }

        Rendering::TextureHandle GetBakedAtlasTextureHandle() const { return m_BakedAtlasTexture; }
        /// 法線アトラスのハンドルを返す。未設定なら無効なハンドルを返す。
        Rendering::TextureHandle GetBakedNormalAtlasTextureHandle() const { return m_BakedNormalAtlasTexture; }
        /// ORM アトラスのハンドルを返す。未設定なら無効なハンドルを返す。
        Rendering::TextureHandle GetBakedOrmAtlasTextureHandle() const { return m_BakedOrmAtlasTexture; }
        /// 深度アトラスのハンドルを返す。未設定なら無効なハンドルを返す。
        Rendering::TextureHandle GetBakedDepthAtlasTextureHandle() const { return m_BakedDepthAtlasTexture; }
        const Rendering::ImpostorBakeMetadata &GetBakedAtlasMetadata() const { return BakedAtlasMetadata->Value; }
        bool HasBakedAtlas() const { return m_BakedAtlasTexture.IsValid() && BakedAtlasMetadata->Value.IsValid(); }

        bool BuildBoardProxy(Rendering::BoardProxy &outProxy,
                             const Rendering::MaterialResources *materials = nullptr) const override;

    protected:
        void PollAssetBindings() override;

        PROPERTY(TextureAssetRef, BakedAtlasAsset)
        PROPERTY(ImpostorAtlasMetadataValue, BakedAtlasMetadata)
        PROPERTY(float, LODSwitchDistance)
        PROPERTY(float, LODSwitchScreenSize)
        Rendering::TextureHandle m_BakedAtlasTexture = Rendering::TextureHandle::Invalid();
        /// 外部から引き受けた法線アトラス。
        Rendering::TextureHandle m_BakedNormalAtlasTexture = Rendering::TextureHandle::Invalid();
        /// 外部から引き受けた ORM アトラス。
        Rendering::TextureHandle m_BakedOrmAtlasTexture = Rendering::TextureHandle::Invalid();
        /// 外部から引き受けた深度アトラス。
        Rendering::TextureHandle m_BakedDepthAtlasTexture = Rendering::TextureHandle::Invalid();
        bool m_bBakedAtlasExternal = false;
        Asset::AssetBinding m_AtlasBinding;
        uint64_t m_SourceMeshComponentId = 0;
    };

    using ImpostorComponentPtr = Container::TSharedPtr<ImpostorComponent>;
    using ImpostorComponentWeakPtr = Container::TWeakPtr<ImpostorComponent>;

} // namespace NorvesLib::Core::Component

DECLARE_CLASS_CAST_FLAG(NorvesLib::Core::Component::ImpostorComponent, NorvesLib::Core::EClassCastFlags::ImpostorComponent)

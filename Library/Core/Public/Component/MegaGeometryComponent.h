#pragma once

#include "MeshComponent.h"
#include "Asset/AssetBinding.h"
#include "Asset/AssetRef.h"
#include "Ecs/EcsRenderTypes.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/MegaGeometry/MegaGeometryTypes.h"

namespace NorvesLib::Core::Component
{

    /**
     * @brief World と Component の経路で MegaMesh を同期するコンポーネント。
     *
     * 変換と可視性は MeshComponent の状態を使う。
     */
    class MegaGeometryComponent : public MeshComponent
    {
        REFLECTION_CLASS(MegaGeometryComponent, MeshComponent)

    public:
        MegaGeometryComponent();
        explicit MegaGeometryComponent(const FieldInitializer *initializer);
        explicit MegaGeometryComponent(const IUnknown *sourceObject);
        virtual ~MegaGeometryComponent();

        Ecs::EcsComponentRowKind GetEcsRowKind() const override { return Ecs::EcsComponentRowKind::MegaGeometry; }
        /// MegaGeometry の proxy は MegaMesh の handle をそのまま渡すだけで、メッシュや材質の状態を読まない
        void CollectRenderResourceRefs(Container::VariableArray<Rendering::RenderResourceRef>& out) const override
        {
            (void)out;
        }

        void SetMegaMeshHandle(Rendering::MegaGeometry::MegaMeshHandle handle);
        void SetMegaMeshHandle(Rendering::MegaGeometry::MegaMeshHandle handle, Container::StringView sourcePath);
        Rendering::MegaGeometry::MegaMeshHandle GetMegaMeshHandle() const { return LoadMegaMeshHandle(); }
        bool HasMegaMesh() const { return LoadMegaMeshHandle().IsValid(); }

        const ModelAssetRef& GetModelAsset() const { return ModelAsset.Get(); }
        void SetModelAsset(const ModelAssetRef& reference);

        void BeginPlay() override;
        void Finalize() override;
        void OnPropertyApplied(const ClassProperty& property) override;
        void PollAssetBinding();

        /**
         * @brief SceneView 同期用の MegaGeometry proxy を構築する
         * @param outProxy 出力先
         * @return 有効な proxy を作れたら true
         */
        bool BuildMegaGeometryProxy(Rendering::MegaGeometryProxy &outProxy) const;

    protected:
        /// 論理パス。シーンから読むと BeginPlay で非同期に解決して束縛する。
        PROPERTY(ModelAssetRef, ModelAsset)

        void OnEcsRowAttached() override;
        void OnEcsRowDetaching() override;

    private:
        Rendering::MegaGeometry::MegaMeshHandle LoadMegaMeshHandle() const;
        void StoreMegaMeshHandle(Rendering::MegaGeometry::MegaMeshHandle handle);
        void RequestModelBinding();

        // 行が無い間だけ使う、保存対象ではない派生の状態。
        Rendering::MegaGeometry::MegaMeshHandle m_MegaMeshHandleFallback = Rendering::MegaGeometry::MegaMeshHandle::Invalid();
        Asset::AssetBinding m_ModelBinding;
    };

    using MegaGeometryComponentPtr = Container::TSharedPtr<MegaGeometryComponent>;
    using MegaGeometryComponentWeakPtr = Container::TWeakPtr<MegaGeometryComponent>;

} // namespace NorvesLib::Core::Component

// CastTo の高速経路で使う型のフラグ。
DECLARE_CLASS_CAST_FLAG(NorvesLib::Core::Component::MegaGeometryComponent, NorvesLib::Core::EClassCastFlags::MegaGeometryComponent)

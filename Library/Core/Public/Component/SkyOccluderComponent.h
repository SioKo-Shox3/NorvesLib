// 手置きの空・雨の遮蔽物の設定と描画登録を管理する。
#pragma once

#include "Component.h"
#include "Rendering/SkyOcclusion.h"

namespace NorvesLib::Core::Engine
{
    class Engine;
}

namespace NorvesLib::Core::Component
{
    /** 持ち主の姿勢と保存された設定から、空と雨に共通の遮蔽物を作ります。 */
    class SkyOccluderComponent : public Component
    {
        REFLECTION_CLASS(SkyOccluderComponent, Component)

    public:
        SkyOccluderComponent();
        explicit SkyOccluderComponent(const FieldInitializer* initializer);
        explicit SkyOccluderComponent(const IUnknown* sourceObject);
        virtual ~SkyOccluderComponent();

        virtual void Initialize() override;
        virtual void Finalize() override;
        virtual void BeginPlay() override;
        virtual void EndPlay() override;

        /** 持ち主が無効、または有効な遮蔽物を構築できない場合は false を返します。 */
        bool BuildSkyOccluderProxy(Rendering::SkyOccluderProxy& out) const;
        /** 持ち主のワールド AABB から寸法と中心を設定します。失敗時は値を変えません。 */
        bool InitializeFromOwnerBounds();
        /** 現在の値を比較し、変更された遮蔽物だけを描画側へ公開します。 */
        void RefreshSkyOccluder();

    protected:
        PROPERTY(Rendering::SkyOccluderShape, Shape)
        PROPERTY(Rendering::SkyOccluderClass, OccluderClass)
        /** メートル単位の局所半寸法。持ち主の拡大率の絶対値を掛けます。 */
        PROPERTY(Math::Vector3, HalfExtents)
        /** 持ち主の回転を基準としたメートル単位のずれ。拡大率は掛けません。 */
        PROPERTY(Math::Vector3, CenterOffset)
        /** Y 軸まわりの追加回転（ラジアン）。正方向は局所 +Z がワールド +X へ向く方向です。 */
        PROPERTY(float, YawOffsetRadians)
        /** ワールドの鉛直方向の底の切り上げ量（メートル）。 */
        PROPERTY(float, BaseOffset)
        PROPERTY(float, Opacity)
        PROPERTY(bool, bDynamic)
        PROPERTY(bool, bBlocksRain)

    private:
        void SetDefaults();

        Engine::Engine* m_RegisteredEngine = nullptr;
        uint32_t m_SkyOccluderHandle = 0u;
        Rendering::SkyOccluderProxy m_LastProxy;
    };

    using SkyOccluderComponentPtr = Container::TSharedPtr<SkyOccluderComponent>;
    using SkyOccluderComponentWeakPtr = Container::TWeakPtr<SkyOccluderComponent>;
} // namespace NorvesLib::Core::Component

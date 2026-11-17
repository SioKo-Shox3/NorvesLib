// 持ち主の姿勢と保存された設定から局所霧を描画へ登録する。
#pragma once

#include "Component.h"
#include "Rendering/VolumetricFog.h"

namespace NorvesLib::Core::Engine
{
    class Engine;
}

namespace NorvesLib::Core::Component
{
    /** 持ち主の変換に追従する楕円体の局所霧を管理します。 */
    class LocalFogVolumeComponent : public Component
    {
        REFLECTION_CLASS(LocalFogVolumeComponent, Component)

    public:
        LocalFogVolumeComponent();
        explicit LocalFogVolumeComponent(const FieldInitializer* initializer);
        explicit LocalFogVolumeComponent(const IUnknown* sourceObject);
        virtual ~LocalFogVolumeComponent();

        virtual void Initialize() override;
        virtual void Finalize() override;
        virtual void BeginPlay() override;
        virtual void EndPlay() override;

        /** 持ち主が無効、または有効な局所霧を構築できない場合は false を返します。 */
        bool BuildLocalFogVolumeProxy(Rendering::LocalFogVolumeProxy& out) const;
        /** 値を比較し、変更された局所霧だけを描画側へ公開します。 */
        void RefreshLocalFogVolume();

    protected:
        /** 持ち主の回転を基準とした中心のずれ（メートル）。拡大率は掛けません。 */
        PROPERTY(Math::Vector3, CenterOffset)
        /** 局所の各軸における楕円体の半径（メートル）。 */
        PROPERTY(Math::Vector3, Radius)
        /** 中心の消散係数（1/メートル）。 */
        PROPERTY(float, Density)
        /** 散乱の色（線形 RGB、0..1）。 */
        PROPERTY(Math::Vector3, Color)
        /** 半径の位置における標準偏差の倍数（0.5..8）。 */
        PROPERTY(float, Falloff)
        /** 濃淡の量（0..1）。 */
        PROPERTY(float, NoiseAmount)
        /** 濃淡の大きさ（メートル）。 */
        PROPERTY(float, NoiseScaleMeters)

    private:
        void SetDefaults();

        Engine::Engine* m_RegisteredEngine = nullptr;
        uint32_t m_LocalFogVolumeHandle = 0u;
        Rendering::LocalFogVolumeProxy m_LastProxy;
    };

    using LocalFogVolumeComponentPtr = Container::TSharedPtr<LocalFogVolumeComponent>;
    using LocalFogVolumeComponentWeakPtr = Container::TWeakPtr<LocalFogVolumeComponent>;
}

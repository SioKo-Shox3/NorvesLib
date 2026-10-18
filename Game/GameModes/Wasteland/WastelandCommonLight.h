#pragma once

// 荒野の共通の照明と灰色の板（照明の尺度の検証用。開発ビルドの起動引数でだけ使う）。
// --wasteland-common-light: 空の大気の散乱・空気遠近・高さフォグ・雲を切り、空の光と背景を一様な空の放射輝度に、太陽を
//   照度（太陽に垂直な面の lux）と向きを決めた方向光にする。値は Scripts/Terrain/CommonLight.json（エンジンと照合の台本が共有する）。
// --common-light-case=sun|sky: 太陽だけ（空の放射輝度 0）か、空だけ（太陽 0）にする。指定が無ければ両方。
// --wasteland-gray-plate: 視点の前に灰色の板（鏡面の無いアルベドの水平の面）を置き、カメラを板へ向ける。間接光（RTGI）は切る
//   （周りの地形の照り返し・遮蔽を板の値に入れない）。照合は Scripts/Terrain/ReferenceCheck.py --plate。
// --wasteland-gray-material: 地形の区画の材質を、テクスチャと 3 層の混ぜ合わせの無い灰色（CommonLight.json の gray_material）に
//   替える。材質の BaseColor は GBuffer のアルベドと、RTGI・DDGI の光線の当たりの反射率の両方に使われるので、両方が同じ灰色になる。
//   照合は Scripts/Terrain/ReferenceCheck.py --gray（Blender Cycles の同じ材質の絵と比べる）。

#include "Core/Public/Container/Containers.h"
#include "Core/Public/Math/Vector3.h"
#include "Core/Public/Rendering/MaterialTypes.h"

#include <cstdint>

namespace NorvesLib::Core
{
    class Entity;
    namespace GameMode
    {
        struct GameModeContext;
    }
    namespace Component
    {
        class DirectionalLightComponent;
    }
    namespace Rendering::Terrain
    {
        class TerrainHeightField;
    }
} // namespace NorvesLib::Core

namespace Game::Scenes
{
    struct FreeCameraPose;
}

namespace Game::GameModes::WastelandCommonLight
{
    /// 共通の照明のうち点ける光（--common-light-case）
    enum class LightCase : uint8_t
    {
        /// 太陽と空の両方
        Both,
        /// 太陽だけ（空の放射輝度 0）
        SunOnly,
        /// 空だけ（太陽 0）
        SkyOnly,
    };

    /// 起動引数で選ぶもの
    struct Options
    {
        bool bEnabled = false;
        LightCase Case = LightCase::Both;
        bool bGrayPlate = false;
        bool bGrayMaterial = false;
    };

    /// 視点ごとの太陽の向き（CommonLight.json の views）
    struct ViewSun
    {
        NorvesLib::Core::Container::String ViewName;
        float AltitudeDegrees = 0.0f;
        float AzimuthDegrees = 0.0f;
    };

    /// CommonLight.json の中身
    struct Definition
    {
        /// 一様な空の放射輝度（cd/m²、線形の Rec.709 の RGB）
        NorvesLib::Math::Vector3 SkyRadiance = NorvesLib::Math::Vector3::Zero;
        /// 太陽に垂直な面の照度（lux）。大気の透過率は掛けない
        float SunIlluminanceNormalLux = 0.0f;
        /// views に無い視点の太陽の向き（方位角は +X（東）から +Z（南）へ測る）
        float DefaultSunAltitudeDegrees = 0.0f;
        float DefaultSunAzimuthDegrees = 0.0f;
        NorvesLib::Core::Container::VariableArray<ViewSun> Views;
        /// 灰色の板のアルベド（線形）と粗さ
        float PlateAlbedo = 0.18f;
        float PlateRoughness = 1.0f;
        /// 板の一辺（m）・板の面の地面からの高さ（m）
        float PlateSizeMeters = 3.0f;
        float PlateHeightAboveGroundMeters = 1.5f;
        /// 板を撮るカメラ: 視点の位置から視線の水平の向きへ板の中心までの距離（m）と、板の面からの目の高さ（m）
        float PlateForwardMeters = 5.2f;
        float CameraHeightAbovePlateMeters = 3.0f;
        /// 灰色の地形の材質のアルベド（線形）と粗さ。誘電体（金属度 0、エンジンの F0 は 0.04 の固定）
        float GrayMaterialAlbedo = 0.3f;
        float GrayMaterialRoughness = 1.0f;
    };

    /// 共通の照明の状態（荒野のモードの間だけ持つ）
    struct State
    {
        Options Opts;
        Definition Def;
        bool bLoaded = false;
        /// 太陽の方向光（太陽を点けないときは作らない。実体は GameModeScope が返す）
        NorvesLib::Core::Component::DirectionalLightComponent *pSun = nullptr;
        /// 灰色の板の実体と材質（GameModeScope が返す）
        NorvesLib::Core::Entity *pPlate = nullptr;
        NorvesLib::Core::Rendering::MaterialHandle PlateMaterial;
        bool bPlateMeshRegistered = false;
        /// 板のために切った RTGI の元の値
        bool bPreviousRTGI = true;
        bool bRTGIOverridden = false;
    };

    /// CommonLight.json を読む。失敗したら理由を outError に入れて false
    bool LoadDefinition(Definition &outDefinition, NorvesLib::Core::Container::String &outError);

    /// 視点の名前に対応する太陽の向き（views に無ければ既定）
    void FindSun(const Definition &definition, const NorvesLib::Core::Container::String &viewName,
                 float &outAltitudeDegrees, float &outAzimuthDegrees);

    /// 起動時に 1 回: 定義を読み、板の材質・メッシュを用意する。定義が読めなければ false（呼び出し側はモードを失敗にする）。
    /// 灰色の地形の材質（Opts.bGrayMaterial）の値も、ここで読んだ Def から地形を置く側が引く
    bool Enter(NorvesLib::Core::GameMode::GameModeContext &ctx, State &state);

    /// 視点ごとに: 空・霧・空気遠近・雲を切り、一様な空と太陽をその視点の向きで置く
    void ApplyEnvironment(NorvesLib::Core::GameMode::GameModeContext &ctx, State &state,
                          const NorvesLib::Core::Container::String &viewName);

    /// 視点ごとに（板を置くときだけ）: 視点の前に板を置き、カメラの姿勢を板へ向けたものに替える
    void PlacePlate(NorvesLib::Core::GameMode::GameModeContext &ctx, State &state,
                    const NorvesLib::Core::Rendering::Terrain::TerrainHeightField *pHeightField,
                    Game::Scenes::FreeCameraPose &inoutPose);

    /// モードを出るとき: RTGI と一様な空の設定を戻す（実体は GameModeScope が返す）
    void Leave(NorvesLib::Core::GameMode::GameModeContext &ctx, State &state);

} // namespace Game::GameModes::WastelandCommonLight

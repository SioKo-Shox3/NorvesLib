#pragma once

// ---------------------------------------------------------------------------
// material-gallery（材質ギャラリー）
//
// Poly Haven の石の床（stone_floor）を敷いた屋外の展示場に台座を 4 列並べ、手元の Poly Haven の地面・壁・木・金属・屋根と
// 荒野の組の材質を、台座の上の傾けた板（視差オクルージョン付き）と球で見せる。別の一角のひな壇には、金属度 0〜1（左右）×
// 粗さ 0〜1（奥へ上がる段）の 6×6 の球を並べる。各台座の前面には材質の id の名札を、画面へ投影した ImGui の文字で出す。
// 光は物理空の昼の太陽。視点は SceneCatalog.h の kMaterialGalleryViews（overview・textures・spheres・close）。
// テクスチャは Scripts/FetchPolyHavenTextures.ps1 -Set gallery で落とし、CookAssets で焼く（Assets/AssetSets の
// Rendering3DTestStartupMaterials・Rendering3DTestStressTextures・WastelandTerrain・MaterialGalleryTextures）。
// 焼いていないテクスチャは読まず、MATERIAL_GALLERY_TEXTURE_MISSING を出してその材質を無地のまま描く。
// ---------------------------------------------------------------------------

#include "Core/Public/Asset/AssetSystem.h"
#include "Core/Public/Container/Containers.h"
#include "Core/Public/Container/PointerTypes.h"
#include "Core/Public/Container/String.h"
#include "Core/Public/Delegate/Delegate.h"
#include "Core/Public/GameMode/GameModeContext.h"
#include "Core/Public/GameMode/IGameMode.h"
#include "Core/Public/GameMode/TGameMode.h"
#include "Core/Public/Rendering/MaterialTypes.h"
#include "Pause/PauseMenu.h"
#include "Scenes/FreeCameraRig.h"
#include "Scenes/SceneViewSwitch.h"

#if defined(NORVES_ENABLE_IMGUI)
#include "ImGuiModule/IImGuiView.h"
#endif

#include <cstdint>

namespace Game::GameModes
{

    /// 読み込み中の材質 1 つ。テクスチャがそろったら TargetMaterial を CreateData で更新する
    struct GalleryPendingMaterial
    {
        NorvesLib::Core::Rendering::MaterialHandle TargetMaterial;
        NorvesLib::Core::Rendering::MaterialCreateData CreateData;
        uint32_t PendingTextureCount = 0;
        bool bCancelled = false;
    };

    /// 名札 1 枚（ワールドの位置と文字）
    struct GalleryLabel
    {
        float Position[3] = {0.0f, 0.0f, 0.0f};
        /// UTF-8 の固定の文字列（材質の id か、球の格子の軸の値）
        const char *Text = nullptr;
    };

    /// 名札を隠す箱（展示 1 つの台座と、その上の板・球の範囲の軸に沿った箱）。ImGui の文字は深度を持たないので、
    /// カメラから名札への視線がこの箱を名札より手前で通るときは、その名札を描かない
    struct GalleryOccluder
    {
        float Min[3] = {0.0f, 0.0f, 0.0f};
        float Max[3] = {0.0f, 0.0f, 0.0f};
    };

    /// 名札を投影するカメラ（Routine が毎 Tick 書き、名札のビューが読む。どちらも主スレッド）
    struct GalleryLabelCamera
    {
        float Position[3] = {0.0f, 0.0f, 0.0f};
        float YawDegrees = 0.0f;
        float PitchDegrees = 0.0f;
        /// 縦の画角[度]
        float FieldOfViewDegrees = 60.0f;
        bool bValid = false;
    };

#if defined(NORVES_ENABLE_IMGUI)
    /// 名札を画面へ投影して描く ImGui のビュー。窓を作らず背景の描画リストへ描くので、入力を奪わない
    class MaterialGalleryLabelView final : public NorvesLib::Modules::Gui::IImGuiView
    {
    public:
        /// labels・occluders・camera は借用（Mode のデータが所有し、ビューより長く生きる）
        MaterialGalleryLabelView(const NorvesLib::Core::Container::VariableArray<GalleryLabel> *labels,
                                 const NorvesLib::Core::Container::VariableArray<GalleryOccluder> *occluders,
                                 const GalleryLabelCamera *camera)
            : m_pLabels(labels), m_pOccluders(occluders), m_pCamera(camera)
        {
        }

        void OnImGui() override;

        const char *GetViewName() const override
        {
            return "MaterialGalleryLabels";
        }

    private:
        const NorvesLib::Core::Container::VariableArray<GalleryLabel> *m_pLabels = nullptr;
        const NorvesLib::Core::Container::VariableArray<GalleryOccluder> *m_pOccluders = nullptr;
        const GalleryLabelCamera *m_pCamera = nullptr;
    };
#endif

    struct MaterialGalleryData
    {
        /// 起動時の視点の名前（--scene-view。空ならシーンの既定の視点）
        NorvesLib::Core::Container::String m_ViewName;
        /// 入力の台本のパス（--camera-script。空なら流さない）
        NorvesLib::Core::Container::String m_CameraScriptPath;
        /// 起動時にカメラパスの途中へ置く時刻[秒]（--camera-path-time）。負なら置かない
        float m_CameraPathTime = -1.0f;
        /// 組み立てが済んだところからカメラパスを再生する（--camera-path-play）
        bool m_bCameraPathPlay = false;
        /// ポーズメニュー（非所有）。開いている間はカメラを動かさない。無いなら nullptr
        const Game::Pause::PauseMenu *m_pPauseMenu = nullptr;
        /// 実行中の視点の切り替えの依頼（非所有。--capture-views の撮影が使う）。無いなら nullptr
        Game::Scenes::SceneViewSwitch *m_pViewSwitch = nullptr;
        /// クック済みの資産を引く AssetSystem の窓口（テクスチャが焼いてあるかを確かめる）
        NorvesLib::Core::Delegate<NorvesLib::Core::Container::TSharedPtr<const NorvesLib::Core::Asset::AssetSystem>>
            m_GetAssetSystem;

        Game::Scenes::FreeCameraRig m_Rig;
        uint32_t m_TickCount = 0;
        /// 読み込み中・読み込み済みの材質（床と展示の材質）。Leave で読み込みの結果を捨てる印を立てる
        NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Container::TSharedPtr<GalleryPendingMaterial>>
            m_PendingMaterials;
        /// テクスチャがそろっていない材質の数（0 になったら組み立ての完了）
        uint32_t m_PendingMaterialCount = 0;
        /// 組み立ての完了を一度だけ記録した
        bool m_bMaterialsReadyLogged = false;

        NorvesLib::Core::Container::VariableArray<GalleryLabel> m_Labels;
        NorvesLib::Core::Container::VariableArray<GalleryOccluder> m_Occluders;
        GalleryLabelCamera m_LabelCamera;
#if defined(NORVES_ENABLE_IMGUI)
        MaterialGalleryLabelView m_LabelView{&m_Labels, &m_Occluders, &m_LabelCamera};
        bool m_bLabelViewRegistered = false;
#endif
    };

    class MaterialGalleryRoutine
    {
    public:
        NorvesLib::Core::GameMode::GameModeEnterResult Enter(NorvesLib::Core::GameMode::GameModeContext &ctx,
                                                             MaterialGalleryData &data);
        void Tick(NorvesLib::Core::GameMode::GameModeContext &ctx, MaterialGalleryData &data, float deltaTime);
        void Leave(NorvesLib::Core::GameMode::GameModeContext &ctx, MaterialGalleryData &data,
                   NorvesLib::Core::GameMode::GameModeExitReason reason);

        static constexpr const char *DebugName = "MaterialGallery";
    };

    using MaterialGalleryMode = NorvesLib::Core::GameMode::TGameMode<MaterialGalleryRoutine, MaterialGalleryData>;

} // namespace Game::GameModes

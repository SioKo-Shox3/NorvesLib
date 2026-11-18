// 画面に依存しないレベルマーカーと地形マスクの編集・保存・読み込みを提供する。
#pragma once

#include "Container/String.h"
#include "Container/VariableArray.h"
#include "Gameplay/Level/LevelPaintMask.h"
#include "Math/GeometryTypes.h"
#include "Scene/LevelSnap.h"
#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core
{
    class Entity;
    class World;
}

namespace NorvesLib::Core::Scene
{
    class SceneQuery;
}

namespace NorvesLib::Modules::Gameplay::Level
{
    /// 配置操作で扱うマーカーの種類。
    enum class ELevelMarkerKind : uint8_t
    {
        Territory, RestPoint, FastTravel, Poi, AnimalSpawn, ScentTrail, ScentTrailPoint
    };

    inline constexpr uint32_t kLevelMarkerKindCount = 7;

    /// Key の接頭辞に使う英語の識別子を返す。無効な種類では空文字列を返す。
    const char* GetLevelMarkerKindId(ELevelMarkerKind kind);
    /// Component から種類を判定する。種類を判定できなければ false で出力を変えない。
    bool TryGetLevelMarkerKind(const Core::Entity& entity, ELevelMarkerKind& outKind);

    /// 編集の操作が作ったルートと、破棄予約する Entity を持ち主へ知らせる口。
    /// Game 側はゲームモードの寿命の追跡(GameModeScope の TrackObject / Untrack)へつなぐ。
    class ILevelEditEntityOwner
    {
    public:
        virtual ~ILevelEditEntityOwner() = default;
        /// 親を持たないマーカーを作った直後と、読み込みで生成したルートごとに呼ぶ。
        virtual void OnLevelEditRootSpawned(Core::Entity& root) = 0;
        /// Destroy() で破棄予約する直前に、破棄する部分木の Entity ごとに親から子の順で呼ぶ。
        virtual void OnLevelEditBeforeDestroy(Core::Entity& entity) = 0;
    };

    /// 選択を ObjectId で保持し、World に対するマーカーの編集操作を行う。
    class LevelEditOperations
    {
    public:
        static constexpr float kDefaultRayDistance = 2000.0f;
        static constexpr float kDefaultMaxDrop = 500.0f;
        static constexpr float kDefaultPickRadius = 1.5f;
        /// 草の密度の基準値。乗算時に中立となる値。
        static constexpr uint8_t kGrassDensityBaselineValue = 255;
        /// 枯れの基準値。手続きで生成した基準値に差し替えるまで使う。
        static constexpr uint8_t kDrynessBaselineValue = 0;

        /// 持ち主への通知先を借用する。nullptr で通知先を外す。
        void SetEntityOwner(ILevelEditEntityOwner* owner);
        /// 借用中の通知先を返す。
        ILevelEditEntityOwner* GetEntityOwner() const;

        /// トリガーを除く物理光線の当たりを返す。当たりが無ければ高さ場を刻みで調べ、最初の地面との交差を二分探索する。
        /// 距離は実距離。地面が無い場合や入力が無効な場合は false で出力を変えない。
        bool RaycastGround(const Core::Scene::SceneQuery& query, const Math::Ray& ray, float maxDistance,
                           Math::Vector3& outPoint, Math::Vector3& outNormal) const;

        /// 地面の点に GroundOffset を加えて配置し、吸着設定に従って地面へ合わせ直して選択する。
        /// 空の Key は自動採番し、使用中の Key は拒否する。痕跡の点は選択中の痕跡、または選択中の点の親の子に置く。
        /// Territory には球の EncounterZone を付ける。失敗した場合は nullptr で選択を保つ。
        Core::Entity* PlaceMarker(Core::World& world, const Core::Scene::SceneQuery& query, ELevelMarkerKind kind,
                                  const Math::Vector3& groundPoint, const Core::Container::String& requestedKey = {});

        /// Key を持つ有効なマーカーがあれば true。ignore と破棄予約の部分木は比較しない。
        bool IsKeyInUse(const Core::World& world, const Core::Container::String& key, const Core::Entity* ignore = nullptr) const;
        /// 種類の識別子に、001 からの最小の未使用番号を 3 桁以上で付けた Key を返す。
        Core::Container::String MakeUniqueKey(const Core::World& world, ELevelMarkerKind kind) const;
        /// 空・使用中の Key、有効な LevelMarkerComponent を持たない Entity を拒否する。
        bool SetMarkerKey(Core::World& world, Core::Entity& marker, const Core::Container::String& newKey);
        /// Key を持つ有効なマーカーを検索する。無ければ nullptr。
        Core::Entity* FindMarkerByKey(const Core::World& world, const Core::Container::String& key) const;

        /// 出力を空にし、LevelMarker または痕跡の点を持つ Entity を親から子の順に集める。破棄予約の部分木は除く。
        void CollectMarkers(const Core::World& world, Core::Container::VariableArray<Core::Entity*>& outMarkers) const;
        /// 光線から指定半径以内に中心があるマーカーのうち、光線に沿って最も手前のものを返す。
        Core::Entity* PickMarker(const Core::World& world, const Math::Ray& ray, float pickRadius = kDefaultPickRadius) const;

        /// マーカーの ObjectId を選択として保持する。nullptr または無効なマーカーなら解除する。
        void Select(const Core::Entity* marker);
        /// 選択を解除する。
        void ClearSelection();
        /// ObjectId で選択を引き直す。破棄予約の部分木にある場合や消滅した場合は nullptr。
        Core::Entity* GetSelected(const Core::World& world) const;
        /// 保持している選択の ObjectId を返す。未選択なら 0。
        uint64_t GetSelectedObjectId() const;

        /// 回転と拡大を保って移動し、吸着設定に従って地面へ合わせ直す。痕跡の点は親の設定を使う。
        /// 変換の更新後に子孫の痕跡の点も合わせ直す。地面が見つからなければ指定位置を保つ。
        bool MoveMarker(Core::World& world, const Core::Scene::SceneQuery& query, Core::Entity& marker, const Math::Vector3& position);
        /// マーカーを破棄予約する。選択がその部分木にある場合は先に解除する。
        bool DeleteMarker(Core::World& world, Core::Entity& marker);
        /// 全マーカーを ReSnapAllLevelMarkers で再吸着する。
        Core::Scene::LevelSnap::LevelSnapStats ReSnapAll(Core::World& world, const Core::Scene::SceneQuery& query,
                                                       float maxDrop = kDefaultMaxDrop);

        /// LevelMarker を持つ World のルートとその部分木だけをシーン JSON に保存する。破棄予約の部分木は除く。
        /// 一時ファイルから置換し、失敗しても既存の保存先を保つ。保存数は成功時のみ返し、エラー出力は最初に空にする。
        bool SaveMarkers(const Core::World& world, const Core::Container::String& filePath,
                         size_t* pOutSavedRoots = nullptr, Core::Container::String* pOutError = nullptr) const;
        /// ファイルの解析後に選択を解除し、既存マーカーのルートを破棄予約して Gameplay のクラスを登録してから加算読み込みする。
        /// 出力は最初に空にし、生成に成功したルートを返す。ファイルを開けない・解析できないときは何も変えずに false。
        /// 解析が通った後の生成の失敗では、消したマーカーは戻らず、生成できたルートは残る。
        bool LoadMarkers(Core::World& world, const Core::Container::String& filePath,
                         Core::Container::VariableArray<Core::Entity*>* pOutRoots = nullptr);

        /// 全層を配置へ合わせ、置き場の PNG を読む。ファイルが無い層は基準値で始める。
        /// 読めない層があれば状態を保って false。成功時に置き場と各層の読込記録を保持する。
        /// エラー出力は最初に空にする。
        bool BindPaintMasks(const LevelPaintMaskLayout& layout, const Core::Container::String& masksDirectory,
                            Core::Container::String* pOutError = nullptr);
        /// 全層を空にし、置き場と読込記録を忘れる。
        void UnbindPaintMasks();
        /// 全層が配置に合わせ済みなら true。
        bool HasPaintMasks() const;
        /// 合わせた配置。合わせていなければ無効な配置を返す。
        const LevelPaintMaskLayout& GetPaintMaskLayout() const;
        /// 覚えている置き場を返す。
        const Core::Container::String& GetPaintMaskDirectory() const;
        /// 無効な層・合わせていない場合は nullptr。
        const LevelPaintMask* GetPaintMask(ELevelPaintLayer layer) const;
        /// 最後の BindPaintMasks・LoadPaintMasks でファイルから読んだ層なら true。
        bool WasPaintMaskLoadedFromFile(ELevelPaintLayer layer) const;

        /// 地面の点の XZ を中心に塗る・消す。変わった標本数を返す。
        /// 合わせていない場合や無効な層では 0。
        uint32_t PaintMask(ELevelPaintLayer layer, const Math::Vector3& groundPoint, const LevelPaintBrush& brush);
        uint32_t EraseMask(ELevelPaintLayer layer, const Math::Vector3& groundPoint, const LevelPaintBrush& brush);
        /// 基準値を差し替える。bResetValues なら値も戻す。未設定・無効な層・標本数違いは false。
        bool SetPaintMaskBaseline(ELevelPaintLayer layer, Core::Container::Span<const uint8_t> baseline, bool bResetValues);

        /// 全層を指定の置き場へ保存する。空なら覚えている置き場を使う。
        /// 成功した層の変更の印を消す。一層でも失敗すれば false で、成功したファイルは残る。
        /// 保存数は成功時のみ返し、保存数とエラー出力は最初に空にする。
        bool SavePaintMasks(const Core::Container::String& masksDirectory, size_t* pOutSavedLayers = nullptr,
                            Core::Container::String* pOutError = nullptr);
        /// 覚えている置き場から全層を読み直す。基準値は保ち、無い層の値は基準値へ戻す。
        /// 読めない層があれば状態を保って false。未設定なら false。エラー出力は最初に空にする。
        bool LoadPaintMasks(Core::Container::String* pOutError = nullptr);

    private:
        LevelPaintMask m_PaintMasks[kLevelPaintLayerCount];
        bool m_bPaintMaskLoadedFromFile[kLevelPaintLayerCount] = {};
        Core::Container::String m_PaintMaskDirectory;
        ILevelEditEntityOwner* m_pEntityOwner = nullptr;
        uint64_t m_SelectedObjectId = 0;
    };
} // namespace NorvesLib::Modules::Gameplay::Level

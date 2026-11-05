#pragma once

#include "Container/Containers.h"
#include "Delegate/Delegate.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include "Rendering/RenderTypes.h"
#include <type_traits>

// 重いサブシステムヘッダのインクルードを避け、前方宣言のみ使用する。
// RenderResources の具体的な API（Meshes()/MegaGeometry()）は
// GameModeScope.cpp でのみ必要なため、ここでは前方宣言で十分。
namespace NorvesLib::Core
{
    class World;

    namespace Rendering
    {
        class RenderResources;
    }
}

namespace NorvesLib::Core::GameMode
{
    /** @brief Entity の除去に対する後始末の実行位置 */
    enum class GameModeCleanupPhase : uint8_t
    {
        BeforeEntities,
        AfterEntities,
    };

    /** @brief 非同期で受け取るテクスチャの所有者 */
    enum class GameModeAsyncTextureOwnership : uint8_t
    {
        Cache,
        Scope,
    };

    /** @brief World と描画資源に現在登録されている数 */
    struct GameModeScopeCounts
    {
        uint64_t RootEntities = 0;
        uint64_t Textures = 0;
        uint64_t Meshes = 0;
        uint64_t Models = 0;
        uint64_t PendingAsyncLoads = 0;
    };

    /** @brief 基準から数が変わった資源 */
    struct GameModeLeakEntry
    {
        Container::String Name;
        uint64_t Baseline = 0;
        uint64_t Current = 0;
    };

    /**
     * @brief ゲームモードスコープ
     *
     * 1つのゲームモードが Enter〜Leave の間に生成したリソースを追跡し、
     * Leave 時に確立された順序で安全にクリーンアップするためのヘルパー。
     *
     * クリーンアップ順序:
     *   BeforeEntities — 非同期の取り消し・音の停止など（後入れ先出し）
     *   1) World::RemoveObject      — SceneView プロキシを除去
     *   AfterEntities — Entity が参照していた資源の後始末（後入れ先出し）
     *   2) MeshResources::Unregister — GPU メッシュデータを解放
     *   3) MegaGeometryResources::ReleaseModel — MegaGeometry モデルを解放
     *   4) MaterialResources::Release — 材質を解放（材質が持つ VT も返す）
     *   5) TextureResources::ReleaseTexture — 追跡したテクスチャを解放
     *
     * 非コピー設計（二重 Cleanup 防止）。
     *
     * 非同期モデルは TrackAsyncModelLoad、テクスチャの呼び戻しは WrapAsyncTextureCallback で保護する。
     * 基準は構築時（状態機械が Enter の直前に作る）、漏れの検査は破棄時（Leave と最後の Cleanup の後）。
     * キャッシュが持つテクスチャやモデルは最初の Leave で差に出ることがある。
     */
    class GameModeScope
    {
    public:
        GameModeScope();
        GameModeScope(
            NorvesLib::Core::World* world,
            NorvesLib::Core::Rendering::RenderResources* renderResources);
        ~GameModeScope();

        // 非コピー（二重 Cleanup 防止）
        GameModeScope(const GameModeScope&) = delete;
        GameModeScope& operator=(const GameModeScope&) = delete;

        /**
         * @brief Entity を追跡リストへ追加
         *
         * Cleanup() が呼ばれると、root は World::RemoveObject、child は World::RemoveEntity で除去される。
         * @param object 追跡対象の Entity（非所有、World が所有）
         */
        void TrackObject(NorvesLib::Core::Entity* object);

        /**
         * @brief World に Entity を生成し、Cleanup 対象として追跡します
         * @tparam T Entity派生型
         * @param parent 親Entity。nullptrならWorld直下root
         * @return 生成されたEntity。失敗時nullptr
         */
        template <typename T = NorvesLib::Core::Entity>
        T* SpawnObject(NorvesLib::Core::Entity* parent = nullptr)
        {
            static_assert(std::is_base_of_v<NorvesLib::Core::Entity, T>, "T must derive from Entity");

            if (m_pWorld == nullptr)
            {
                return nullptr;
            }

            T* object = m_pWorld->SpawnEntity<T>(parent);
            if (object != nullptr)
            {
                TrackObject(object);
            }
            return object;
        }

        /**
         * @brief MeshDataHandle を追跡リストへ追加
         *
         * Cleanup() が呼ばれると MeshResources::Unregister が呼ばれる。
         * @param handle 追跡対象のメッシュハンドル
         */
        void TrackMesh(NorvesLib::Core::Rendering::MeshDataHandle handle);

        /**
         * @brief ModelHandle を追跡リストへ追加
         *
         * Cleanup() が呼ばれると MegaGeometryResources::ReleaseModel が呼ばれる。
         * @param handle 追跡対象のモデルハンドル
         */
        void TrackModel(NorvesLib::Core::Rendering::ModelHandle handle);

        /**
         * @brief MaterialHandle を追跡リストへ追加
         *
         * Cleanup() が呼ばれると MaterialResources::Release が呼ばれる。材質が参照する VT（仮想テクスチャ）は
         * 材質ごとに作られて誰も共有しないため、材質と一緒に返す。パスで読んだテクスチャは
         * テクスチャのキャッシュが所有するので返さない。
         * @param handle 追跡対象の材質ハンドル
         */
        void TrackMaterial(NorvesLib::Core::Rendering::MaterialHandle handle);

        /**
         * @brief TextureHandle を追跡リストへ追加
         *
         * Cleanup() が呼ばれると TextureResources::ReleaseTexture が呼ばれる。CreateTexture などで
         * シーンが作った匿名のテクスチャに使う。キャッシュが所有するハンドルは追跡しない。
         * @param handle 追跡対象のテクスチャハンドル
         */
        void TrackTexture(NorvesLib::Core::Rendering::TextureHandle handle);

        /**
         * @brief 型を問わない後始末を登録する。未束縛は無視する。
         *
         * Cleanup 中、実行中・未実行の段へ追加した登録はその段で実行する（実行中なら次に呼ぶ）。
         * 終了済みの段への登録は最後の段の後でもう一巡し、両段が空になるまで実行する。
         * 各段は末尾から取り出してから呼ぶ。Cleanup 後の登録は次の Cleanup で実行する。
         */
        void AddCleanup(GameModeCleanupPhase phase, Delegate<void> callback);

        /**
         * @brief 非同期モデルの要求を BeforeEntities で取り消す。0 または描画資源なしは無視する。
         * 読み込み完了で得たモデルは呼び出し側が TrackModel する。
         */
        void TrackAsyncModelLoad(uint32_t requestId);

        /**
         * @brief 世代が終了した後の呼び戻しを抑止する。GameThread 専用。
         *
         * LoadTextureAsync のパスのテクスチャはキャッシュが持つので Cache を使う。
         * Scope は呼び出し側が持つ匿名のテクスチャの読み込み向けで、生存中は追跡し、終了後は解放する。
         * RenderResources は呼び戻しが終わるまで生存していること。
         */
        Delegate<void, Rendering::TextureHandle> WrapAsyncTextureCallback(
            Delegate<void, Rendering::TextureHandle> onLoaded,
            GameModeAsyncTextureOwnership ownership = GameModeAsyncTextureOwnership::Cache);

        /** @brief 現在の数を取得する。World / RenderResources がない項目は 0。全構成で利用できる。 */
        GameModeScopeCounts CaptureCounts() const;

        /** @brief 独自の数と登録時の基準を控える。未束縛は無視し、Release では何もしない。 */
        void AddLeakCounter(const char* name, Delegate<uint64_t> counter);

        /** @brief 組み込みの5項目と独自の数のうち基準と異なる項目を返す。Release では空。 */
        Container::VariableArray<GameModeLeakEntry> CollectLeaks() const;

        /**
         * @brief 既に呼び出し側が World から除去済みの Entity を追跡対象から外す
         *
         * 呼び出し側が自前で World::RemoveObject 済みのオブジェクトを追跡から外す。
         * このメソッド自身は World::RemoveObject を呼ばない（解放済みポインタへの
         * 二重 RemoveObject を防ぐ）。追跡していない、または null の場合は何もしない。
         * @param object 追跡を止める Entity
         */
        void Untrack(NorvesLib::Core::Entity* object);

        /**
         * @brief 追跡リソースを確立された順序で解放する
         *
         * BeforeEntities を後入れ先出しで実行する。
         * 1) 追跡 Entity を World から除去（root は RemoveObject、child は RemoveEntity）
         * AfterEntities を後入れ先出しで実行する。
         * 2) 全 MeshDataHandle を MeshResources::Unregister で解放
         * 3) 全 ModelHandle を MegaGeometryResources::ReleaseModel で解放
         * 4) 全 MaterialHandle を MaterialResources::Release で解放（材質の VT も返す）
         * 5) 追跡したテクスチャを TextureResources::ReleaseTexture で解放
         *
         * m_pWorld / m_pRenderResources が nullptr の場合はその処理をスキップ（冪等）。
         * 再入時は何もせず戻る。呼び出しの開始時に非同期の呼び戻しの世代を進める。
         */
        void Cleanup();

        /**
         * @brief 追跡リストがすべて空かどうかを返す
         *
         * @return 全追跡配列と後始末の登録が空の場合 true
         */
        bool IsEmpty() const;

    private:
        struct AsyncTextureLifetime
        {
            GameModeScope* Scope = nullptr;
            Rendering::RenderResources* RenderResources = nullptr;
            uint64_t Generation = 1;
        };

        Container::TSharedPtr<AsyncTextureLifetime> m_AsyncTextureLifetime;
        Container::VariableArray<Delegate<void>> m_BeforeEntities;
        Container::VariableArray<Delegate<void>> m_AfterEntities;
        bool m_bCleaningUp = false;

#if NORVES_ENABLE_STATS
        struct LeakCounter
        {
            Container::String Name;
            Delegate<uint64_t> Counter;
            uint64_t Baseline = 0;
        };

        GameModeScopeCounts m_BaselineCounts;
        Container::VariableArray<LeakCounter> m_LeakCounters;
#endif

        /// 非所有（World が所有）
        NorvesLib::Core::World* m_pWorld = nullptr;
        /// 非所有
        NorvesLib::Core::Rendering::RenderResources* m_pRenderResources = nullptr;

        NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Entity*>
            m_TrackedObjects;
        NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Rendering::MeshDataHandle>
            m_TrackedMeshes;
        NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Rendering::ModelHandle>
            m_TrackedModels;
        NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Rendering::MaterialHandle>
            m_TrackedMaterials;
        NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Rendering::TextureHandle>
            m_TrackedTextures;
    };

} // namespace NorvesLib::Core::GameMode

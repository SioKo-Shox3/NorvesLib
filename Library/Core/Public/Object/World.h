#pragma once

#include "Object.h"
#include "Reflection.h"
#include "Container/Containers.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    class SceneView;
}

namespace NorvesLib::Core
{
    class WorldObject;

    /**
     * @brief ゲームワールド
     *
     * Objectを継承し、WorldObjectをInner（子オブジェクト）として管理します。
     * WorldObjectはOuter（親）をたどることでWorldを取得できます。
     *
     * 責務:
     * - WorldObjectのライフサイクル管理（Inner/Outerで親子付け）
     * - 毎フレームのTick更新
     * - MeshComponentからSceneViewへのProxy同期
     *
     * Worldが破棄されると、Inner全てが連鎖破棄されます。
     */
    class World : public Object
    {
        REFLECTION_CLASS(World, Object)

    public:
        World();
        explicit World(const FieldInitializer *initializer);
        explicit World(const IUnknown *sourceObject);
        virtual ~World();

        virtual void Initialize() override;
        virtual void Finalize() override;

        /**
         * @brief WorldObjectをInnerとして追加
         * @param object 追加するWorldObject（OuterがこのWorldに設定されます）
         */
        void AddObject(WorldObject *object);

        /**
         * @brief WorldObjectをInnerから除去
         * @param object 除去するWorldObject
         */
        void RemoveObject(WorldObject *object);

        /**
         * @brief 管理下の全WorldObjectを取得（Innersからフィルタリング）
         */
        Container::VariableArray<WorldObject *> GetWorldObjects() const;

        /**
         * @brief WorldObject数を取得
         */
        size_t GetObjectCount() const;

        /**
         * @brief 全オブジェクトのTick更新
         * @param deltaTime 前フレームからの経過時間（秒）
         */
        void Tick(float deltaTime);

        /**
         * @brief 描画先SceneViewを設定
         * @param sceneView MeshProxyの送信先
         */
        void SetSceneView(Rendering::SceneView *sceneView);

        /**
         * @brief SceneViewを取得
         */
        Rendering::SceneView *GetSceneView() const { return m_SceneView; }

        /**
         * @brief MeshComponentからSceneViewへProxy同期
         */
        void SyncToSceneView();

    private:
        void CleanupDestroyedObjects();

        // システムポインタ（リフレクション対象外）
        Rendering::SceneView *m_SceneView = nullptr;

        // リフレクションプロパティ
        PROPERTY(uint64_t, NextObjectId)
    };

} // namespace NorvesLib::Core

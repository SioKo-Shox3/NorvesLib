// Bridge SDK に依存せず、シーン文書と実行中オブジェクトのプロパティを操作する。
#pragma once

#include "Container/Containers.h"

#include <cstdint>

namespace NorvesLib::Core
{
    class World;
    class Entity;
    class Object;

    namespace Component
    {
        class Component;
    }

    namespace Scene
    {
        struct SceneSaveStats;
        struct SceneLoadStats;
    }
}

namespace Game::Bridge
{
    namespace Container = NorvesLib::Core::Container;

    struct BridgeSceneResult
    {
        bool Success = false;
        Container::String Code;
        Container::String Message;
    };

    struct BridgeSceneProperty
    {
        Container::String Name;
        uint64_t Type = 0;
        // PropertyValue::DeserializeStable が読む内部表記を保持する。
        Container::String SerializedValue;
        bool bReadOnly = false;
    };

    /// GameThread 専用。各操作を同時に呼び出さない。
    class BridgeSceneDocumentService
    {
    public:
        /// UTF-8 の資産ルートを解決して保持する。
        explicit BridgeSceneDocumentService(const Container::String& assetRoot);

        /// World を資産ルート内のシーンファイルに保存する。
        /// 失敗コード: invalid_path / save_failed。
        BridgeSceneResult SaveScene(const NorvesLib::Core::World& world, const Container::String& logicalPath,
            NorvesLib::Core::Scene::SceneSaveStats* pOutStats = nullptr) const;
        /// 既存の World へ加算する。失敗時も生成に成功したルートは World に残る。
        BridgeSceneResult LoadScene(NorvesLib::Core::World& world, const Container::String& logicalPath,
            NorvesLib::Core::Scene::SceneLoadStats* pOutStats = nullptr,
            Container::VariableArray<NorvesLib::Core::Entity*>* pOutRoots = nullptr) const;

        /// 指定クラスの Entity を World に生成し、指定があれば親に所属させる。
        /// 失敗コード: unknown_class / invalid_class / invalid_target / spawn_failed。
        static BridgeSceneResult CreateEntity(NorvesLib::Core::World& world, const Container::String& className,
            NorvesLib::Core::Entity* parent, NorvesLib::Core::Entity*& outEntity);
        /// 指定クラスの Component を、同じ World に属する owner に追加する。
        /// 失敗コード: unknown_class / invalid_class / invalid_target / spawn_failed。
        static BridgeSceneResult AddComponent(NorvesLib::Core::World& world, NorvesLib::Core::Entity& owner,
            const Container::String& className, NorvesLib::Core::Component::Component*& outComponent);
        /// スキーマ投影に名前と有効な ID があるプロパティの値だけを一覧にする。
        /// クラスを取得できない場合は invalid_target を返す。
        static BridgeSceneResult ListProperties(const NorvesLib::Core::Object& target,
            Container::VariableArray<BridgeSceneProperty>& outProperties);
        /// 一覧に出るプロパティから名前が一致する値を取得する。
        /// 失敗コード: invalid_target / unknown_property。
        static BridgeSceneResult FindProperty(const NorvesLib::Core::Object& target, const Container::String& name,
            BridgeSceneProperty& outProperty);
        /// serializedValue の内部表記を適用し、Component なら OnPropertyApplied を呼び、要求時に値を読み直す。
        /// 失敗コード: invalid_target / unknown_property / read_only / invalid_value / apply_failed。
        static BridgeSceneResult SetProperty(NorvesLib::Core::Object& target, const Container::String& name,
            const Container::String& serializedValue, BridgeSceneProperty* pOutApplied = nullptr);

    private:
        Container::WideString m_RootPath;
    };
} // namespace Game::Bridge

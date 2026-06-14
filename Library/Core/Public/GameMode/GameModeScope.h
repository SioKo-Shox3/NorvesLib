#pragma once

#include "Container/Containers.h"
#include "Rendering/RenderTypes.h"

// 重いサブシステムヘッダのインクルードを避け、前方宣言のみ使用する。
// RenderResources の具体的な API（Meshes()/MegaGeometry()）は
// GameModeScope.cpp でのみ必要なため、ここでは前方宣言で十分。
namespace NorvesLib::Core
{
    class World;
    class WorldObject;

    namespace Rendering
    {
        class RenderResources;
    }
}

namespace NorvesLib::Core::GameMode
{

    /**
     * @brief ゲームモードスコープ
     *
     * 1つのゲームモードが Enter〜Leave の間に生成したリソースを追跡し、
     * Leave 時に確立された順序で安全にクリーンアップするためのヘルパー。
     *
     * クリーンアップ順序（Phase 1 で確立）:
     *   1) World::RemoveObject      — SceneView プロキシを除去
     *   2) MeshResources::Unregister — GPU メッシュデータを解放
     *   3) MegaGeometryResources::ReleaseModel — MegaGeometry モデルを解放
     *
     * 非コピー設計（二重 Cleanup 防止）。
     *
     * TODO(Phase4): SpawnObject<T>() の追加（World::SpawnObject のラッパー + TrackObject 自動呼び出し）
     * TODO: TrackAsync(...) の追加（非同期ロードリソースの追跡）
     */
    class GameModeScope
    {
    public:
        GameModeScope() = default;
        GameModeScope(
            NorvesLib::Core::World* world,
            NorvesLib::Core::Rendering::RenderResources* renderResources);

        // 非コピー（二重 Cleanup 防止）
        GameModeScope(const GameModeScope&) = delete;
        GameModeScope& operator=(const GameModeScope&) = delete;

        /**
         * @brief WorldObject を追跡リストへ追加
         *
         * Cleanup() が呼ばれると挿入順に World::RemoveObject が呼ばれる。
         * @param object 追跡対象の WorldObject（非所有、World が所有）
         */
        void TrackObject(NorvesLib::Core::WorldObject* object);

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
         * @brief 追跡リソースを確立された順序で解放する
         *
         * 1) 全 WorldObject を World::RemoveObject で除去
         * 2) 全 MeshDataHandle を MeshResources::Unregister で解放
         * 3) 全 ModelHandle を MegaGeometryResources::ReleaseModel で解放
         *
         * m_pWorld / m_pRenderResources が nullptr の場合はその処理をスキップ（冪等）。
         */
        void Cleanup();

        /**
         * @brief 追跡リストがすべて空かどうかを返す
         *
         * @return 全追跡配列が空の場合 true
         */
        bool IsEmpty() const;

    private:
        /// 非所有（World が所有）
        NorvesLib::Core::World* m_pWorld = nullptr;
        /// 非所有
        NorvesLib::Core::Rendering::RenderResources* m_pRenderResources = nullptr;

        NorvesLib::Core::Container::VariableArray<NorvesLib::Core::WorldObject*>
            m_TrackedObjects;
        NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Rendering::MeshDataHandle>
            m_TrackedMeshes;
        NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Rendering::ModelHandle>
            m_TrackedModels;
    };

} // namespace NorvesLib::Core::GameMode

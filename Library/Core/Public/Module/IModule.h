#pragma once

#include "Module/ModuleExport.h"
#include "CoreTypes.h"

#include <cstdint>

// Engine は前方宣言のみ(枠組みは Engine 実体に依存しない)。
// 実体は NorvesLib::Core::Engine::Engine(Public/Engine/Engine.h)。
namespace NorvesLib::Core::Engine
{
    class Engine;
} // namespace NorvesLib::Core::Engine

namespace NorvesLib::Core::Module
{
    class ModuleRegistry;

    /**
     * @brief モジュール寿命フェーズ
     *
     * ModuleRegistry がこの順に前進し逆順で後退する。二重遷移は冪等ガードする。
     * 値は安定(将来 DLL 記述子テーブルが参照しうるので追記のみ・既存値固定)。
     */
    enum class EModulePhase : uint8_t
    {
        Created = 0,
        Installed,
        Initialized,
        Running,
        ShuttingDown,
        Uninstalled,
    };

    /**
     * @brief 全モジュール共通の基底インターフェース
     *
     * 描画参加は本 IF に畳み込まず IRenderModule へ分離する(Audio 等の描画を
     * 持たないサービス型モジュールへの過適合を避けるため)。寿命は GameThread の
     * 単一スレッドで進み、呼び出し順序は ModuleRegistry のみが保証する。
     */
    class NORVES_MODULE_API IModule
    {
    public:
        virtual ~IModule() = default;

        // --- 識別 ---
        // ログ/重複検出に使う。ClassId/Reflection には依存しない。
        virtual Identity GetModuleId() const = 0;
        virtual const char *GetName() const = 0;

        // --- 寿命(GameThread 単一スレッド。Registry のみが呼び順序を保証) ---
        // 全モジュール登録直後に呼ばれる。RenderThread は起動済み。
        virtual bool Install(Engine::Engine &engine) = 0;
        // 全 Install 完了後に呼ばれる。重い初期化はここで行う。失敗時は false。
        virtual bool Initialize() = 0;
        // 毎フレーム GameThread で呼ばれる。snapshot 主義(ライブ状態を直接読まない)。
        virtual void Tick(float deltaTime) {}
        // Initialize の逆。RenderThread 静止後に駆動される(寿命順序は Registry が保証)。
        virtual void Shutdown() = 0;
        // Install の逆。最小実装では空で良い。
        virtual void Uninstall(Engine::Engine &engine) {}

        // --- リフレクション登録フック席(署名のみ先行予約) ---
        // 現状は非リフレクション型モジュール限定のため既定空実装。
        // Audio 等リフレクション型が来た時点で ClassRegistry::Unregister と
        // module-scoped ClassId 範囲予約が前提条件になる(計画 Risks 表参照)。
        virtual void RegisterReflectedTypes() {}   // Install 後に Registry が呼ぶ予約点
        virtual void UnregisterReflectedTypes() {} // Shutdown 前に Registry が呼ぶ予約点

        EModulePhase GetPhase() const { return m_Phase; }

    protected:
        // m_Phase は ModuleRegistry のみが遷移させる。
        friend class ModuleRegistry;
        EModulePhase m_Phase = EModulePhase::Created;
    };
} // namespace NorvesLib::Core::Module

#pragma once

#include "Module/ModuleExport.h"
#include "Module/IModule.h"
#include "Module/IRenderModule.h"
#include "CoreTypes.h"

namespace NorvesLib::Core::Module
{
    /**
     * @brief 登録順=実行順の最小モジュールレジストリ
     *
     * 依存解決グラフは持たず、登録順に Install→Initialize→Tick を進め、逆順に
     * Shutdown→Uninstall する。所有は m_Modules(TUniquePtr 配列)が持ち、描画
     * モジュールは非所有ビュー m_RenderModules に、Identity 索引は m_ById に持つ。
     */
    class NORVES_MODULE_API ModuleRegistry
    {
    public:
        ModuleRegistry() = default;
        ~ModuleRegistry() = default;

        ModuleRegistry(const ModuleRegistry &) = delete;
        ModuleRegistry &operator=(const ModuleRegistry &) = delete;

        /**
         * @brief モジュールを所有移譲しつつ登録する
         * @return 借用ポインタ(同名 Identity 重複時は既存登録を返し新規は破棄)
         *
         * [ABI 注記] TUniquePtr 値渡しは静的リンク専用。別 static lib(NorvesModule_*)が
         * Core の本 API を呼ぶ間は同一 CRT/アロケータ共有のため安全。DLL 化時はこの
         * シグネチャを C-ABI(IModule* + 所有規約: Core 側アロケータで保持/破棄)へ
         * 差し替える差替点。戻り IModule* は ModuleRegistry 生存かつ当該モジュール
         * Uninstall までのみ有効な借用ポインタ。長期保持せず必要時 FindModule(id) で
         * 再取得すること。
         */
        IModule *Register(Container::TUniquePtr<IModule> module);

        /**
         * @brief 登録順に Install→RegisterReflectedTypes→Initialize と進める
         * @return 全成功で true。途中失敗時は到達フェーズに応じ既進行分を逆順に
         *         Shutdown/Uninstall でロールバックし false を返す。
         */
        bool InstallAll(Engine::Engine &engine);

        /**
         * @brief Running 中のみ・登録順に Tick する
         */
        void TickAll(float deltaTime);

        /**
         * @brief 逆順に UnregisterReflectedTypes→Shutdown→Uninstall する(冪等)
         *
         * RenderThread 静止後・device 生存中に呼ばれる前提。二重呼び出し安全。
         */
        void ShutdownAll(Engine::Engine &engine);

        /**
         * @brief 描画モジュールの overlay パス集合の元になる非所有ビューを返す
         *
         * [ABI 注記] Container::Span 返しは静的専用。DLL 化時は
         * (IRenderModule** out, size_t* n) の C-ABI 風 out へ差し替える。複数前提で
         * 最初から配列ビュー返しにしている。
         */
        Container::Span<IRenderModule *> GetRenderModules();

        /**
         * @brief Identity でモジュールを検索する(借用ポインタ・未登録時 nullptr)
         */
        IModule *FindModule(const Identity &id) const;

        // 将来 DLL 対応の予約点(未実装・署名のみ予約):
        //   bool Unregister(const Identity &id);

    private:
        // ロールバック共通処理(到達フェーズに応じ逆順で後退させる)。
        void RollbackInstalled(Engine::Engine &engine, size_t count);

        Container::VariableArray<Container::TUniquePtr<IModule>> m_Modules; // 登録順=所有
        Container::VariableArray<IRenderModule *> m_RenderModules;          // 非所有ビュー
        Container::UnorderedMap<Identity, IModule *, Identity::Hasher> m_ById;
        EModulePhase m_Phase = EModulePhase::Created;
    };

    /**
     * @brief プロセス唯一の ModuleRegistry を返す Meyers アクセサ
     *
     * ClassRegistry::Get() と同型。Engine メンバにはしない(Engine がモジュールに
     * 依存しない単一 lib 設計を堅持するため)。読み取り中心の Core 在駐レジストリの
     * 例外カテゴリ(ClassRegistry/AssetRegistry/ResourceRegistry と同格)。別
     * static lib(NorvesModule_*)からも Core ヘッダ越しに同一実体へ到達する。
     */
    NORVES_MODULE_API ModuleRegistry &GetModuleRegistry();
} // namespace NorvesLib::Core::Module

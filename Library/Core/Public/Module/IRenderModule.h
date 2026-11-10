#pragma once

#include "Module/ModuleExport.h"

// Rendering::IViewPass は前方宣言で足りる(本ヘッダはポインタ型しか扱わないため
// IViewPass.h の include は不要・ヘッダ依存を最小化)。実体は
// NorvesLib::Core::Rendering::IViewPass(Public/Rendering/IViewPass.h)。
namespace NorvesLib::Core::Rendering
{
    class IViewPass;
    class RenderExtensionRegistry;
} // namespace NorvesLib::Core::Rendering

namespace NorvesLib::Core::Module
{
    /**
     * @brief 描画参加するモジュール用の小インターフェース
     *
     * 描画に参加するモジュール(例: オーバーレイUI)はこれを追加実装する。描画を持たない
     * サービス型モジュール(例: Audio)は実装しない = 無コスト。IModule とは
     * 直交し、Registry は dynamic_cast 相当の手段で描画モジュールを収集する。
     */
    class NORVES_MODULE_API IRenderModule
    {
    public:
        virtual ~IRenderModule() = default;

        /**
         * @brief モジュールのインストールの後、最初のフレームの前に 1 回呼ばれ、描画拡張を登録する。
         *
         * 名前は Ext.<Game>.<Name> とする。ファクトリの context はモジュールの Shutdown まで
         * 有効に保つ。Shutdown の前に Unregister し、解除の反映まで context を有効に保つ責務は
         * モジュールにある。登録表は主の SceneView が所有し、ここでは借用する。
         */
        virtual void RegisterRenderExtensions(Rendering::RenderExtensionRegistry& registry)
        {
            (void)registry;
        }

        /**
         * @brief overlay として描画するパスを返す(借用ポインタ)
         *
         * 返すパスの寿命はモジュールが所有し、モジュール Shutdown まで有効。
         * null を返してよい(その場合は描画に参加しない)。返したパスの
         * IViewPass::Initialize/Shutdown を駆動する責務はモジュール側にある。
         */
        virtual Rendering::IViewPass *GetOverlayPass() = 0;

        /**
         * @brief 撮影（--capture-png）の最終出力へ overlay を載せるか
         *
         * 既定は載せない（デバッグ用の窓が撮影に写り込まないようにする）。ランチャーのように、
         * overlay そのものが画面の本体であるときだけ true を返す。
         */
        virtual bool WantsOverlayInCapture() const
        {
            return false;
        }
    };
} // namespace NorvesLib::Core::Module

#pragma once

#include "Module/ModuleExport.h"

// Rendering::IViewPass は前方宣言で足りる(本ヘッダはポインタ型しか扱わないため
// IViewPass.h の include は不要・ヘッダ依存を最小化)。実体は
// NorvesLib::Core::Rendering::IViewPass(Public/Rendering/IViewPass.h)。
namespace NorvesLib::Core::Rendering
{
    class IViewPass;
} // namespace NorvesLib::Core::Rendering

namespace NorvesLib::Core::Module
{
    /**
     * @brief 描画参加するモジュール用の小インターフェース
     *
     * 描画に参加するモジュール(例: ImGui)はこれを追加実装する。描画を持たない
     * サービス型モジュール(例: Audio)は実装しない = 無コスト。IModule とは
     * 直交し、Registry は dynamic_cast 相当の手段で描画モジュールを収集する。
     */
    class NORVES_MODULE_API IRenderModule
    {
    public:
        virtual ~IRenderModule() = default;

        /**
         * @brief overlay として描画するパスを返す(借用ポインタ)
         *
         * 返すパスの寿命はモジュールが所有し、モジュール Shutdown まで有効。
         * null を返してよい(その場合は描画に参加しない)。返したパスの
         * IViewPass::Initialize/Shutdown を駆動する責務はモジュール側にある。
         */
        virtual Rendering::IViewPass *GetOverlayPass() = 0;
    };
} // namespace NorvesLib::Core::Module

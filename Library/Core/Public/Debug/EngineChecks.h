#pragma once

/**
 * @file EngineChecks.h
 * @brief 開発ビルドだけの検査の結果を、撮影・周回のスクリプトが照合できる形でログへ出す窓口
 *
 * 検査は起動引数 --engine-checks=<名前,...> で有効にした名前だけが結果を出す。名前は大文字・数字・下線
 * （例: WORLD_CLEANUP）。検査の側は、有効なときだけ重い照合を走らせ、結果を Report で渡す。
 *   ENGINE_CHECK name=<名前> result=pass|fail count=<照合した件数>
 * スクリプト（CaptureScene.ps1・CaptureStartupScene.ps1 の -RequireChecks、RunSceneCycleSmoke.ps1）が
 * Game.log からこの行を読み、pass が 1 回以上あり fail が 0 であることを確かめる。
 *
 * 状態は GEngine のメンバとして持つ（Engine::GetEngineChecks）。GameThread だけが読み書きする。
 * 開発ビルド以外（NORVES_BUILD_DEVELOPMENT=0）では何もせず、常に無効として振る舞う。
 */

#include "Container/Containers.h"
#include "Debug/DebugConfig.h"

#include <cstdint>

namespace NorvesLib::Debug
{
    class EngineChecks
    {
    public:
        /// 配線の確認用の検査の名前。--engine-checks=ENGINE_CHECK_SELFTEST で有効にし、Game が起動の直後に 1 回結果を出す。
        static constexpr const char *kSelfTestName = "ENGINE_CHECK_SELFTEST";

        /// 名前が検査の名前として正しいか（先頭は大文字、続きは大文字・数字・下線。長さ 64 以下）
        static bool IsValidName(const char *name);

        /// 検査を 1 つ有効にする。名前が不正、または開発ビルドでないときは false。同じ名前の重複は無視して true。
        bool Enable(const char *name);

        /// カンマ区切りの名前を全部有効にする。1 つでも不正なら false（有効にできた分は残る）。
        bool EnableList(const char *list);

        [[nodiscard]] bool IsEnabled(const char *name) const;

        /// 結果を 1 件出す。有効にしていない検査は何もしない。count は照合した件数。fail のときはエラーとして出す。
        void Report(const char *name, bool bPass, uint32_t count = 1);

        /// これまでに出した fail の数
        [[nodiscard]] uint32_t GetFailCount() const
        {
            return m_FailCount;
        }

    private:
        NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Container::String> m_Enabled;
        uint32_t m_FailCount = 0;
    };

} // namespace NorvesLib::Debug

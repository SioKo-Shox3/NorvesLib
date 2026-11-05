// シ者の表現を確かめる場面の一覧と、場面に渡す共通の文脈。
#pragma once

#include "Core/Public/GameMode/GameModeContext.h"

#include <cstddef>

namespace Game::GameModes
{
    struct ShishaShowcaseData;
    class ShishaShowcaseCamera;

    /// 呼び出しごとに作る借用の文脈。保持する状態は Data の置き場に入れる。
    struct ShishaShowcaseSceneContext
    {
        NorvesLib::Core::GameMode::GameModeContext& Mode;
        ShishaShowcaseData& Data;
        ShishaShowcaseCamera& Camera;
        float Scale;
        float DeltaTime;
        double ElapsedSeconds;
    };

    struct ShishaShowcaseSceneEntry
    {
        const char* Name;
        bool (*Build)(ShishaShowcaseSceneContext&);
        /// 取り付けが済んだら true。nullptr なら組み立て直後から準備済み。
        bool (*Update)(ShishaShowcaseSceneContext&) = nullptr;
        void (*Leave)(ShishaShowcaseSceneContext&) = nullptr;
    };

    /// 大文字小文字を区別する完全一致。未知の名前と nullptr は nullptr を返す。
    const ShishaShowcaseSceneEntry* FindShishaShowcaseScene(const char* name);
    size_t GetShishaShowcaseSceneCount();
    /// 範囲外は nullptr を返す。
    const ShishaShowcaseSceneEntry* GetShishaShowcaseScene(size_t index);
} // namespace Game::GameModes

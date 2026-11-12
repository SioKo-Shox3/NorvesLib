// 場面の一覧から名前空間の範囲の入口宣言と対応表を作り、列挙と完全一致の検索を提供する。
#include "GameModes/ShishaShowcase/ShishaShowcaseScenes.h"

#include <cstring>

namespace Game::GameModes
{
    // 場面を足すときはこの一覧に 1 行だけ足す（名前, 入口の関数の接尾辞）。
    // 場面のファイルでは Build/Update/Leave + 接尾辞の 3 関数を Game::GameModes に定義する。
#define SHISHA_SHOWCASE_SCENES(X) \
    X("base", ShishaShowcaseBaseScene) \
    X("pale", ShishaShowcasePaleScene)

#define SHISHA_SHOWCASE_DECLARE(name, suffix) \
    bool Build##suffix(ShishaShowcaseSceneContext&); \
    bool Update##suffix(ShishaShowcaseSceneContext&); \
    void Leave##suffix(ShishaShowcaseSceneContext&);

    SHISHA_SHOWCASE_SCENES(SHISHA_SHOWCASE_DECLARE)

#undef SHISHA_SHOWCASE_DECLARE

#define SHISHA_SHOWCASE_ENTRY(name, suffix) \
    {name, Build##suffix, Update##suffix, Leave##suffix},

    static const ShishaShowcaseSceneEntry kScenes[] = {
        SHISHA_SHOWCASE_SCENES(SHISHA_SHOWCASE_ENTRY)
    };

#undef SHISHA_SHOWCASE_ENTRY
#undef SHISHA_SHOWCASE_SCENES

    const ShishaShowcaseSceneEntry* FindShishaShowcaseScene(const char* name)
    {
        if (name != nullptr)
        {
            for (const auto& scene : kScenes)
            {
                if (std::strcmp(scene.Name, name) == 0)
                {
                    return &scene;
                }
            }
        }
        return nullptr;
    }

    size_t GetShishaShowcaseSceneCount()
    {
        return sizeof(kScenes) / sizeof(kScenes[0]);
    }

    const ShishaShowcaseSceneEntry* GetShishaShowcaseScene(size_t index)
    {
        return index < GetShishaShowcaseSceneCount() ? &kScenes[index] : nullptr;
    }
} // namespace Game::GameModes

/**
 * @file DecalPoolEvictionTest.cpp
 * @brief デカールの追加順の追い出し、空き枠の再利用、寿命と薄れの CPU 試験
 */

#include "Particle/DecalPool.h"

#include <cassert>
#include <iostream>

using namespace NorvesLib::Core::Particle;

namespace
{
    DecalDesc MakeDecal(float positionX)
    {
        DecalDesc desc;
        desc.Instance.Position[0] = positionX;
        return desc;
    }
} // namespace

int main()
{
    // 上限を超えた追加は最初の 2 件を消し、残る一覧も追加順を保つ。
    {
        DecalPool pool(4u);
        DecalHandle handles[6];
        for (uint32_t index = 0u; index < 6u; ++index)
        {
            handles[index] = pool.Add(MakeDecal(static_cast<float>(index)));
        }
        assert(!pool.IsAlive(handles[0]));
        assert(!pool.IsAlive(handles[1]));
        for (uint32_t index = 2u; index < 6u; ++index)
        {
            assert(pool.IsAlive(handles[index]));
        }
        assert(pool.GetCount() == 4u);
        const auto scene = pool.BuildScene();
        assert(!scene.IsEmpty());
        assert(scene.Decals->size() == 4u);
        for (uint32_t index = 0u; index < 4u; ++index)
        {
            assert((*scene.Decals)[index].Position[0] == static_cast<float>(index + 2u));
        }
    }

    // 空いた枠への追加は追い出さず、その次の追加では残る最古だけを消す。
    {
        DecalPool pool(4u);
        DecalHandle handles[4];
        for (uint32_t index = 0u; index < 4u; ++index)
        {
            handles[index] = pool.Add(MakeDecal(static_cast<float>(index)));
        }
        const bool bRemoved = pool.Remove(handles[1]);
        assert(bRemoved);
        const DecalHandle reused = pool.Add(MakeDecal(4.0f));
        assert(!pool.IsAlive(handles[1]));
        assert(pool.IsAlive(handles[0]));
        assert(pool.IsAlive(handles[2]));
        assert(pool.IsAlive(handles[3]));
        assert(pool.IsAlive(reused));
        assert(pool.GetCount() == 4u);

        const DecalHandle newest = pool.Add(MakeDecal(5.0f));
        assert(!pool.IsAlive(handles[0]));
        assert(pool.IsAlive(handles[2]));
        assert(pool.IsAlive(handles[3]));
        assert(pool.IsAlive(reused));
        assert(pool.IsAlive(newest));
        assert(pool.GetCount() == 4u);
    }

    // 寿命の終わりは描画値が薄れ、寿命を過ぎるとハンドルが無効になる。
    {
        DecalPool pool;
        DecalDesc desc = MakeDecal(0.0f);
        desc.LifetimeSeconds = 2.0f;
        desc.FadeOutSeconds = 1.0f;
        const DecalHandle handle = pool.Add(desc);
        pool.Tick(1.5f);
        assert(pool.IsAlive(handle));
        const auto scene = pool.BuildScene();
        assert(!scene.IsEmpty());
        assert((*scene.Decals)[0].Color[3] < 1.0f);
        assert((*scene.Decals)[0].Color[3] >= 0.0f);
        pool.Tick(1.0f);
        assert(!pool.IsAlive(handle));
        assert(pool.GetCount() == 0u);
        assert(pool.BuildScene().IsEmpty());
    }

    std::cout << "DecalPoolEvictionTest: 検査に合格しました\n";
    return 0;
}

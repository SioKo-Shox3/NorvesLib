#include "Rendering/FramePacket.h"
#include "Rendering/RenderExtension/RenderExtensionData.h"
#include "Rendering/ViewRenderContext.h"
#include <cstdint>
#include <cstring>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Rendering;

namespace
{
    struct Params
    {
        float Color[4];
        uint32_t Mode;
    };

    struct OtherParams
    {
        uint32_t Mode;
        float Color[4];
    };

    constexpr RenderExtensionDataKey<Params> kValueKey{
        Identity::Literal("Test.Extension.Value", sizeof("Test.Extension.Value") - 1)};
    constexpr RenderExtensionDataKey<Params> kArrayKey{
        Identity::Literal("Test.Extension.Array", sizeof("Test.Extension.Array") - 1)};
    constexpr RenderExtensionDataKey<Params> kRetainedKey{
        Identity::Literal("Test.Extension.Retained", sizeof("Test.Extension.Retained") - 1)};
    constexpr RenderExtensionDataKey<OtherParams> kOtherValueKey{
        Identity::Literal("Test.Extension.Value", sizeof("Test.Extension.Value") - 1)};
    constexpr RenderExtensionDataKey<OtherParams> kOtherArrayKey{
        Identity::Literal("Test.Extension.Array", sizeof("Test.Extension.Array") - 1)};

    static_assert(sizeof(Params) == sizeof(OtherParams));
    static_assert(alignof(Params) == alignof(OtherParams));
    static_assert(ComputeRenderExtensionLayoutHash<Params>() != ComputeRenderExtensionLayoutHash<OtherParams>());

    int g_Failures = 0;
    const char* g_TestName = nullptr;

    bool Expect(bool bCondition, const char* reason)
    {
        if (!bCondition)
        {
            std::cerr << g_TestName << ": " << reason << '\n';
            ++g_Failures;
        }
        return bCondition;
    }

    template <typename T>
    void SetValues(RenderExtensionDataStore& store, const RenderExtensionDataKey<T>& key,
                   const T* values, uint32_t count)
    {
        store.SetBytes(key.Name, ComputeRenderExtensionLayoutHash<T>(), static_cast<uint32_t>(sizeof(T)), count, values);
    }

    bool SameParams(const Params& left, const Params& right)
    {
        for (uint32_t index = 0; index < 4; ++index)
        {
            if (left.Color[index] != right.Color[index])
            {
                return false;
            }
        }
        return left.Mode == right.Mode;
    }

    void TestSnapshotIgnoresLaterChanges()
    {
        const Params expected{{1.0f, 2.0f, 3.0f, 4.0f}, 5u};
        const Params expectedArray[] = {
            {{6.0f, 7.0f, 8.0f, 9.0f}, 10u},
            {{11.0f, 12.0f, 13.0f, 14.0f}, 15u}};
        Params value = expected;
        Params values[] = {expectedArray[0], expectedArray[1]};
        RenderExtensionDataStore store;
        SetValues(store, kValueKey, &value, 1u);
        SetValues(store, kArrayKey, values, 2u);

        FramePacket packet;
        store.SnapshotTo(packet.Scene.Extensions);
        value = {{21.0f, 22.0f, 23.0f, 24.0f}, 25u};
        values[0] = value;
        values[1] = value;
        const Params nextValue{{31.0f, 32.0f, 33.0f, 34.0f}, 35u};
        const Params nextValues[] = {nextValue, nextValue};
        SetValues(store, kValueKey, &nextValue, 1u);
        SetValues(store, kArrayKey, nextValues, 2u);

        ViewRenderContext context;
        context.SnapshotScene = &packet.Scene;
        const Params* snapshot = context.FindExtension(kValueKey);
        Expect(snapshot != nullptr && SameParams(*snapshot, expected),
               "後の元データ変更または Set で値のスナップショットが変わった");
        const auto snapshotArray = context.FindExtensionArray(kArrayKey);
        Expect(snapshotArray.size() == 2 && SameParams(snapshotArray[0], expectedArray[0]) &&
                   SameParams(snapshotArray[1], expectedArray[1]),
               "後の元データ変更または Set で配列のスナップショットが変わった");
    }

    void TestIdenticalBytesReuseBlobAndRevision()
    {
        Params value{{1.0f, 2.0f, 3.0f, 4.0f}, 0u};
        Params identical;
        std::memcpy(&identical, &value, sizeof(identical));
        RenderExtensionDataStore store;
        RenderExtensionDataSet first;
        RenderExtensionDataSet second;
        RenderExtensionDataSet changed;

        SetValues(store, kValueKey, &value, 1u);
        store.SnapshotTo(first);
        if (!Expect(first.size() == 1 && first[0].get() != nullptr, "最初のブロブがない"))
        {
            return;
        }
        const uint64_t firstRevision = first[0]->Revision;
        Expect(store.Find(kValueKey.Name) == first[0].get(), "ストアとスナップショットのブロブが異なる");

        SetValues(store, kValueKey, &identical, 1u);
        store.SnapshotTo(second);
        if (!Expect(second.size() == 1 && second[0].get() != nullptr, "同じ値を設定後のブロブがない"))
        {
            return;
        }
        Expect(second[0].get() == first[0].get(), "同じバイトでブロブが変わった");
        Expect(second[0]->Revision == firstRevision, "同じバイトで Revision が変わった");

        const uint64_t secondRevision = second[0]->Revision;
        reinterpret_cast<unsigned char*>(&identical.Mode)[0] ^= 1u;
        SetValues(store, kValueKey, &identical, 1u);
        store.SnapshotTo(changed);
        if (!Expect(changed.size() == 1 && changed[0].get() != nullptr, "1 バイト変更後のブロブがない"))
        {
            return;
        }
        Expect(changed[0].get() != second[0].get(), "1 バイトの変更でブロブが変わらなかった");
        Expect(changed[0]->Revision != secondRevision, "1 バイトの変更で Revision が変わらなかった");
    }

    void TestLayoutMismatchReturnsEmpty()
    {
        const Params value{{1.0f, 2.0f, 3.0f, 4.0f}, 5u};
        const Params values[] = {value, {{6.0f, 7.0f, 8.0f, 9.0f}, 10u}};
        RenderExtensionDataStore store;
        SetValues(store, kValueKey, &value, 1u);
        SetValues(store, kArrayKey, values, 2u);
        FramePacket packet;
        store.SnapshotTo(packet.Scene.Extensions);
        ViewRenderContext context;
        context.SnapshotScene = &packet.Scene;

        Expect(context.FindExtension(kOtherValueKey) == nullptr, "レイアウトの違う値が返った");
        Expect(context.FindExtensionArray(kOtherArrayKey).empty(), "レイアウトの違う配列が返った");
        const Params* found = context.FindExtension(kValueKey);
        Expect(found != nullptr && SameParams(*found, value), "レイアウト不一致後に元の値を取得できない");
        const auto foundArray = context.FindExtensionArray(kArrayKey);
        Expect(foundArray.size() == 2 && SameParams(foundArray[0], values[0]) &&
                   SameParams(foundArray[1], values[1]),
               "レイアウト不一致後に元の配列を取得できない");
    }

    void TestPacketClearRemovesExtensions()
    {
        const Params value{{1.0f, 2.0f, 3.0f, 4.0f}, 5u};
        RenderExtensionDataStore store;
        SetValues(store, kValueKey, &value, 1u);
        FramePacket packet;
        store.SnapshotTo(packet.Scene.Extensions);
        ViewRenderContext context;
        context.SnapshotScene = &packet.Scene;
        Expect(context.FindExtension(kValueKey) != nullptr, "Clear 前に値を取得できない");

        packet.Clear();
        Expect(packet.Scene.Extensions.empty(), "Clear 後に拡張データが残っている");
        Expect(context.FindExtension(kValueKey) == nullptr, "Clear 後に値を取得できた");
    }

    void TestReusedPacketDropsRemovedValues()
    {
        const Params removedValue{{1.0f, 2.0f, 3.0f, 4.0f}, 5u};
        const Params retainedValue{{6.0f, 7.0f, 8.0f, 9.0f}, 10u};
        RenderExtensionDataStore store;
        SetValues(store, kValueKey, &removedValue, 1u);
        SetValues(store, kRetainedKey, &retainedValue, 1u);
        FramePacket packet;
        store.SnapshotTo(packet.Scene.Extensions);
        ViewRenderContext context;
        context.SnapshotScene = &packet.Scene;
        const Params* initialRemoved = context.FindExtension(kValueKey);
        const Params* initialRetained = context.FindExtension(kRetainedKey);
        Expect(initialRemoved != nullptr && SameParams(*initialRemoved, removedValue), "削除前の値を取得できない");
        Expect(initialRetained != nullptr && SameParams(*initialRetained, retainedValue), "残す値を取得できない");

        store.Remove(kValueKey.Name);
        store.SnapshotTo(packet.Scene.Extensions);
        Expect(context.FindExtension(kValueKey) == nullptr, "再利用したパケットに削除済みの値が残った");
        const Params* retained = context.FindExtension(kRetainedKey);
        Expect(retained != nullptr && SameParams(*retained, retainedValue), "再利用したパケットから残す値を取得できない");

        packet.Clear();
        store.Remove(kRetainedKey.Name);
        Expect(store.GetCount() == 0, "最後の値を削除してもストアが空にならない");
        store.SnapshotTo(packet.Scene.Extensions);
        Expect(packet.Scene.Extensions.empty(), "空のストアを写したパケットに拡張データが残った");
        Expect(context.FindExtension(kValueKey) == nullptr && context.FindExtension(kRetainedKey) == nullptr,
               "空のストアを写したパケットから値を取得できた");
    }

    void RunTest(const char* name, void (*test)())
    {
        g_TestName = name;
        const int before = g_Failures;
        test();
        std::cout << name << (g_Failures == before ? ": 成功\n" : ": 失敗\n");
    }
}

int main()
{
    RunTest("TestSnapshotIgnoresLaterChanges", TestSnapshotIgnoresLaterChanges);
    RunTest("TestIdenticalBytesReuseBlobAndRevision", TestIdenticalBytesReuseBlobAndRevision);
    RunTest("TestLayoutMismatchReturnsEmpty", TestLayoutMismatchReturnsEmpty);
    RunTest("TestPacketClearRemovesExtensions", TestPacketClearRemovesExtensions);
    RunTest("TestReusedPacketDropsRemovedValues", TestReusedPacketDropsRemovedValues);
    std::cout << "RenderExtensionDataTest: 失敗数=" << g_Failures << '\n';
    return g_Failures == 0 ? 0 : 1;
}

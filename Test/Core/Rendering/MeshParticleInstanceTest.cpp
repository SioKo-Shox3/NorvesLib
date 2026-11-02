// メッシュの粒子の CPU 上の記録と検証を確かめる。
#include "Math/MatrixUtils.h"
#include "Math/VectorUtils.h"
#include "Particle/VfxEffectSystem.h"
#include "Particle/VfxSchema.h"
#include "Rendering/SceneProxy.h"

#include <cmath>
#include <cstring>
#include <iostream>

using namespace NorvesLib;
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Particle;

namespace
{
    constexpr float kDt = 0.25f;
    constexpr uint32_t kColorModule = 3u;
    constexpr uint32_t kRendererModule = 5u;
    constexpr float kStart = 0.25f;
    constexpr float kDuration = 0.5f;
    constexpr float kEdgeWidth = 0.15f;
    const Math::Vector4 kEdgeColor(2.5f, 1.25f, 0.5f, 1.0f);

    bool Check(bool condition, const char* item, const char* reason)
    {
        if (!condition)
        {
            std::cerr << item << ": " << reason << '\n';
        }
        return condition;
    }

#define REQUIRE(condition, reason) \
    do \
    { \
        if (!Check((condition), item, (reason))) \
        { \
            return false; \
        } \
    } while (false)

    bool Near(float actual, float expected, float tolerance = 0.0001f)
    {
        return std::isfinite(actual) && std::isfinite(expected) && std::fabs(actual - expected) <= tolerance;
    }

    bool CheckFloat(float actual, float expected, const char* item, const char* name, float tolerance = 0.0001f)
    {
        if (!Near(actual, expected, tolerance))
        {
            std::cerr << item << ": " << name << " 実際=" << actual << " 期待=" << expected << '\n';
            return false;
        }
        return true;
    }

    bool CheckVector(const Math::Vector3& actual, const Math::Vector3& expected, const char* item, const char* name)
    {
        if (!Near(actual.x, expected.x) || !Near(actual.y, expected.y) || !Near(actual.z, expected.z))
        {
            std::cerr << item << ": " << name << " 実際=(" << actual.x << "," << actual.y << "," << actual.z
                << ") 期待=(" << expected.x << "," << expected.y << "," << expected.z << ")\n";
            return false;
        }
        return true;
    }

    bool CheckColor(const Math::Vector4& actual, const Math::Vector4& expected, const char* item, const char* name)
    {
        if (!Near(actual.x, expected.x) || !Near(actual.y, expected.y) ||
            !Near(actual.z, expected.z) || !Near(actual.w, expected.w))
        {
            std::cerr << item << ": " << name << " 実際=(" << actual.x << "," << actual.y << "," << actual.z
                << "," << actual.w << ") 期待=(" << expected.x << "," << expected.y << "," << expected.z
                << "," << expected.w << ")\n";
            return false;
        }
        return true;
    }

    bool CheckCount(size_t actual, size_t expected, const char* item, const char* name)
    {
        if (actual != expected)
        {
            std::cerr << item << ": " << name << " 実際=" << actual << " 期待=" << expected << '\n';
            return false;
        }
        return true;
    }

    float ExpectedThreshold(float age, float lifetime, float start)
    {
        const float value = (age / lifetime - start) / kDuration;
        return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    }

    bool MakeEffect(VfxEffectAsset& asset, bool burst, const Math::Vector3& velocity, bool align)
    {
        Container::String error;
        VfxEmitterAsset emitter = MakeVfxDefaultEmitter("MeshEmitter");
        VfxModule spawn = MakeVfxDefaultModule(burst ? VfxModuleType::SpawnBurst : VfxModuleType::SpawnRate);
        VfxModule initialVelocity = MakeVfxDefaultModule(VfxModuleType::InitVelocityBox);
        VfxModule lifetime = MakeVfxDefaultModule(VfxModuleType::InitLifetime);
        VfxModule color = MakeVfxDefaultModule(VfxModuleType::InitColor);
        VfxModule size = MakeVfxDefaultModule(VfxModuleType::InitSize);
        VfxModule renderer = MakeVfxDefaultModule(VfxModuleType::MeshRenderer);
        const Math::Vector3 lower(velocity.x < 0.0f ? velocity.x : 0.0f,
            velocity.y < 0.0f ? velocity.y : 0.0f, velocity.z < 0.0f ? velocity.z : 0.0f);
        if (!SetVfxEmitterProperty(emitter, "Space", VfxValue::MakeEnum(static_cast<uint32_t>(VfxSimulationSpace::World)), &error) ||
            !(burst ? SetVfxModuleParameter(spawn, "Count", VfxValue::MakeUInt(1u), &error) :
                SetVfxModuleParameter(spawn, "Rate", VfxValue::MakeFloat(8.0f), &error)) ||
            !SetVfxModuleParameter(initialVelocity, "Min", VfxValue::MakeVector3(lower), &error) ||
            !SetVfxModuleParameter(initialVelocity, "Max", VfxValue::MakeVector3(velocity), &error) ||
            !SetVfxModuleParameter(initialVelocity, "Min", VfxValue::MakeVector3(velocity), &error) ||
            !SetVfxModuleParameter(lifetime, "Lifetime", VfxValue::MakeFloat(2.0f), &error) ||
            !SetVfxModuleParameter(color, "Color", VfxValue::MakeColor(Math::Vector4(0.25f, 0.5f, 0.75f, 1.0f)), &error) ||
            !SetVfxModuleParameter(size, "Size", VfxValue::MakeVector2(Math::Vector2(2.5f, 4.0f)), &error) ||
            !SetVfxModuleParameter(renderer, "Mesh", VfxValue::MakeName("test_mesh"), &error) ||
            !SetVfxModuleParameter(renderer, "AlignToVelocity", VfxValue::MakeBool(align), &error) ||
            !SetVfxModuleParameter(renderer, "DissolveStart", VfxValue::MakeFloat(kStart), &error) ||
            !SetVfxModuleParameter(renderer, "DissolveDuration", VfxValue::MakeFloat(kDuration), &error) ||
            !SetVfxModuleParameter(renderer, "EdgeWidth", VfxValue::MakeFloat(kEdgeWidth), &error) ||
            !SetVfxModuleParameter(renderer, "EdgeColor", VfxValue::MakeColor(kEdgeColor), &error))
        {
            std::cerr << "エフェクトの構成に失敗: " << error.c_str() << '\n';
            return false;
        }
        emitter.Modules.push_back(spawn);
        emitter.Modules.push_back(initialVelocity);
        emitter.Modules.push_back(lifetime);
        emitter.Modules.push_back(color);
        emitter.Modules.push_back(size);
        emitter.Modules.push_back(renderer);
        asset.Name = "MeshParticle";
        asset.Emitters.push_back(emitter);
        return true;
    }

    VfxInstanceDesc MakeDesc()
    {
        VfxInstanceDesc desc;
        desc.Position = Math::Vector3(4.0f, -3.0f, 7.0f);
        desc.Mesh = Rendering::MeshDataHandle{101u};
        desc.Material = Rendering::MaterialHandle{202u};
        return desc;
    }

    bool CheckRecordCounts(const VfxEffectSystem& system, VfxEffectHandle first, VfxEffectHandle second,
        bool includeFirst, bool includeSecond, const char* item)
    {
        VariableArray<VfxParticleState> firstParticles;
        VariableArray<VfxParticleState> secondParticles;
        REQUIRE(!system.IsLiveInstance(first) || system.CopyParticles(first, 0u, firstParticles), "１つ目の粒を取得できない");
        REQUIRE(!system.IsLiveInstance(second) || system.CopyParticles(second, 0u, secondParticles), "２つ目の粒を取得できない");
        const size_t firstExpected = includeFirst ? firstParticles.size() : 0u;
        const size_t secondExpected = includeSecond ? secondParticles.size() : 0u;
        VfxMeshInstance sentinel;
        sentinel.Id = 123456u;
        sentinel.Mesh = Rendering::MeshDataHandle{987u};
        sentinel.EdgeWidth = 0.625f;
        VariableArray<VfxMeshInstance> records;
        records.push_back(sentinel);
        system.AppendMeshInstances(records);
        if (!CheckCount(records.size(), 1u + firstExpected + secondExpected, item, "追加後の記録数"))
        {
            return false;
        }
        REQUIRE(records[0].Id == sentinel.Id && records[0].Mesh == sentinel.Mesh &&
            records[0].EdgeWidth == sentinel.EdgeWidth, "配列の既存要素が変わった");
        size_t firstCount = 0u;
        size_t secondCount = 0u;
        for (size_t index = 1u; index < records.size(); ++index)
        {
            if (includeFirst && records[index].InstanceIndex == first.Index)
            {
                ++firstCount;
            }
            else if (includeSecond && records[index].InstanceIndex == second.Index)
            {
                ++secondCount;
            }
            else
            {
                std::cerr << item << ": 対象外のインスタンスが記録された 添字=" << records[index].InstanceIndex << '\n';
                return false;
            }
        }
        return CheckCount(firstCount, firstExpected, item, "１つ目の記録数") &&
            CheckCount(secondCount, secondExpected, item, "２つ目の記録数");
    }

    bool TestRecordCounts()
    {
        const char* item = "1. 記録の数";
        VfxEffectAsset asset;
        REQUIRE(MakeEffect(asset, false, Math::Vector3(3.0f, 4.0f, 0.0f), false), "エフェクトを作れない");
        VfxEffectSystem system;
        Container::String error;
        const auto first = system.CreateInstance(asset, MakeDesc(), &error);
        REQUIRE(first.IsValid(), "１つ目のインスタンスを作れない");
        const VfxEffectHandle invalid;
        REQUIRE(CheckRecordCounts(system, first, invalid, true, false, item), "生成前の記録数が違う");
        bool observedFirst = false;
        for (uint32_t step = 0u; step < 5u; ++step)
        {
            system.Tick(kDt);
            VariableArray<VfxParticleState> particles;
            REQUIRE(system.CopyParticles(first, 0u, particles), "１つ目の粒を取得できない");
            observedFirst |= !particles.empty();
            REQUIRE(CheckRecordCounts(system, first, invalid, true, false, item), "１つ目の記録数が違う");
        }
        const auto second = system.CreateInstance(asset, MakeDesc(), &error);
        REQUIRE(second.IsValid(), "２つ目のインスタンスを作れない");
        bool observedSecond = false;
        for (uint32_t step = 0u; step < 5u; ++step)
        {
            system.Tick(kDt);
            VariableArray<VfxParticleState> particles;
            REQUIRE(system.CopyParticles(second, 0u, particles), "２つ目の粒を取得できない");
            observedSecond |= !particles.empty();
            REQUIRE(CheckRecordCounts(system, first, second, true, true, item), "２つの合計が違う");
        }
        REQUIRE(observedFirst && observedSecond, "粒が生成されていない");
        REQUIRE(system.SetInstanceEnabled(first, false), "１つ目を無効にできない");
        REQUIRE(CheckRecordCounts(system, first, second, false, true, item), "無効なインスタンスが記録された");
        REQUIRE(system.DestroyInstance(second), "２つ目を破棄できない");
        REQUIRE(CheckRecordCounts(system, first, second, false, false, item), "破棄したインスタンスが記録された");
        REQUIRE(system.SetInstanceEnabled(first, true), "１つ目を再び有効にできない");
        REQUIRE(CheckRecordCounts(system, first, second, true, false, item), "再び有効にした記録数が違う");
        REQUIRE(system.DestroyInstance(first), "１つ目を破棄できない");
        REQUIRE(CheckRecordCounts(system, first, second, false, false, item), "破棄後に記録が残った");
        return true;
    }

    bool TestRecordContents()
    {
        const char* item = "2. 記録の中身";
        VfxEffectAsset asset;
        REQUIRE(MakeEffect(asset, false, Math::Vector3(3.0f, 4.0f, 0.0f), false), "エフェクトを作れない");
        VfxEffectSystem system;
        const VfxInstanceDesc desc = MakeDesc();
        Container::String error;
        const auto handle = system.CreateInstance(asset, desc, &error);
        REQUIRE(handle.IsValid(), "インスタンスを作れない");
        for (uint32_t step = 0u; step < 5u; ++step)
        {
            system.Tick(kDt);
        }
        VariableArray<VfxParticleState> particles;
        VariableArray<VfxMeshInstance> records;
        REQUIRE(system.CopyParticles(handle, 0u, particles), "粒を取得できない");
        system.AppendMeshInstances(records);
        REQUIRE(!particles.empty(), "粒が生成されていない");
        if (!CheckCount(records.size(), particles.size(), item, "記録数"))
        {
            return false;
        }
        VariableArray<uint8_t> seen;
        seen.resize(particles.size());
        for (size_t index = 0u; index < seen.size(); ++index)
        {
            seen[index] = 0u;
        }
        for (const auto& record : records)
        {
            REQUIRE(record.InstanceIndex == handle.Index && record.EmitterIndex == 0u &&
                record.ParticleIndex < particles.size(), "記録の粒の参照が範囲外");
            REQUIRE(seen[record.ParticleIndex] == 0u, "同じ粒が重複して記録された");
            seen[record.ParticleIndex] = 1u;
            const auto& particle = particles[record.ParticleIndex];
            const auto origin = Math::MatrixUtils::TransformPointRowVector(record.World, Math::Vector3(0.0f, 0.0f, 0.0f));
            const auto x = Math::MatrixUtils::TransformVectorRowVector(record.World, Math::Vector3(1.0f, 0.0f, 0.0f));
            const auto y = Math::MatrixUtils::TransformVectorRowVector(record.World, Math::Vector3(0.0f, 1.0f, 0.0f));
            const auto z = Math::MatrixUtils::TransformVectorRowVector(record.World, Math::Vector3(0.0f, 0.0f, 1.0f));
            if (!CheckVector(origin, particle.Position, item, "位置") ||
                !CheckVector(x, Math::Vector3(particle.Size.x, 0.0f, 0.0f), item, "X 軸") ||
                !CheckVector(y, Math::Vector3(0.0f, particle.Size.x, 0.0f), item, "Y 軸") ||
                !CheckVector(z, Math::Vector3(0.0f, 0.0f, particle.Size.x), item, "Z 軸") ||
                !CheckColor(record.Color, particle.Color, item, "粒の色") ||
                !CheckFloat(record.EdgeWidth, kEdgeWidth, item, "縁の幅") ||
                !CheckColor(record.EdgeColor, kEdgeColor, item, "縁の色"))
            {
                std::cerr << item << ": 粒の添字=" << record.ParticleIndex << '\n';
                return false;
            }
            if (record.Mesh != desc.Mesh || record.Material != desc.Material)
            {
                std::cerr << item << ": ハンドル 実際=(" << record.Mesh.Id << "," << record.Material.Id
                    << ") 期待=(" << desc.Mesh.Id << "," << desc.Material.Id << ")\n";
                return false;
            }
        }
        return true;
    }

    bool TestThresholdOverTime()
    {
        const char* item = "3. 溶けの閾値と時間";
        VfxEffectAsset asset;
        REQUIRE(MakeEffect(asset, true, Math::Vector3(0.0f, 0.0f, 0.0f), false), "エフェクトを作れない");
        VfxEffectSystem system;
        Container::String error;
        const auto handle = system.CreateInstance(asset, MakeDesc(), &error);
        REQUIRE(handle.IsValid(), "インスタンスを作れない");
        bool observedZero = false;
        bool observedMiddle = false;
        bool observedOne = false;
        float previous = 0.0f;
        for (uint32_t step = 0u; step < 8u; ++step)
        {
            system.Tick(kDt);
            VariableArray<VfxParticleState> particles;
            VariableArray<VfxMeshInstance> records;
            REQUIRE(system.CopyParticles(handle, 0u, particles), "粒を取得できない");
            system.AppendMeshInstances(records);
            if (!CheckCount(particles.size(), 1u, item, "粒の数") ||
                !CheckCount(records.size(), 1u, item, "記録数"))
            {
                return false;
            }
            const float threshold = records[0].DissolveThreshold;
            const float expected = ExpectedThreshold(particles[0].Age, particles[0].Lifetime, kStart);
            if (!CheckFloat(threshold, expected, item, "閾値", 0.00001f))
            {
                std::cerr << item << ": 刻み=" << step << " 年齢=" << particles[0].Age
                    << " 寿命=" << particles[0].Lifetime << '\n';
                return false;
            }
            if (threshold < previous)
            {
                std::cerr << item << ": 閾値が減少 前=" << previous << " 現在=" << threshold << '\n';
                return false;
            }
            previous = threshold;
            observedZero |= threshold == 0.0f;
            observedMiddle |= threshold > 0.0f && threshold < 1.0f;
            observedOne |= threshold == 1.0f;
        }
        REQUIRE(observedZero && observedMiddle && observedOne, "閾値の 0・中間・1 をすべて通っていない");
        return true;
    }

    bool TestThresholdEdges()
    {
        const char* item = "4. 式の端";
        return CheckFloat(VfxEffectSystem::ComputeDissolveThreshold(0.25f, 2.0f, 0.25f, 0.0f),
                0.0f, item, "長さ 0 の開始前", 0.00001f) &&
            CheckFloat(VfxEffectSystem::ComputeDissolveThreshold(1.0f, 2.0f, 0.25f, 0.0f),
                1.0f, item, "長さ 0 の開始後", 0.00001f) &&
            CheckFloat(VfxEffectSystem::ComputeDissolveThreshold(0.0f, 0.0f, 0.25f, 0.5f),
                1.0f, item, "寿命 0", 0.00001f) &&
            CheckFloat(VfxEffectSystem::ComputeDissolveThreshold(1.0f, 2.0f, 0.25f, 0.5f),
                0.5f, item, "範囲の内側", 0.00001f);
    }

    bool TestLiveChange()
    {
        const char* item = "5. 動作中の値の変更";
        VfxEffectAsset asset;
        REQUIRE(MakeEffect(asset, true, Math::Vector3(0.0f, 0.0f, 0.0f), false), "エフェクトを作れない");
        VfxEffectSystem system;
        Container::String error;
        const auto handle = system.CreateInstance(asset, MakeDesc(), &error);
        REQUIRE(handle.IsValid(), "インスタンスを作れない");
        for (uint32_t step = 0u; step < 5u; ++step)
        {
            system.Tick(kDt);
        }
        VariableArray<VfxParticleState> particles;
        VariableArray<VfxMeshInstance> before;
        REQUIRE(system.CopyParticles(handle, 0u, particles), "粒を取得できない");
        system.AppendMeshInstances(before);
        if (!CheckCount(particles.size(), 1u, item, "粒の数") ||
            !CheckCount(before.size(), 1u, item, "変更前の記録数") ||
            !CheckFloat(before[0].DissolveThreshold, ExpectedThreshold(particles[0].Age, particles[0].Lifetime, kStart),
                item, "変更前の閾値", 0.00001f))
        {
            return false;
        }
        constexpr float changedStart = 0.375f;
        REQUIRE(system.SetModuleParameter(handle, 0u, kRendererModule, "DissolveStart",
            VfxValue::MakeFloat(changedStart), &error), "溶け始める年齢を変更できない");
        system.Tick(1.0f / 60.0f);
        REQUIRE(system.CopyParticles(handle, 0u, particles), "変更後の粒を取得できない");
        if (!CheckCount(particles.size(), 1u, item, "変更後の粒の数"))
        {
            return false;
        }
        const float newThreshold = ExpectedThreshold(particles[0].Age, particles[0].Lifetime, changedStart);
        const float oldThreshold = ExpectedThreshold(particles[0].Age, particles[0].Lifetime, kStart);
        if (!(std::fabs(newThreshold - oldThreshold) >= 0.001f))
        {
            std::cerr << item << ": 新旧の閾値を区別できない 新=" << newThreshold << " 旧=" << oldThreshold << '\n';
            return false;
        }
        VariableArray<VfxMeshInstance> after;
        system.AppendMeshInstances(after);
        if (!CheckCount(after.size(), 1u, item, "変更後の記録数"))
        {
            return false;
        }
        return CheckFloat(after[0].DissolveThreshold, newThreshold, item, "変更後の閾値", 0.00001f);
    }

    bool TestRejection()
    {
        const char* item = "6. 拒否";
        VfxEffectAsset asset;
        REQUIRE(MakeEffect(asset, true, Math::Vector3(0.0f, 0.0f, 0.0f), false), "エフェクトを作れない");
        VfxEffectAsset screen = asset;
        VfxEmitterAsset screenEmitter = MakeVfxDefaultEmitter("ScreenEmitter");
        screenEmitter.Modules = asset.Emitters[0].Modules;
        screen.Emitters[0] = screenEmitter;
        VfxEffectSystem system;
        Container::String error;
        const auto screenHandle = system.CreateInstance(screen, MakeDesc(), &error);
        if (screenHandle.IsValid() || std::strstr(error.c_str(), "mesh_requires_world_space") == nullptr)
        {
            std::cerr << item << ": 画面の空間を正しく拒否しない ハンドル有効=" << screenHandle.IsValid()
                << " エラー=" << error.c_str() << '\n';
            return false;
        }
        VfxEffectAsset multiple = asset;
        multiple.Emitters[0].Modules.push_back(MakeVfxDefaultModule(VfxModuleType::SpriteRenderer));
        error.clear();
        const auto multipleHandle = system.CreateInstance(multiple, MakeDesc(), &error);
        if (multipleHandle.IsValid() || std::strstr(error.c_str(), "multiple_renderers") == nullptr)
        {
            std::cerr << item << ": 描画モジュールの重複を正しく拒否しない ハンドル有効="
                << multipleHandle.IsValid() << " エラー=" << error.c_str() << '\n';
            return false;
        }
        return true;
    }

    bool TestBoardsAndAlignment()
    {
        const char* item = "7. 板と向き";
        VfxEffectAsset asset;
        REQUIRE(MakeEffect(asset, true, Math::Vector3(0.0f, 0.0f, 0.0f), false), "エフェクトを作れない");
        VfxEmitterAsset sprite = asset.Emitters[0];
        sprite.Name = "SpriteEmitter";
        sprite.Modules[kRendererModule] = MakeVfxDefaultModule(VfxModuleType::SpriteRenderer);
        Container::String error;
        REQUIRE(SetVfxModuleParameter(sprite.Modules[kColorModule], "Color",
            VfxValue::MakeColor(Math::Vector4(0.8f, 0.3f, 0.1f, 0.6f)), &error), "板の色を設定できない");
        asset.Emitters.push_back(sprite);
        VfxEffectSystem system;
        const auto handle = system.CreateInstance(asset, MakeDesc(), &error);
        REQUIRE(handle.IsValid(), "２つのエミッタを持つインスタンスを作れない");
        system.Tick(kDt);
        VariableArray<VfxParticleState> meshParticles;
        VariableArray<VfxParticleState> spriteParticles;
        VariableArray<Rendering::BoardProxy> boards;
        VariableArray<VfxMeshInstance> records;
        REQUIRE(system.CopyParticles(handle, 0u, meshParticles) && system.CopyParticles(handle, 1u, spriteParticles),
            "両エミッタの粒を取得できない");
        system.AppendBoardProxies(boards);
        system.AppendMeshInstances(records);
        if (!CheckCount(meshParticles.size(), 1u, item, "メッシュの粒の数") ||
            !CheckCount(spriteParticles.size(), 1u, item, "板の粒の数") ||
            !CheckCount(boards.size(), spriteParticles.size(), item, "板の記録数") ||
            !CheckCount(records.size(), meshParticles.size(), item, "メッシュの記録数") ||
            !CheckColor(boards[0].Tint, spriteParticles[0].Color, item, "板の粒の色"))
        {
            return false;
        }
        const Math::Vector3 velocities[] = {Math::Vector3(3.0f, 4.0f, 0.0f), Math::Vector3(0.0f, 0.0f, -5.0f)};
        for (const auto& velocity : velocities)
        {
            VfxEffectAsset alignedAsset;
            REQUIRE(MakeEffect(alignedAsset, true, velocity, true), "向きのエフェクトを作れない");
            VfxEffectSystem alignedSystem;
            const auto alignedHandle = alignedSystem.CreateInstance(alignedAsset, MakeDesc(), &error);
            REQUIRE(alignedHandle.IsValid(), "向きのインスタンスを作れない");
            alignedSystem.Tick(kDt);
            VariableArray<VfxMeshInstance> aligned;
            alignedSystem.AppendMeshInstances(aligned);
            if (!CheckCount(aligned.size(), 1u, item, "向きの記録数"))
            {
                return false;
            }
            const auto axis = Math::MatrixUtils::TransformVectorRowVector(aligned[0].World, Math::Vector3(0.0f, 0.0f, 1.0f));
            if (!CheckVector(Math::VectorUtils::Normalize(axis), Math::VectorUtils::Normalize(velocity), item, "速度に向けた +Z"))
            {
                return false;
            }
        }
        return true;
    }

#undef REQUIRE
} // namespace

int main()
{
    const bool counts = TestRecordCounts();
    const bool contents = TestRecordContents();
    const bool time = TestThresholdOverTime();
    const bool edges = TestThresholdEdges();
    const bool live = TestLiveChange();
    const bool rejection = TestRejection();
    const bool boardsAndAlignment = TestBoardsAndAlignment();
    return counts && contents && time && edges && live && rejection && boardsAndAlignment ? 0 : 1;
}

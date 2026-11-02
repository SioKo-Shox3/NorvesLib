// VFX の公開契約を描画や GPU に依存せず反証する。
#include "Particle/VfxEffectSerialization.h"
#include "Particle/VfxEffectSystem.h"
#include "Particle/VfxSchema.h"
#include "Rendering/SceneProxy.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

using namespace NorvesLib;
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Particle;

namespace
{
    constexpr float kDt = 1.0f / 60.0f;
    constexpr uint32_t kRate = 0u;
    constexpr uint32_t kVelocity = 1u;
    constexpr uint32_t kLifetime = 2u;
    constexpr uint32_t kAcceleration = 3u;
    constexpr uint32_t kRenderer = 4u;

    constexpr const char* kMotionJson = R"({
        "format":"norves.vfx", "version":1, "name":"Motion",
        "emitters":[{"name":"Moving", "modules":[
            {"type":"SpawnRate", "Rate":60},
            {"type":"InitVelocityBox", "Min":[1,2,3], "Max":[1,2,3]},
            {"type":"InitLifetime", "Lifetime":2},
            {"type":"Acceleration", "Acceleration":[0,0,0]},
            {"type":"SpriteRenderer"}
        ]}]
    })";

    constexpr const char* kCookJson = R"({
        "format":"norves.vfx", "version":1, "name":"CookParity",
        "flipbooks":[{"name":"Atlas", "texture":"atlas.png", "columns":4, "rows":2,
            "frame_count":6, "frames_per_second":10, "start_frame":0, "loop":true}],
        "emitters":[
            {"name":"Continuous", "properties":{"MaxParticles":256,"Seed":17,"Space":"World"},
             "modules":[
                {"type":"SpawnRate", "Rate":30},
                {"type":"InitVelocityBox", "Min":[-2,1,-3], "Max":[4,6,2]},
                {"type":"InitLifetime", "Lifetime":2},
                {"type":"Acceleration", "Acceleration":[0,-9,0.5]},
                {"type":"SpriteRenderer", "Blend":"Additive", "Flipbook":"Atlas"}
             ]},
            {"name":"Burst", "modules":[
                {"type":"SpawnBurst", "Count":7, "Time":0.25},
                {"type":"InitVelocityBox", "Min":[-1,2,0], "Max":[2,4,3]},
                {"type":"InitLifetime", "Lifetime":4},
                {"type":"SpriteRenderer"}
             ]}
        ]
    })";

    bool Check(bool bCondition, const char* item, const char* reason)
    {
        if (!bCondition)
        {
            std::cerr << item << ": " << reason << '\n';
        }
        return bCondition;
    }

#define REQUIRE(condition, reason) \
    do \
    { \
        if (!Check((condition), item, (reason))) \
        { \
            return false; \
        } \
    } while (false)

    bool Near(float left, float right)
    {
        return std::fabs(left - right) <= 0.0001f;
    }

    bool Near(const Math::Vector3& left, const Math::Vector3& right)
    {
        return Near(left.x, right.x) && Near(left.y, right.y) && Near(left.z, right.z);
    }

    bool SamePosition(const Math::Vector3& left, const Math::Vector3& right)
    {
        return left.x == right.x && left.y == right.y && left.z == right.z;
    }

    bool SameValues(const VariableArray<VfxValue>& left, const VariableArray<VfxValue>& right)
    {
        if (left.size() != right.size())
        {
            return false;
        }
        for (size_t index = 0u; index < left.size(); ++index)
        {
            if (!(left[index] == right[index]))
            {
                return false;
            }
        }
        return true;
    }

    bool SameParticles(const VariableArray<VfxParticleState>& left, const VariableArray<VfxParticleState>& right)
    {
        if (left.size() != right.size())
        {
            return false;
        }
        for (size_t index = 0u; index < left.size(); ++index)
        {
            const auto& a = left[index];
            const auto& b = right[index];
            if (!SamePosition(a.Position, b.Position) || !SamePosition(a.Velocity, b.Velocity) ||
                a.Age != b.Age || a.Lifetime != b.Lifetime || a.BirthTime != b.BirthTime ||
                a.Frame != b.Frame || !(a.UVRect == b.UVRect))
            {
                return false;
            }
        }
        return true;
    }

    bool SameBytes(const VariableArray<uint8_t>& left, const VariableArray<uint8_t>& right)
    {
        return left.size() == right.size() &&
            (left.empty() || std::memcmp(left.data(), right.data(), left.size()) == 0);
    }

    const VfxParameterSchema* Parameter(const VfxObjectSchema* schema, const char* name)
    {
        if (schema == nullptr)
        {
            return nullptr;
        }
        const int32_t index = FindVfxParameterIndex(*schema, name);
        return index >= 0 ? &schema->Parameters[index] : nullptr;
    }

    bool TestSchema()
    {
        const char* item = "1. スキーマ";
        const auto* max = Parameter(&GetVfxEmitterSchema(), "MaxParticles");
        REQUIRE(max != nullptr, "MaxParticles がない");
        REQUIRE(max->Type == VfxValueType::UInt && max->Min == 1.0 && max->Max == 65536.0 &&
            std::strcmp(max->Unit, "count") == 0 && MakeVfxDefaultValue(*max) == VfxValue::MakeUInt(128u),
            "MaxParticles の型・範囲・単位・既定値が違う");
        const auto* lifetime = Parameter(FindVfxModuleSchema(VfxModuleType::InitLifetime), "Lifetime");
        REQUIRE(lifetime != nullptr, "Lifetime がない");
        REQUIRE(lifetime->Type == VfxValueType::Float && lifetime->Min == 0.001 && lifetime->Max == 3600.0 &&
            std::strcmp(lifetime->Unit, "s") == 0 && MakeVfxDefaultValue(*lifetime) == VfxValue::MakeFloat(1.0f),
            "Lifetime の型・範囲・単位・既定値が違う");
        const auto* rate = Parameter(FindVfxModuleSchema(VfxModuleType::SpawnRate), "Rate");
        const auto* acceleration = Parameter(FindVfxModuleSchema(VfxModuleType::Acceleration), "Acceleration");
        REQUIRE(rate != nullptr && std::strcmp(rate->Unit, "1/s") == 0, "Rate の単位が違う");
        REQUIRE(acceleration != nullptr && std::strcmp(acceleration->Unit, "space/s^2") == 0,
            "Acceleration の単位が違う");
        const auto* blend = Parameter(FindVfxModuleSchema(VfxModuleType::SpriteRenderer), "Blend");
        constexpr const char* names[] = {"Opaque", "Masked", "Translucent", "Additive", "Modulate"};
        REQUIRE(blend != nullptr && blend->Type == VfxValueType::Enum && blend->EnumCount == 5u &&
            blend->EnumNames != nullptr, "Blend の列挙がない");
        for (uint32_t index = 0u; index < 5u; ++index)
        {
            REQUIRE(std::strcmp(blend->EnumNames[index], names[index]) == 0, "Blend の列挙名が違う");
        }
        const auto* velocity = FindVfxModuleSchemaByName("InitVelocityBox");
        REQUIRE(velocity != nullptr && velocity->ModuleType == VfxModuleType::InitVelocityBox,
            "名前から InitVelocityBox を取得できない");
        const auto schemas = GetVfxModuleSchemas();
        REQUIRE(!schemas.empty(), "モジュールのスキーマが空");
        for (const auto& schema : schemas)
        {
            Container::String error;
            const auto module = MakeVfxDefaultModule(schema.ModuleType);
            if (!ValidateVfxModule(module, &error))
            {
                std::cerr << item << ": " << schema.Name << " の既定値が検証に失敗\n";
                return false;
            }
        }
        return true;
    }

    bool TestRejectionAndRetention()
    {
        const char* item = "2. 拒否と保持";
        Container::String error;
        VfxEffectAsset asset;
        REQUIRE(LoadVfxEffectFromJson(kMotionJson, asset, error), "基準の JSON を読めない");
        struct InvalidParameter
        {
            uint32_t ModuleIndex;
            const char* Name;
            VfxValue Value;
            const char* Reason;
        };
        const InvalidParameter cases[] = {
            {kLifetime, "Lifetime", VfxValue::MakeFloat(0.0f), "Lifetime 0"},
            {kRate, "Rate", VfxValue::MakeFloat(-1.0f), "Rate -1"},
            {kRate, "Rate", VfxValue::MakeFloat(std::numeric_limits<float>::quiet_NaN()), "Rate NaN"},
            {kLifetime, "Lifetime", VfxValue::MakeVector3(Math::Vector3(1, 2, 3)), "Lifetime の型違い"},
            {kLifetime, "Unknown", VfxValue::MakeFloat(1.0f), "未知の名前"},
            {kVelocity, "Min", VfxValue::MakeVector3(Math::Vector3(2, 2, 3)), "Min > Max"}
        };
        for (const auto& invalid : cases)
        {
            VfxModule module = asset.Emitters[0].Modules[invalid.ModuleIndex];
            const VfxModule previous = module;
            if (SetVfxModuleParameter(module, invalid.Name, invalid.Value, &error) ||
                module.Type != previous.Type || !SameValues(module.Values, previous.Values))
            {
                std::cerr << item << ": 資産の拒否または保持に失敗: " << invalid.Reason << '\n';
                return false;
            }
            VfxEffectSystem changed;
            VfxEffectSystem control;
            const VfxInstanceDesc desc;
            const auto changedHandle = changed.CreateInstance(asset, desc, &error);
            const auto controlHandle = control.CreateInstance(asset, desc, &error);
            REQUIRE(changedHandle.IsValid() && controlHandle.IsValid(), "比較用インスタンスを作れない");
            for (uint32_t step = 0u; step < 5u; ++step)
            {
                changed.Tick(kDt);
                control.Tick(kDt);
            }
            if (changed.SetModuleParameter(changedHandle, 0u, invalid.ModuleIndex, invalid.Name, invalid.Value, &error))
            {
                std::cerr << item << ": 動作中の拒否に失敗: " << invalid.Reason << '\n';
                return false;
            }
            // 寿命の拒否漏れも露見する長さで、生成数と全粒の状態を比較する。
            for (uint32_t step = 0u; step < 180u; ++step)
            {
                changed.Tick(kDt);
                control.Tick(kDt);
                VariableArray<VfxParticleState> actual;
                VariableArray<VfxParticleState> expected;
                REQUIRE(changed.CopyParticles(changedHandle, 0u, actual) &&
                    control.CopyParticles(controlHandle, 0u, expected), "比較用の粒を取得できない");
                if (expected.empty() || !SameParticles(actual, expected))
                {
                    std::cerr << item << ": 動作中の値が保持されない: " << invalid.Reason
                        << " 刻み=" << step << '\n';
                    return false;
                }
            }
        }
        VfxEffectAsset sentinel;
        REQUIRE(LoadVfxEffectFromJson(kCookJson, sentinel, error), "保持確認用の資産を読めない");
        VariableArray<uint8_t> before;
        REQUIRE(CookVfxEffect(sentinel, before, error), "保持確認用の資産をクックできない");
        constexpr const char* badModules[] = {
            R"({"type":"InitLifetime","Lifetime":0})",
            R"({"type":"InitLifetime","Lifetime":[1,2,3]})",
            R"({"type":"InitLifetime","Unknown":1})"
        };
        for (const char* badModule : badModules)
        {
            Container::String json = R"({"format":"norves.vfx","version":1,"name":"Invalid",
                "emitters":[{"name":"First","modules":[{"type":"SpawnRate"}]},
                {"name":"Second","modules":[)";
            json += badModule;
            json += "]}]}";
            VfxEffectAsset output = sentinel;
            REQUIRE(!LoadVfxEffectFromJson(json, output, error), "不正な JSON を受け入れた");
            VariableArray<uint8_t> after;
            REQUIRE(CookVfxEffect(output, after, error) && SameBytes(before, after),
                "JSON の失敗時に outAsset が書き換わった");
        }
        return true;
    }

    bool TestCookParity()
    {
        const char* item = "3. クックの一致";
        Container::String error;
        VfxEffectAsset source;
        VfxEffectAsset cooked;
        VariableArray<uint8_t> bytes;
        REQUIRE(LoadVfxEffectFromJson(kCookJson, source, error), "JSON を読めない");
        REQUIRE(CookVfxEffect(source, bytes, error), "クックできない");
        REQUIRE(LoadCookedVfxEffect({bytes.data(), bytes.size()}, cooked, error), "バイナリを読み戻せない");
        VfxEffectSystem first;
        VfxEffectSystem second;
        VfxInstanceDesc desc;
        desc.Seed = 0x12345678u;
        desc.Position = Math::Vector3(10, 20, -30);
        const auto a = first.CreateInstance(source, desc, &error);
        const auto b = second.CreateInstance(cooked, desc, &error);
        REQUIRE(a.IsValid() && b.IsValid(), "インスタンスを作れない");
        REQUIRE(first.GetEmitterCount(a) == 2u && second.GetEmitterCount(b) == 2u, "エミッタ数が違う");
        bool bObserved[2] = {};
        for (uint32_t step = 0u; step < 180u; ++step)
        {
            first.Tick(kDt);
            second.Tick(kDt);
            for (uint32_t emitter = 0u; emitter < 2u; ++emitter)
            {
                VariableArray<VfxParticleState> left;
                VariableArray<VfxParticleState> right;
                REQUIRE(first.CopyParticles(a, emitter, left) && second.CopyParticles(b, emitter, right),
                    "粒の列を取得できない");
                if (left.size() != right.size())
                {
                    std::cerr << item << ": 粒の数が違う 刻み=" << step << " エミッタ=" << emitter << '\n';
                    return false;
                }
                bObserved[emitter] |= !left.empty();
                for (size_t index = 0u; index < left.size(); ++index)
                {
                    if (!SamePosition(left[index].Position, right[index].Position))
                    {
                        std::cerr << item << ": 位置が完全一致しない 刻み=" << step
                            << " エミッタ=" << emitter << " 粒=" << index << '\n';
                        return false;
                    }
                }
            }
        }
        REQUIRE(bObserved[0] && bObserved[1], "両エミッタで粒が生成されていない");
        REQUIRE(!bytes.empty(), "バイナリが空");
        REQUIRE(!LoadCookedVfxEffect({bytes.data(), bytes.size() - 1u}, cooked, error),
            "末尾を切ったバイナリを受け入れた");
        bytes[0] ^= 0xFFu;
        REQUIRE(!LoadCookedVfxEffect({bytes.data(), bytes.size()}, cooked, error),
            "マジックを壊したバイナリを受け入れた");
        return true;
    }

    bool TestLiveChanges()
    {
        const char* item = "4. 動作中の値の変更";
        Container::String error;
        VfxEffectAsset asset;
        REQUIRE(LoadVfxEffectFromJson(kMotionJson, asset, error), "JSON を読めない");
        VfxEffectSystem system;
        const auto handle = system.CreateInstance(asset, {}, &error);
        REQUIRE(handle.IsValid(), "インスタンスを作れない");
        constexpr uint32_t kWarmupSteps = 8u;
        for (uint32_t step = 0u; step < kWarmupSteps; ++step)
        {
            system.Tick(kDt);
        }
        VariableArray<VfxParticleState> before;
        REQUIRE(system.CopyParticles(handle, 0u, before) && before.size() == kWarmupSteps,
            "変更前の生成数が違う");
        REQUIRE(system.SetModuleParameter(handle, 0u, kAcceleration, "Acceleration",
            VfxValue::MakeVector3(Math::Vector3(0, 10, 0)), &error), "加速度を変更できない");
        VariableArray<VfxParticleState> immediate;
        REQUIRE(system.CopyParticles(handle, 0u, immediate) && SameParticles(before, immediate),
            "刻みの前に粒が変わった");
        system.Tick(kDt);
        VariableArray<VfxParticleState> after;
        REQUIRE(system.CopyParticles(handle, 0u, after) && after.size() == before.size() + 1u,
            "変更後の生成数が違う");
        for (size_t index = 0u; index < before.size(); ++index)
        {
            const auto velocity = before[index].Velocity + Math::Vector3(0, 10 * kDt, 0);
            REQUIRE(Near(after[index].Velocity, velocity), "次の刻みで速度が 10*dt 増えていない");
            REQUIRE(Near(after[index].Position, before[index].Position + velocity * kDt),
                "速度を更新した後の位置積分が違う");
        }
        REQUIRE(system.SetModuleParameter(handle, 0u, kRate, "Rate", VfxValue::MakeFloat(120), &error),
            "SpawnRate を変更できない");
        REQUIRE(system.CopyParticles(handle, 0u, immediate) && SameParticles(after, immediate),
            "SpawnRate の変更直後に粒が変わった");
        system.Tick(kDt);
        REQUIRE(system.CopyParticles(handle, 0u, immediate) && immediate.size() == after.size() + 2u,
            "SpawnRate の変更が次の刻みの生成数に効かない");
        return true;
    }

    bool TestEvents()
    {
        const char* item = "5. イベント";
        constexpr const char* json = R"({"format":"norves.vfx","version":1,"name":"Events",
            "emitters":[
                {"name":"Stationary","modules":[{"type":"SpawnOnEvent","EventName":"Hit","Count":3}]},
                {"name":"Moving","modules":[{"type":"SpawnOnEvent","EventName":"Hit","Count":3},
                    {"type":"InitVelocityBox","Min":[2,-3,4],"Max":[2,-3,4]}]}
            ]})";
        Container::String error;
        VfxEffectAsset asset;
        REQUIRE(LoadVfxEffectFromJson(json, asset, error), "JSON を読めない");
        VfxEffectSystem system;
        VfxInstanceDesc desc;
        desc.Position = Math::Vector3(-10, -20, -30);
        const auto handle = system.CreateInstance(asset, desc, &error);
        REQUIRE(handle.IsValid(), "インスタンスを作れない");
        const Math::Vector3 position(11, 7, -5);
        constexpr double kEventTime = 0.025;
        REQUIRE(system.RaiseEvent(handle, "Hit", position, kEventTime), "Hit を予約できない");
        system.Tick(kDt);
        for (uint32_t emitter = 0u; emitter < 2u; ++emitter)
        {
            VariableArray<VfxParticleState> particles;
            REQUIRE(system.CopyParticles(handle, emitter, particles) && particles.empty(),
                "イベントより前の刻みで粒が出た");
        }
        system.Tick(kDt);
        const float age = static_cast<float>(2.0 * static_cast<double>(kDt) - kEventTime);
        for (uint32_t emitter = 0u; emitter < 2u; ++emitter)
        {
            VariableArray<VfxParticleState> particles;
            REQUIRE(system.CopyParticles(handle, emitter, particles) && particles.size() == 3u,
                "イベントを含む刻みで 3 個出ない");
            for (const auto& particle : particles)
            {
                REQUIRE(std::fabs(particle.BirthTime - kEventTime) <= 1e-9, "BirthTime がイベント時刻と違う");
                REQUIRE(Near(particle.Age, age), "Age が刻み終端からの経過時間と違う");
                if (emitter == 0u)
                {
                    REQUIRE(SamePosition(particle.Position, position), "速度 0 の生成位置が P と違う");
                }
                else
                {
                    REQUIRE(Near(particle.Position, position + Math::Vector3(2, -3, 4) * age),
                        "移動する粒の位置が P + v*Age と違う");
                }
            }
        }
        REQUIRE(!system.RaiseEvent(handle, "Hit", position, kEventTime), "過去の時刻を受け入れた");
        VfxEffectSystem other;
        const auto otherHandle = other.CreateInstance(asset, desc, &error);
        REQUIRE(otherHandle.IsValid() && other.RaiseEvent(otherHandle, "Miss", position, kEventTime),
            "別名のイベントを予約できない");
        other.Tick(kDt);
        other.Tick(kDt);
        for (uint32_t emitter = 0u; emitter < 2u; ++emitter)
        {
            VariableArray<VfxParticleState> particles;
            REQUIRE(other.CopyParticles(otherHandle, emitter, particles) && particles.empty(),
                "名前の違うイベントで粒が出た");
        }
        return true;
    }

    Math::Vector4 ExpectedUV(uint32_t frame)
    {
        // UVRect は左上のオフセットと大きさ。縦方向も行番号とともに増える。
        return Math::Vector4(static_cast<float>(frame % 4u) / 4.0f,
            static_cast<float>(frame / 4u) / 2.0f, 0.25f, 0.5f);
    }

    bool TestFlipbook()
    {
        const char* item = "6. フリップブック";
        Container::String error;
        VfxEffectAsset imported;
        REQUIRE(LoadVfxEffectFromJson(kCookJson, imported, error) && imported.Flipbooks.size() == 1u,
            "取り込み設定を読めない");
        auto flipbook = imported.Flipbooks[0];
        REQUIRE(flipbook.Columns == 4u && flipbook.Rows == 2u && flipbook.FrameCount == 6u &&
            flipbook.FramesPerSecond == 10.0f && flipbook.StartFrame == 0u && flipbook.bLoop,
            "取り込んだフリップブック設定が違う");
        constexpr float ages[] = {0.0f, 0.1f, 0.25f, 0.55f, 0.65f};
        constexpr uint32_t frames[] = {0u, 1u, 2u, 5u, 0u};
        for (uint32_t index = 0u; index < 5u; ++index)
        {
            REQUIRE(VfxEffectSystem::ComputeFlipbookFrame(flipbook, ages[index], 1.0f) == frames[index],
                "10 FPS のコマ番号が違う");
        }
        for (uint32_t frame = 0u; frame < 6u; ++frame)
        {
            REQUIRE(VfxEffectSystem::ComputeFlipbookUVRect(flipbook, frame) == ExpectedUV(frame),
                "UVRect の列・行・大きさが違う");
        }
        flipbook.FramesPerSecond = 0.0f;
        REQUIRE(VfxEffectSystem::ComputeFlipbookFrame(flipbook, 0.5f, 1.0f) == 3u,
            "寿命に合わせる再生のコマ番号が違う");
        flipbook.FramesPerSecond = 10.0f;
        flipbook.bLoop = false;
        REQUIRE(VfxEffectSystem::ComputeFlipbookFrame(flipbook, 0.65f, 1.0f) == 5u &&
            VfxEffectSystem::ComputeFlipbookFrame(flipbook, 3.0f, 4.0f) == 5u,
            "Loop なしで最後のコマに止まらない");
        VfxEffectAsset asset;
        REQUIRE(LoadVfxEffectFromJson(kMotionJson, asset, error), "インスタンス用の資産を読めない");
        asset.Flipbooks = imported.Flipbooks;
        REQUIRE(SetVfxModuleParameter(asset.Emitters[0].Modules[kRenderer], "Flipbook",
            VfxValue::MakeName("Atlas"), &error), "描画モジュールにフリップブックを設定できない");
        VfxEffectSystem system;
        const auto handle = system.CreateInstance(asset, {}, &error);
        REQUIRE(handle.IsValid(), "インスタンスを作れない");
        bool bObservedFrames[6] = {};
        for (uint32_t step = 0u; step < 50u; ++step)
        {
            system.Tick(kDt);
            VariableArray<VfxParticleState> particles;
            VariableArray<Rendering::BoardProxy> proxies;
            REQUIRE(system.CopyParticles(handle, 0u, particles) && !particles.empty(), "実際の粒が出ない");
            system.AppendBoardProxies(proxies);
            REQUIRE(proxies.size() == particles.size(), "板の数が粒の数と違う");
            for (size_t index = 0u; index < particles.size(); ++index)
            {
                const auto& particle = particles[index];
                const uint32_t expectedFrame = static_cast<uint32_t>(std::floor(particle.Age * 10.0f)) % 6u;
                bObservedFrames[expectedFrame] = true;
                REQUIRE(particle.Frame == expectedFrame && particle.UVRect == ExpectedUV(expectedFrame),
                    "粒の Age に対するコマ番号または UVRect が違う");
                REQUIRE(proxies[index].UVRect == ExpectedUV(expectedFrame), "板へ渡す UVRect が違う");
            }
        }
        for (bool bObserved : bObservedFrames)
        {
            REQUIRE(bObserved, "実際の粒で全コマを通過していない");
        }
        return true;
    }

#undef REQUIRE
} // namespace

int main()
{
    const bool bSchema = TestSchema();
    const bool bRetention = TestRejectionAndRetention();
    const bool bCook = TestCookParity();
    const bool bLiveChanges = TestLiveChanges();
    const bool bEvents = TestEvents();
    const bool bFlipbook = TestFlipbook();
    return bSchema && bRetention && bCook && bLiveChanges && bEvents && bFlipbook ? 0 : 1;
}

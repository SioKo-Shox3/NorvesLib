// しがみつきの調整値の読み込み、入力の拒否、失敗時の出力保持を確かめる。
#include "Gameplay/Grip/GripTuning.h"

#include <cstdio>
#include <cstdlib>

namespace
{
    using namespace NorvesLib::Modules::Gameplay::Grip;
    using String = NorvesLib::Core::Container::String;

#define GRIP_TUNING_CHECK(expr) \
    do \
    { \
        if (!(expr)) \
        { \
            std::fprintf(stderr, "GripTuningLoadTest %d: %s\n", __LINE__, #expr); \
            std::abort(); \
        } \
    } while (false)

    const char* const kTuning = R"({"freeLoadG":2,"panicLoadG":5,"baseDrainPerSecond":0.0625,"loadDrainPerSecond":0.5,"recoveryPerSecond":0.25,"smoothingSeconds":0.125,"tensionWeight":0.75,"accelerationWeight":0.25,"maxSubsteps":4,"maxLoadG":12,"swingDampingPerSecond":0.75})";

    String Replace(const String& source, const char* before, const char* after)
    {
        String result = source;
        const String target(before);
        const size_t position = result.find(target);
        GRIP_TUNING_CHECK(position != String::npos);
        result.replace(position, target.size(), after);
        return result;
    }

    void CheckTuningValues(const GripTuning& tuning)
    {
        GRIP_TUNING_CHECK(tuning.FreeLoadG == 2.0f);
        GRIP_TUNING_CHECK(tuning.PanicLoadG == 5.0f);
        GRIP_TUNING_CHECK(tuning.BaseDrainPerSecond == 0.0625f);
        GRIP_TUNING_CHECK(tuning.LoadDrainPerSecond == 0.5f);
        GRIP_TUNING_CHECK(tuning.RecoveryPerSecond == 0.25f);
        GRIP_TUNING_CHECK(tuning.SmoothingSeconds == 0.125f);
        GRIP_TUNING_CHECK(tuning.TensionWeight == 0.75f);
        GRIP_TUNING_CHECK(tuning.AccelerationWeight == 0.25f);
        GRIP_TUNING_CHECK(tuning.MaxSubsteps == 4);
        GRIP_TUNING_CHECK(tuning.MaxLoadG == 12.0f);
        GRIP_TUNING_CHECK(tuning.SwingDampingPerSecond == 0.75f);
    }

    void RejectTuning(const String& text, const char* path)
    {
        // 入力とも既定値とも異なる値で、途中までの書き換えも検出する。
        const GripTuning original{3.0f, 6.0f, 0.125f, 0.75f, 0.5f, 0.25f, 0.5f, 0.75f, 2, 15.0f, 1.0f};
        GripTuning out = original;
        String error;
        GRIP_TUNING_CHECK(!LoadGripTuningFromJson(text, "unit.grip.tuning.json", out, error));
        GRIP_TUNING_CHECK(error.find(String("unit.grip.tuning.json: ") + path + ": ") == 0);
        GRIP_TUNING_CHECK(out.FreeLoadG == original.FreeLoadG);
        GRIP_TUNING_CHECK(out.PanicLoadG == original.PanicLoadG);
        GRIP_TUNING_CHECK(out.BaseDrainPerSecond == original.BaseDrainPerSecond);
        GRIP_TUNING_CHECK(out.LoadDrainPerSecond == original.LoadDrainPerSecond);
        GRIP_TUNING_CHECK(out.RecoveryPerSecond == original.RecoveryPerSecond);
        GRIP_TUNING_CHECK(out.SmoothingSeconds == original.SmoothingSeconds);
        GRIP_TUNING_CHECK(out.TensionWeight == original.TensionWeight);
        GRIP_TUNING_CHECK(out.AccelerationWeight == original.AccelerationWeight);
        GRIP_TUNING_CHECK(out.MaxSubsteps == original.MaxSubsteps);
        GRIP_TUNING_CHECK(out.MaxLoadG == original.MaxLoadG);
        GRIP_TUNING_CHECK(out.SwingDampingPerSecond == original.SwingDampingPerSecond);
    }

    void CheckBundledAsset()
    {
        GripTuning tuning;
        String error;
        GRIP_TUNING_CHECK(LoadGripTuningFromJson(kTuning, "unit.grip.tuning.json", tuning, error));
        GRIP_TUNING_CHECK(LoadGripTuningFromAsset("Gameplay/grip.tuning.json", tuning, error) && error.empty());
        GRIP_TUNING_CHECK(tuning.FreeLoadG == 1.5f);
        GRIP_TUNING_CHECK(tuning.PanicLoadG == 4.0f);
        GRIP_TUNING_CHECK(tuning.BaseDrainPerSecond == 0.02f);
        GRIP_TUNING_CHECK(tuning.LoadDrainPerSecond == 0.25f);
        GRIP_TUNING_CHECK(tuning.RecoveryPerSecond == 0.1f);
        GRIP_TUNING_CHECK(tuning.SmoothingSeconds == 0.1f);
        GRIP_TUNING_CHECK(tuning.TensionWeight == 1.0f);
        GRIP_TUNING_CHECK(tuning.AccelerationWeight == 0.0f);
        GRIP_TUNING_CHECK(tuning.MaxSubsteps == 8);
        GRIP_TUNING_CHECK(tuning.MaxLoadG == 10.0f);
        GRIP_TUNING_CHECK(tuning.SwingDampingPerSecond == 0.5f);
    }
} // 無名名前空間

int main()
{
    GripTuning tuning;
    String error;
    GRIP_TUNING_CHECK(LoadGripTuningFromJson(kTuning, "unit.grip.tuning.json", tuning, error) && error.empty());
    CheckTuningValues(tuning);

    RejectTuning(Replace(kTuning, R"("baseDrainPerSecond":0.0625,)", ""), "baseDrainPerSecond");
    RejectTuning(Replace(kTuning, R"("freeLoadG":2)", R"("freeLoadG":2,"unexpected":true)"), "unexpected");
    RejectTuning(Replace(kTuning, R"("baseDrainPerSecond":0.0625)", R"("baseDrainPerSecond":"invalid")"), "baseDrainPerSecond");
    RejectTuning(Replace(kTuning, R"("panicLoadG":5)", R"("panicLoadG":2)"), "panicLoadG");
    RejectTuning(Replace(kTuning, R"("maxSubsteps":4)", R"("maxSubsteps":0)"), "maxSubsteps");
    RejectTuning(Replace(kTuning, R"("maxSubsteps":4)", R"("maxSubsteps":9)"), "maxSubsteps");
    RejectTuning(Replace(kTuning, R"("baseDrainPerSecond":0.0625)", R"("baseDrainPerSecond":-1)"), "baseDrainPerSecond");
    RejectTuning(Replace(kTuning, R"("tensionWeight":0.75,"accelerationWeight":0.25)", R"("tensionWeight":0,"accelerationWeight":0)"), "accelerationWeight");
    RejectTuning(Replace(kTuning, R"("maxLoadG":12)", R"("maxLoadG":4)"), "maxLoadG");
    RejectTuning(Replace(kTuning, R"("swingDampingPerSecond":0.75)", R"("swingDampingPerSecond":1e400)"), "swingDampingPerSecond");
    RejectTuning("[]", "$");

    CheckBundledAsset();
    std::puts("GripTuningLoadTest PASS");
    return 0;
}

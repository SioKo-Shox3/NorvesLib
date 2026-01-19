#include "Random/Random.h"
#include <chrono>
#include <thread>

namespace NorvesLib::Random
{

    // スレッド単位のグローバル乱数生成器
    thread_local Generator *g_threadLocalGenerator = nullptr;

    Generator::Generator()
    {
        // 現在時刻をシードとして使用
        auto now = std::chrono::high_resolution_clock::now();
        auto seed = static_cast<uint32_t>(now.time_since_epoch().count());
        SetSeed(seed);
    }

    Generator::Generator(uint32_t seed)
    {
        SetSeed(seed);
    }

    void Generator::SetSeed(uint32_t seed)
    {
        m_engine.seed(seed);
    }

    int32_t Generator::GetInt(int32_t min, int32_t max)
    {
        std::uniform_int_distribution<int32_t> distribution(min, max);
        return distribution(m_engine);
    }

    float Generator::GetFloat(float min, float max)
    {
        std::uniform_real_distribution<float> distribution(min, max);
        return distribution(m_engine);
    }

    double Generator::GetDouble(double min, double max)
    {
        std::uniform_real_distribution<double> distribution(min, max);
        return distribution(m_engine);
    }

    bool Generator::GetBool(float probability)
    {
        std::bernoulli_distribution distribution(probability);
        return distribution(m_engine);
    }

    Generator &GetGlobalGenerator()
    {
        // スレッド単位のグローバル乱数生成器がまだ作成されていない場合は初期化
        if (g_threadLocalGenerator == nullptr)
        {
            g_threadLocalGenerator = new Generator();
        }
        return *g_threadLocalGenerator;
    }

    int32_t GetRandomInt(int32_t min, int32_t max)
    {
        return GetGlobalGenerator().GetInt(min, max);
    }

    float GetRandomFloat(float min, float max)
    {
        return GetGlobalGenerator().GetFloat(min, max);
    }

    bool GetRandomBool(float probability)
    {
        return GetGlobalGenerator().GetBool(probability);
    }

} // namespace NorvesLib::Random
